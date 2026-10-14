#include "klq_mock.h"
#include "mini_python.h"
#include "klq_runtime.h"
#include "external_ports.h"
#include "robot_ui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

volatile uint32_t board_ms;
static unsigned gpio, stop_count, display_count, motor_count;
static bool display_ok, supported[3], running[3];
static uint8_t picture[13];
static klq_port_status_t ports[3];
typedef struct { unsigned port,power; int direction; bool run; uint32_t at; } Motor;
static Motor motors[128];
static unsigned assertions, groups;
#define CHECK(e) do { ++assertions; if (!(e)) { fprintf(stderr,"FAIL %s:%u: %s\n",__FILE__,(unsigned)__LINE__,#e); exit(1); } } while (0)
#define GROUP(s) do { ++groups; printf("PASS %s\n",s); } while (0)

void RCM_EnableAPB2PeriphClock(unsigned v) { (void)v; }
void GPIO_Config(void *p, GPIO_Config_T *io) { (void)p; CHECK(io->pin==GPIO_PIN_3 && io->mode==GPIO_MODE_IN_PU); }
unsigned GPIO_ReadInputBit(void *p, unsigned pin) { (void)p; CHECK(pin==GPIO_PIN_3); return gpio; }
const klq_port_status_t *external_port_status(unsigned p) { return p<3?ports+p:NULL; }
bool external_port_motor(unsigned p,int d,bool run,unsigned power)
{
    CHECK(p<3); CHECK(motor_count<128);
    motors[motor_count].port=p; motors[motor_count].power=power;
    motors[motor_count].direction=d; motors[motor_count].run=run;
    motors[motor_count++].at=board_ms;
    if (!run) running[p]=false;
    else if (supported[p]) running[p]=true;
    return !run || supported[p];
}
void external_ports_motor_stop_all(void) { ++stop_count; memset(running,0,sizeof(running)); }
bool robot_ui_set_user_pattern(const uint8_t p[13])
{
    unsigned i; if (!display_ok) return false;
    for (i=0;i<13;i++) CHECK(p[i]<=127);
    memcpy(picture,p,13); ++display_count; return true;
}
static void reset(void)
{
    board_ms=100; gpio=1; stop_count=display_count=motor_count=0; display_ok=true;
    memset(ports,0,sizeof(ports)); memset(supported,1,sizeof(supported));
    memset(running,0,sizeof(running)); memset(picture,0,sizeof(picture));
    klq_runtime_init(); klq_runtime_set_motor_power(2); klq_runtime_set_move_power(2);
}
static void begin(const char *p) { mini_python_init((const uint8_t *)p,(uint32_t)strlen(p)); CHECK(mini_python_start()); }
static mini_status_t step(void) { klq_runtime_poll(); return mini_python_poll(); }
static mini_status_t finish(unsigned max)
{
    mini_status_t s=mini_python_status();
    while (max-- && s==MINI_RUNNING) s=step();
    return s;
}
static void done(void) { CHECK(finish(100)==MINI_DONE); CHECK(mini_python_error()==MINI_ERR_NONE); CHECK(stop_count>=1); }
static void expect_error(const char *p,mini_error_t error,unsigned line)
{
    reset(); begin(p); CHECK(finish(100)==MINI_ERROR);
    CHECK(mini_python_error()==error); CHECK(mini_python_error_line()==line);
    CHECK(stop_count==1 && !running[0] && !running[1] && !running[2]);
}
static void sample(unsigned port,unsigned status,unsigned mm,unsigned age)
{
    klq_port_status_t *s=ports+port;
    memset(s,0,sizeof(*s)); s->online=1; s->device_class=1; s->sensor_type=1;
    s->sample_status=(uint8_t)status; s->distance_mm=(uint16_t)mm; s->sample_host_ms=board_ms-age;
}

