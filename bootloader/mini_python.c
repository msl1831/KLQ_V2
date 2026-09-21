#include "mini_python.h"
#include "board.h"
#include "klq_runtime.h"
#include <string.h>

#define LOOP_MAX 8u
#define ARG_MAX 3u

typedef struct { const char *s; uint16_t n; int32_t m; uint8_t string; } Arg;
typedef struct { uint32_t body, remaining; uint16_t indent; uint8_t forever; } Loop;
enum { WAIT_NONE=0, WAIT_TIME, WAIT_MOTOR, WAIT_MOVE, WAIT_DISTANCE, WAIT_TILT, WAIT_BUTTON };

static const uint8_t *src;
static uint32_t src_n, pc, error_pc, deadline;
static int32_t wait_a, wait_b, wait_c;
static uint8_t wait_kind, depth;
static mini_status_t status;
static mini_error_t error;
static Loop loop[LOOP_MAX];

static bool eq(const char *p, uint16_t n, const char *q)
{
    uint16_t m=(uint16_t)strlen(q);
    return n==m && !memcmp(p,q,n);
}

static void fail(mini_error_t e, uint32_t at)
{
    error=e; error_pc=at; status=MINI_ERROR; wait_kind=WAIT_NONE;
    klq_runtime_stop_all();
}

static uint32_t line_at(uint32_t at, const char **p, uint16_t *n, uint16_t *indent, bool *tab)
{
    uint32_t end=at, next;
    uint16_t i=0, cut;
    uint8_t quote=0;
    while (end<src_n && src[end]!='\n' && src[end]!='\r') ++end;
    next=end;
    while (next<src_n && (src[next]=='\n' || src[next]=='\r')) ++next;
    *tab=false;
    while (at+i<end && (src[at+i]==' ' || src[at+i]=='\t')) {
        if (src[at+i]=='\t') *tab=true;
        ++i;
    }
    *indent=i; *p=(const char *)src+at+i; cut=(uint16_t)(end-at-i);
    for (i=0;i<cut;i++) {
        char c=(*p)[i];
        if ((c=='"' || c=='\'') && (!quote || quote==(uint8_t)c)) quote=quote?0:(uint8_t)c;
        else if (c=='#' && !quote) { cut=i; break; }
    }
    while (cut && ((*p)[cut-1]==' ' || (*p)[cut-1]=='\t')) --cut;
    *n=cut;
    return next;
}

static bool fixed(const char *p, uint16_t n, int32_t *value)
{
    uint32_t v=0, frac=0, scale=100; uint16_t i=0; bool neg=false, dot=false, any=false;
    if (i<n && (p[i]=='-' || p[i]=='+')) neg=p[i++]=='-';
    for (;i<n;i++) {
        uint8_t c=(uint8_t)p[i];
        if (c=='.' && !dot) { dot=true; continue; }
        if (c<'0' || c>'9') return false;
        any=true;
        if (!dot) {
            if (v>214748u || (v==214748u && c>'3')) return false;
            v=v*10u+c-'0';
        } else {
            if (!scale) return false;
            frac+=(c-'0')*scale; scale/=10u;
        }
    }
    if (!any || v>2147483u || v*1000u+frac>2147483647u) return false;
    *value=(int32_t)(v*1000u+frac);
    if (neg) *value=-*value;
    return true;
}

static bool args(const char *p, uint16_t n, Arg a[ARG_MAX], uint8_t *count)
{
    uint16_t i=0,start; uint8_t c=0;
    while (i<n && p[i]==' ') ++i;
    if (i==n) { *count=0; return true; }
    while (i<n) {
        if (c==ARG_MAX) return false;
        while (i<n && p[i]==' ') ++i;
        if (i<n && (p[i]=='"' || p[i]=='\'')) {
            char q=p[i++]; start=i;
            while (i<n && p[i]!=q) ++i;
            if (i==n) return false;
            a[c].s=p+start; a[c].n=(uint16_t)(i-start); a[c].string=1; ++i;
        } else {
            start=i;
            while (i<n && p[i]!=',' && p[i]!=' ') ++i;
            a[c].s=p+start; a[c].n=(uint16_t)(i-start); a[c].string=0;
            if (!fixed(a[c].s,a[c].n,&a[c].m)) return false;
        }
        ++c; while (i<n && p[i]==' ') ++i;
        if (i==n) break;
        if (p[i++]!=',') return false;
    }
    *count=c; return true;
}

static bool integer(const Arg *a, int32_t *v)
{
    if (a->string || a->m%1000) return false;
    *v=a->m/1000; return true;
}

static int hex(char c)
{
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}

static bool call(const char *p, uint16_t n, uint32_t at)
{
    const char *name=p; uint16_t name_n=0, arg_n; uint8_t ac; Arg a[ARG_MAX]; int32_t x,y,z; int r;
    uint8_t pattern[13],i;
    if (n>=4u && !memcmp(p,"klq.",4)) { name+=4; n-=4; }
    while (name_n<n && name[name_n]!='(') ++name_n;
    if (name_n==n || n<name_n+2u || name[n-1]!=')') { fail(MINI_ERR_SYNTAX,at); return false; }
    arg_n=(uint16_t)(n-name_n-2u);
    if (!args(name+name_n+1u,arg_n,a,&ac)) { fail(MINI_ERR_ARGUMENT,at); return false; }

    if (eq(name,name_n,"wait")) {
        if (ac!=1u || a[0].string || a[0].m<0 || a[0].m>3600000) goto arg;
        deadline=board_ms+(uint32_t)a[0].m; wait_kind=WAIT_TIME; return true;
    }
    if (eq(name,name_n,"stop")) { if (ac) goto arg; status=MINI_DONE; klq_runtime_stop_all(); return true; }
    if (eq(name,name_n,"motor_power") || eq(name,name_n,"move_power")) {
        if (ac!=1u || !integer(a,&x) || x<1 || x>3) goto range;
        if (eq(name,name_n,"motor_power")) klq_runtime_set_motor_power((unsigned)x);
        else klq_runtime_set_move_power((unsigned)x);
        return true;
    }
    if (eq(name,name_n,"motor_run")) {
        if (ac!=2u || !integer(a,&x) || !integer(a+1,&y)) goto arg;
        r=klq_runtime_motor((unsigned)x,(int)y,true); goto runtime;
    }
    if (eq(name,name_n,"motor_stop")) {
        if (ac!=1u || !integer(a,&x)) goto arg;
        r=klq_runtime_motor((unsigned)x,1,false); goto runtime;
    }
    if (eq(name,name_n,"motor_for")) {
        if (ac!=3u || !integer(a,&x) || !integer(a+1,&y) || a[2].string ||
            a[2].m<0 || a[2].m>3600000) goto arg;
        r=klq_runtime_motor((unsigned)x,(int)y,true);
        if (r==KLQ_RT_OK) { wait_a=x; wait_b=y; deadline=board_ms+(uint32_t)a[2].m; wait_kind=WAIT_MOTOR; return true; }
        goto runtime;
    }
    if (eq(name,name_n,"move")) {
        if (ac!=1u || !integer(a,&x)) goto arg;
        r=klq_runtime_move((unsigned)x,true); goto runtime;
    }
    if (eq(name,name_n,"move_stop")) {
        if (ac) goto arg; r=klq_runtime_move(0,false); goto runtime;
    }
    if (eq(name,name_n,"move_for")) {
        if (ac!=2u || !integer(a,&x) || a[1].string || a[1].m<0 || a[1].m>3600000) goto arg;
        r=klq_runtime_move((unsigned)x,true);
        if (r==KLQ_RT_OK) { wait_a=x; deadline=board_ms+(uint32_t)a[1].m; wait_kind=WAIT_MOVE; return true; }
        goto runtime;
    }
    if (eq(name,name_n,"wait_distance")) {
        if (ac!=3u || !integer(a,&x) || !integer(a+1,&y) || !integer(a+2,&z) ||
            x<1 || x>3 || (y!=-1 && y!=1) || z<0 || z>6553) goto range;
        wait_a=x; wait_b=y; wait_c=z; wait_kind=WAIT_DISTANCE; return true;
    }
    if (eq(name,name_n,"wait_ir")) {
        if (ac!=3u || !integer(a,&x) || !integer(a+1,&y) || !integer(a+2,&z) ||
            x<1 || x>3 || (y!=-1 && y!=1) || z<0 || z>100) goto range;
        fail(MINI_ERR_UNSUPPORTED,at); return false;
    }
    if (eq(name,name_n,"wait_tilt")) {
        if (ac!=1u || !integer(a,&x) || x<0 || x>3) goto range;
        wait_a=x; wait_kind=WAIT_TILT; return true;
    }
    if (eq(name,name_n,"wait_button")) {
        if (ac!=1u || !integer(a,&x) || (x!=0 && x!=1)) goto range;
        wait_a=x; wait_kind=WAIT_BUTTON; return true;
    }
    if (eq(name,name_n,"display_face")) {
        if (ac!=1u || !integer(a,&x) || x<0 || x>9) goto range;
        if (!klq_runtime_face((unsigned)x)) goto runerr; return true;
    }
    if (eq(name,name_n,"display_number")) {
        if (ac!=1u || !integer(a,&x) || x<0 || x>100) goto range;
        if (!klq_runtime_number((unsigned)x)) goto runerr; return true;
    }
    if (eq(name,name_n,"display_pattern")) {
        if (ac!=1u || !a[0].string || a[0].n!=26u) goto arg;
        for (i=0;i<13u;i++) { int h=hex(a[0].s[i*2u]),l=hex(a[0].s[i*2u+1u]); if (h<0 || l<0 || ((h<<4)|l)>0x7f) goto arg; pattern[i]=(uint8_t)((h<<4)|l); }
        if (!klq_runtime_pattern(pattern)) goto runerr; return true;
    }
    if (eq(name,name_n,"display_off")) {
        if (ac) goto arg; if (!klq_runtime_display_off()) goto runerr; return true;
    }
    fail(MINI_ERR_NAME,at); return false;
runtime:
    if (r==KLQ_RT_OK) return true;
    fail(r==KLQ_RT_UNSUPPORTED?MINI_ERR_UNSUPPORTED:(r==KLQ_RT_RANGE?MINI_ERR_RANGE:MINI_ERR_RUNTIME),at);
    return false;
arg: fail(MINI_ERR_ARGUMENT,at); return false;
range: fail(MINI_ERR_RANGE,at); return false;
runerr: fail(MINI_ERR_RUNTIME,at); return false;
}