static void control(void)
{
    unsigned i;
    reset(); begin("# start\r\nimport klq\r\n\r\npass\r\nklq.display_number(7) # output\r\n"); done(); CHECK(display_count==1);
    reset(); begin("from klq import *\ndisplay_number(12)\n"); done(); CHECK(display_count==1); GROUP("01 start, import, bare calls, comments and CRLF");
    reset(); begin("klq.wait(1.234)\nklq.display_number(1)\n"); CHECK(step()==MINI_RUNNING);
    board_ms=1333; CHECK(step()==MINI_RUNNING && display_count==0);
    board_ms=1334; CHECK(step()==MINI_RUNNING && display_count==1); done();
    reset(); board_ms=0xfffffff0u; begin("klq.wait(0.032)\nklq.display_number(1)\n"); step();
    board_ms=15; CHECK(step()==MINI_RUNNING && display_count==0); board_ms=16; step(); CHECK(display_count==1); done();
    reset(); begin("klq.wait(0)\nklq.wait(3600)\n"); step(); step(); board_ms+=3599999; CHECK(step()==MINI_RUNNING); ++board_ms; done(); GROUP("02 wait decimal, zero, 3600s, exact deadline, uint32 wrap");
    reset(); begin("for _ in range(0):\n    klq.missing()\nfor _ in range(3):\n    for _ in range(2):\n        klq.display_number(7)\nklq.display_number(1)\n"); done(); CHECK(display_count==7); GROUP("03 finite, nested and zero-count loops");
    reset(); begin("while True:\n    klq.display_number(1)\n"); CHECK(finish(10000)==MINI_RUNNING); CHECK(display_count>1000); mini_python_stop(); CHECK(mini_python_status()==MINI_DONE && stop_count==1);
    reset(); begin("while True:\n    pass\n"); for (i=0;i<10000;i++) CHECK(step()==MINI_RUNNING); mini_python_stop(); CHECK(mini_python_status()==MINI_DONE); GROUP("04 no-wait forever/pass loops and external stop");
    reset(); begin("klq.motor_run(1,1)\nklq.stop()\nklq.display_number(8)\n"); done(); CHECK(display_count==0 && !running[0]); GROUP("05 stop block prevents following action and stops motors");
}

static void motor(void)
{
    unsigned p,l,d; int sign; char source[200];
    for (p=1;p<=3;p++) for (sign=-1;sign<=1;sign+=2) {
        reset(); sprintf(source,"klq.motor_for(%u,%d,0.123)\nklq.display_number(7)\n",p,sign); begin(source); step();
        CHECK(motor_count==1 && running[p-1] && motors[0].direction==sign && motors[0].power==2);
        board_ms=222; step(); CHECK(running[p-1] && display_count==0);
        board_ms=223; step(); CHECK(!running[p-1] && motor_count==2 && !motors[1].run && display_count==1); done();
    } GROUP("06/07 motor_for both directions, all ports, exact deadline");
    for (sign=-1;sign<=1;sign+=2) {
        reset(); sprintf(source,"klq.motor_run(3,%d)\nklq.wait(1)\nklq.motor_stop(3)\n",sign); begin(source); step(); step();
        board_ms+=999; step(); CHECK(running[2]); ++board_ms; step(); CHECK(!running[2] && motors[1].direction==0); done();
    } GROUP("08/09/10 motor_run positive/negative and motor_stop");
    for (l=1;l<=3;l++) {
        reset(); sprintf(source,"klq.motor_power(%u)\nklq.motor_run(1,1)\n",l); begin(source); done(); CHECK(motors[0].power==l);
    } GROUP("11 all single-motor power levels and natural safe stop");
    for (d=0;d<4;d++) {
        int left=d==1||d==2?-1:1, right=d==1||d==3?-1:1;
        reset(); sprintf(source,"klq.move_for(%u,0.010)\n",d); begin(source); step();
        CHECK(motor_count==2 && motors[0].port==0 && motors[1].port==1);
        CHECK(motors[0].direction==left && motors[1].direction==right && running[0] && running[1]);
        board_ms+=9; step(); CHECK(motor_count==2); ++board_ms; done(); CHECK(motor_count==4 && !running[0] && !running[1]);
    } GROUP("12/13/14/15 timed forward/back/left/right and paired stop");
    for (d=0;d<4;d++) {
        reset(); sprintf(source,"klq.move(%u)\nklq.wait(1)\nklq.move_stop()\n",d); begin(source); step(); step();
        CHECK(running[0] && running[1]); board_ms+=1000; step(); CHECK(!running[0] && !running[1]); done();
    } GROUP("16/17 continuous four-direction move and move_stop");
    for (l=1;l<=3;l++) {
        reset(); sprintf(source,"klq.move_power(%u)\nklq.move(0)\n",l); begin(source); done(); CHECK(motors[0].power==l && motors[1].power==l);
    } GROUP("18 all paired-motion power levels");
    reset(); supported[0]=false; begin("klq.motor_run(1,1)\n"); CHECK(finish(10)==MINI_ERROR); CHECK(mini_python_error()==MINI_ERR_UNSUPPORTED && stop_count==1);
    reset(); supported[1]=false; begin("klq.move(0)\n"); CHECK(finish(10)==MINI_ERROR); CHECK(motor_count==3 && !motors[2].run && motors[2].port==0 && !running[0] && stop_count==1);
    reset(); supported[0]=false; begin("klq.motor_stop(1)\nklq.move_stop()\n"); done(); CHECK(motor_count==3);
    reset(); begin("klq.motor_for(1,1,1)\n"); step(); mini_python_stop(); CHECK(!running[0] && stop_count==1);
    reset(); begin("klq.motor_run(1,1)\nklq.missing()\n"); CHECK(finish(10)==MINI_ERROR && !running[0] && stop_count==1);
    reset(); begin("klq.move_for(0,0)\n"); step(); CHECK(running[0] && running[1]); done(); CHECK(!running[0] && !running[1]); GROUP("motor unsupported, second-port rollback, error/stop safety and zero duration");
}