static bool pending(void)
{
    if (!wait_kind) return false;
    if (wait_kind==WAIT_TIME || wait_kind==WAIT_MOTOR || wait_kind==WAIT_MOVE) {
        if ((int32_t)(board_ms-deadline)<0) return true;
        if (wait_kind==WAIT_MOTOR) klq_runtime_motor((unsigned)wait_a,(int)wait_b,false);
        else if (wait_kind==WAIT_MOVE) klq_runtime_move((unsigned)wait_a,false);
        wait_kind=WAIT_NONE; return false;
    }
    if (wait_kind==WAIT_DISTANCE && klq_runtime_distance((unsigned)wait_a,(int)wait_b,wait_c)) wait_kind=WAIT_NONE;
    else if (wait_kind==WAIT_TILT && klq_runtime_tilt((unsigned)wait_a)) wait_kind=WAIT_NONE;
    else if (wait_kind==WAIT_BUTTON && klq_runtime_button(wait_a!=0)) wait_kind=WAIT_NONE;
    return wait_kind!=WAIT_NONE;
}

void mini_python_init(const uint8_t *source, uint32_t length)
{
    src=source; src_n=length; pc=error_pc=deadline=0; wait_kind=depth=0; status=MINI_IDLE; error=MINI_ERR_NONE;
}