static void sensors(void)
{
    unsigned p,status,d; int sign; char source[120]; sc7a20_sample_t imu;
    for (p=0;p<3;p++) for (sign=-1;sign<=1;sign+=2) {
        reset(); sprintf(source,"klq.wait_distance(%u,%d,50)\nklq.display_number(7)\n",p+1,sign); begin(source); step();
        sample(p,1,500,0); step(); CHECK(display_count==0 && mini_python_status()==MINI_RUNNING);
        sample(p,1,sign<0?501:499,0); step(); CHECK(display_count==0);
        sample(p,1,sign<0?499:501,0); done(); CHECK(display_count==1);
    }
    reset(); for (status=0;status<=5;status++) {
        sample(0,status,100,0); CHECK(klq_runtime_distance(1,-1,50)==(status==1));
    }
    sample(0,1,100,80); CHECK(klq_runtime_distance(1,-1,50)); ++board_ms; CHECK(!klq_runtime_distance(1,-1,50));
    sample(0,1,100,0); ports[0].online=0; CHECK(!klq_runtime_distance(1,-1,50)); ports[0].online=1;
    ports[0].sensor_type=2; CHECK(!klq_runtime_distance(1,-1,50)); ports[0].sensor_type=1;
    ports[0].device_class=2; CHECK(!klq_runtime_distance(1,-1,50)); ports[0].device_class=1;
    board_ms=20; sample(0,1,100,40); CHECK(klq_runtime_distance(1,-1,50));
    CHECK(!klq_runtime_distance(0,-1,50) && !klq_runtime_distance(4,-1,50));
    CHECK(!klq_runtime_distance(1,0,50) && !klq_runtime_distance(1,-1,-1));
    reset(); begin("klq.wait_distance(1,-1,50)\n"); CHECK(finish(100)==MINI_RUNNING); mini_python_stop(); CHECK(mini_python_status()==MINI_DONE); GROUP("19 distance strict comparisons, all ports, status0..5, stale/offline/type and wrap");
    expect_error("klq.wait_ir(1,-1,50)\n",MINI_ERR_UNSUPPORTED,1);
    expect_error("klq.wait_ir(3,1,100)\n",MINI_ERR_UNSUPPORTED,1);
    expect_error("klq.wait_ir(0,1,50)\n",MINI_ERR_RANGE,1); GROUP("20 infrared unsupported path and argument contract");
    for (d=0;d<4;d++) {
        int value=(d==0||d==2)?-1500:1500;
        reset(); memset(&imu,0,sizeof(imu)); sprintf(source,"klq.wait_tilt(%u)\nklq.display_number(7)\n",d); begin(source); step();
        if (d<2) imu.pitch_cdeg=value+(value<0?1:-1); else imu.roll_cdeg=value+(value<0?1:-1);
        klq_runtime_update_imu(&imu,true); CHECK(step()==MINI_RUNNING && display_count==0);
        if (d<2) imu.pitch_cdeg=value; else imu.roll_cdeg=value;
        klq_runtime_update_imu(&imu,true); CHECK(klq_runtime_tilt(d));
        board_ms+=100; CHECK(klq_runtime_tilt(d)); ++board_ms; CHECK(!klq_runtime_tilt(d));
        klq_runtime_update_imu(&imu,true); done(); CHECK(display_count==1);
        klq_runtime_update_imu(NULL,false); CHECK(!klq_runtime_tilt(d));
    } GROUP("21 four tilt directions, 14.99/15.00deg, 100/101ms and invalid IMU");
    reset(); CHECK(klq_runtime_button(false)); begin("klq.wait_button(1)\nklq.display_number(7)\n"); step(); gpio=0; step();
    board_ms+=19; step(); CHECK(display_count==0 && !klq_runtime_button(true)); ++board_ms; done(); CHECK(display_count==1 && klq_runtime_button(true));
    begin("klq.wait_button(0)\nklq.display_number(8)\n"); step(); gpio=1; step(); board_ms+=19; step(); CHECK(display_count==1); ++board_ms; done(); CHECK(display_count==2);
    reset(); gpio=0; step(); board_ms+=10; gpio=1; step(); board_ms+=10; gpio=0; step(); board_ms+=19; step(); CHECK(!klq_runtime_button(true)); ++board_ms; step(); CHECK(klq_runtime_button(true));
    reset(); board_ms=0xfffffff0u; gpio=0; step(); board_ms=3; step(); CHECK(!klq_runtime_button(true)); board_ms=4; step(); CHECK(klq_runtime_button(true)); GROUP("22 PC3 press/release, 20ms debounce, bounce and uint32 wrap");
}

static void display(void)
{
    unsigned n; char source[100]; static const uint8_t pattern[13]={0,8,28,62,127,62,28,8,0,0,0,0,0};
    static const uint8_t num100[13]={0,0,62,0,0,62,34,62,0,62,34,62,0};
    for (n=0;n<10;n++) { reset(); sprintf(source,"klq.display_face(%u)\n",n); begin(source); done(); CHECK(display_count==1); } GROUP("23 all ten face indices with 7-bit columns");
    reset(); begin("klq.display_pattern('00081C3e7f3e1c080000000000')\n"); done(); CHECK(display_count==1 && !memcmp(picture,pattern,13)); GROUP("24 exact custom pattern, single quotes and mixed hex case");
    for (n=0;n<=100;n++) { reset(); sprintf(source,"klq.display_number(%u)\n",n); begin(source); done(); CHECK(display_count==1); }
    /* Centering of the widest supported value is a fixed, independent expected bitmap. */
    CHECK(!memcmp(picture,num100,13)); GROUP("25 all numbers 0..100 and exact centered 100 bitmap");
    reset(); begin("klq.display_face(1)\nklq.display_off()\n"); done(); CHECK(display_count==2); for(n=0;n<13;n++) CHECK(picture[n]==0); GROUP("26 display_off clears thirteen columns");
    reset(); display_ok=false; begin("klq.display_number(7)\n"); CHECK(finish(10)==MINI_ERROR && mini_python_error()==MINI_ERR_RUNTIME && stop_count==1); GROUP("display rejection exits with runtime error");
}