bool mini_python_start(void)
{
    if (!src || !src_n) return false;
    pc=error_pc=deadline=0; wait_kind=depth=0; error=MINI_ERR_NONE; status=MINI_RUNNING;
    return true;
}

mini_status_t mini_python_poll(void)
{
    const char *p,*q; uint16_t n,ind,qn,qi; bool tab,qt; uint32_t at,next,look,candidate; int32_t count;
    if (status!=MINI_RUNNING || pending()) return status;
again:
    if (pc>=src_n) {
        if (depth) {
            Loop *l=&loop[depth-1u];
            if (l->forever || l->remaining>1u) { if (!l->forever) --l->remaining; pc=l->body; goto again; }
            --depth; goto again;
        }
        status=MINI_DONE; klq_runtime_stop_all(); return status;
    }
    at=pc; next=line_at(at,&p,&n,&ind,&tab);
    if (tab || (ind&3u)) { fail(MINI_ERR_SYNTAX,at); return status; }
    pc=next;
    if (!n) goto again;
    if (depth && ind<=loop[depth-1u].indent) {
        Loop *l=&loop[depth-1u];
        if (l->forever || l->remaining>1u) { if (!l->forever) --l->remaining; pc=l->body; goto again; }
        --depth; pc=at; goto again;
    }
    if ((!depth && ind) || (depth && ind!=(uint16_t)(loop[depth-1u].indent+4u))) {
        fail(MINI_ERR_SYNTAX,at); return status;
    }
    if (eq(p,n,"import klq") || eq(p,n,"from klq import *") || eq(p,n,"pass")) return status;
    if ((n>=7u && !memcmp(p,"import ",7)) || (n>=5u && !memcmp(p,"from ",5))) {
        fail(MINI_ERR_NAME,at); return status;
    }
    if (eq(p,n,"while True:")) {
        if (depth==LOOP_MAX) { fail(MINI_ERR_STACK,at); return status; }
        look=next;
        do { if (look>=src_n) { fail(MINI_ERR_SYNTAX,at); return status; } look=line_at(look,&q,&qn,&qi,&qt); } while (!qn);
        if (qt || qi!=(uint16_t)(ind+4u)) { fail(MINI_ERR_SYNTAX,at); return status; }
        loop[depth].body=next; loop[depth].indent=ind; loop[depth].forever=1; loop[depth].remaining=0; ++depth;
        return status;
    }
    if (n>17u && !memcmp(p,"for _ in range(",15) && p[n-2]==')' && p[n-1]==':') {
        if (!fixed(p+15,(uint16_t)(n-17u),&count) || count%1000 || count<0) { fail(MINI_ERR_ARGUMENT,at); return status; }
        count/=1000;
        look=next;
        do { if (look>=src_n) { fail(MINI_ERR_SYNTAX,at); return status; } look=line_at(look,&q,&qn,&qi,&qt); } while (!qn);
        if (qt || qi!=(uint16_t)(ind+4u)) { fail(MINI_ERR_SYNTAX,at); return status; }
        if (!count) {
            pc=look;
            while (pc<src_n) {
                candidate=pc; pc=line_at(pc,&q,&qn,&qi,&qt);
                if (qn && qi<=ind) { pc=candidate; break; }
            }
            return status;
        }
        if (depth==LOOP_MAX) { fail(MINI_ERR_STACK,at); return status; }
        loop[depth].body=next; loop[depth].indent=ind; loop[depth].forever=0; loop[depth].remaining=(uint32_t)count; ++depth;
        return status;
    }
    call(p,n,at);
    return status;
}

void mini_python_stop(void) { status=MINI_DONE; wait_kind=WAIT_NONE; klq_runtime_stop_all(); }
mini_status_t mini_python_status(void) { return status; }
mini_error_t mini_python_error(void) { return error; }
uint16_t mini_python_error_line(void)
{
    uint32_t i; uint16_t line=1;
    if (error==MINI_ERR_NONE) return 0;
    for (i=0;i<error_pc && i<src_n;i++) if (src[i]=='\n') ++line;
    return line;
}