static void errors(void)
{
    unsigned i; char stack[600];
    struct { const char *p; mini_error_t e; unsigned line; } cases[]={
        {"import os\n",MINI_ERR_NAME,1},{"klq.missing()\n",MINI_ERR_NAME,1},
        {"klq.wait(-1)\n",MINI_ERR_ARGUMENT,1},{"klq.wait(3600.001)\n",MINI_ERR_ARGUMENT,1},
        {"klq.wait(0.0001)\n",MINI_ERR_ARGUMENT,1},{"klq.wait(2147484)\n",MINI_ERR_ARGUMENT,1},
        {"klq.wait()\n",MINI_ERR_ARGUMENT,1},{"klq.wait('1')\n",MINI_ERR_ARGUMENT,1},
        {"klq.display_number(101)\n",MINI_ERR_RANGE,1},{"klq.display_number(-1)\n",MINI_ERR_RANGE,1},
        {"klq.display_number(1.1)\n",MINI_ERR_RANGE,1},{"klq.display_face(10)\n",MINI_ERR_RANGE,1},
        {"klq.display_pattern('80' )\n",MINI_ERR_ARGUMENT,1},
        {"klq.display_pattern('80000000000000000000000000')\n",MINI_ERR_ARGUMENT,1},
        {"klq.display_pattern('gg000000000000000000000000')\n",MINI_ERR_ARGUMENT,1},
        {"klq.display_off(1)\n",MINI_ERR_ARGUMENT,1},{"klq.stop(1)\n",MINI_ERR_ARGUMENT,1},
        {"klq.motor_run(0,1)\n",MINI_ERR_RANGE,1},{"klq.motor_run(4,1)\n",MINI_ERR_RANGE,1},
        {"klq.motor_run(1,0)\n",MINI_ERR_RANGE,1},{"klq.motor_run(1)\n",MINI_ERR_ARGUMENT,1},
        {"klq.motor_for(1,1,-1)\n",MINI_ERR_ARGUMENT,1},{"klq.motor_for(1,1,3601)\n",MINI_ERR_ARGUMENT,1},
        {"klq.motor_power(0)\n",MINI_ERR_RANGE,1},{"klq.motor_power(4)\n",MINI_ERR_RANGE,1},
        {"klq.move_power(4)\n",MINI_ERR_RANGE,1},{"klq.move(4)\n",MINI_ERR_RANGE,1},
        {"klq.move_for(0,-1)\n",MINI_ERR_ARGUMENT,1},{"klq.move_stop(1)\n",MINI_ERR_ARGUMENT,1},
        {"klq.wait_distance(0,-1,10)\n",MINI_ERR_RANGE,1},{"klq.wait_distance(1,0,10)\n",MINI_ERR_RANGE,1},
        {"klq.wait_distance(1,-1,6554)\n",MINI_ERR_RANGE,1},{"klq.wait_tilt(4)\n",MINI_ERR_RANGE,1},
        {"klq.wait_button(2)\n",MINI_ERR_RANGE,1},{"klq.wait_ir(1,1,101)\n",MINI_ERR_RANGE,1},
        {"klq.wait(1,2,3,4)\n",MINI_ERR_ARGUMENT,1},{"klq.wait(1\n",MINI_ERR_SYNTAX,1},
        {"    pass\n",MINI_ERR_SYNTAX,1},{"\tpass\n",MINI_ERR_SYNTAX,1},
        {"for _ in range(1):\npass\n",MINI_ERR_SYNTAX,1},{"while True:\n",MINI_ERR_SYNTAX,1},
        {"for _ in range(-1):\n    pass\n",MINI_ERR_ARGUMENT,1},
        {"for _ in range(1.5):\n    pass\n",MINI_ERR_ARGUMENT,1},
        {"import klq\n# line\nklq.missing()\n",MINI_ERR_NAME,3}
    };
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);i++) expect_error(cases[i].p,cases[i].e,cases[i].line);
    stack[0]=0;
    for (i=0;i<9;i++) { unsigned j; for(j=0;j<i*4;j++) strcat(stack," "); strcat(stack,"for _ in range(1):\n"); }
    strcat(stack,"                                    pass\n"); expect_error(stack,MINI_ERR_STACK,9);
    reset(); mini_python_init(NULL,0); CHECK(!mini_python_start() && mini_python_status()==MINI_IDLE);
    reset(); begin("klq.display_number(7)\n"); done(); CHECK(mini_python_start()); done(); CHECK(display_count==2);
    GROUP("44 invalid programs, stack bound, source restart and empty source");
}
int main(void)
{
    control(); motor(); sensors(); display(); errors();
    printf("RESULT PASS groups=%u assertions=%u blocks=26/26\n",groups,assertions);
    return 0;
}
