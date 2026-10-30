#include "protocol.h"
#include "board.h"
#include "usb_serial.h"
#include <string.h>
#include <stdio.h>
#ifdef KLQ_DEMO_APP
#include "external_ports.h"
#include "robot_ui.h"
#include "power_key.h"
#include "user_program.h"
#include "klq_runtime.h"
#include "voice_link.h"
#include "voice_control.h"
#endif

#ifdef KLQ_DEMO_APP
static uint8_t reset_pending;
static uint32_t reset_deadline;
uint8_t protocol_reset_pending(void) { return reset_pending; }
#else
uint8_t protocol_reset_pending(void) { return 0u; }
#endif

#define IMAGE_MAGIC 0x4b4c5141u
typedef struct { uint32_t magic, length, crc, base, header_crc; } Image;
static uint8_t frame[16+MAX_PAYLOAD], response[20+MAX_PAYLOAD];
static uint32_t used, last_byte;
static bool cached;
#ifndef KLQ_DEMO_APP
static uint32_t image_size, image_crc, written;
static bool downloading;
#endif
static uint32_t last_seq, last_crc, response_len;
static uint16_t last_cmd, last_len;

static uint32_t get32(const uint8_t *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint16_t get16(const uint8_t *p) { return p[0] | (p[1]<<8); }
static void put32(uint8_t *p,uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void put16(uint8_t *p,uint16_t v) { p[0]=v; p[1]=v>>8; }

/* Same reflected CRC-32 as Python zlib.crc32 (initial/final XOR included). */
uint32_t crc32_update(uint32_t crc, const void *data, uint32_t n)
{
    const uint8_t *p = data;
    unsigned bit;
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (bit=0; bit<8; ++bit) crc = (crc>>1) ^ (0xedb88320u & (0u-(crc&1)));
    }
    return ~crc;
}

static bool vector_valid(uint32_t n)
{
    uint32_t sp = *(const uint32_t *)APP_BASE;
    uint32_t pc = *(const uint32_t *)(APP_BASE+4);
    return n >= 8 && n <= APP_MAX && sp > 0x20000000u && sp <= 0x20020000u && !(sp&7) &&
           (pc&1) && (pc&~1u) >= APP_BASE+8 && (pc&~1u) < APP_BASE+n;
}
static bool image_valid(void)
{
    const Image *m = (const Image *)META_BASE;
    return m->magic == IMAGE_MAGIC && m->base == APP_BASE &&
           m->header_crc == crc32_update(0, &m->length, 12) && vector_valid(m->length) &&
           crc32_update(0, (const void *)APP_BASE, m->length) == m->crc;
}

bool protocol_system_firmware_valid(void) { return image_valid(); }

#ifndef KLQ_DEMO_APP
static bool erase_page(uint32_t address)
{
    bool ok;
    if (address < APP_BASE || address > META_BASE || address % FLASH_PAGE) return false;
    FMC_Unlock();
    FMC_ClearStatusFlag(FMC_FLAG_OC | FMC_FLAG_PE | FMC_FLAG_WPE);
    ok = FMC_ErasePage(address) == FMC_STATUS_COMPLETE;
    FMC_Lock();
    return ok;
}
static bool program(uint32_t address, const uint8_t *p, uint32_t n, bool metadata)
{
    uint32_t i, end = metadata ? META_BASE+sizeof(Image) : META_BASE;
    uint16_t value;
    bool ok = true;
    if ((address&1) || address < (metadata ? META_BASE : APP_BASE) || address > end || n > end-address) return false;
    FMC_Unlock();
    FMC_ClearStatusFlag(FMC_FLAG_OC | FMC_FLAG_PE | FMC_FLAG_WPE);
    for (i=0; i<n; i+=2) {
        value = p[i] | ((i+1<n ? p[i+1] : 0xffu)<<8);
        if (FMC_ProgramHalfWord(address+i, value) != FMC_STATUS_COMPLETE ||
            *(volatile uint16_t *)(address+i) != value) { ok=false; break; }
    }
    FMC_Lock();
    return ok;
}
#endif

static void process(void)
{
    uint32_t seq=get32(frame+4), wire_crc=get32(frame+12), calc, status=OK, size=0, action=0;
    uint16_t cmd=get16(frame+8), n=get16(frame+10);
    const uint8_t *p=frame+16;
    uint8_t *out=response+20;
    bool bridge_command=cmd>=CMD_EXT_OPEN && cmd<=CMD_EXT_CLOSE;
    calc=crc32_update(crc32_update(0,frame,12),p,n);
    if (!bridge_command && wire_crc == calc && cached && seq==last_seq && wire_crc==last_crc && cmd==last_cmd && n==last_len) {
        usb_serial_write(response,response_len); return;
    }
    cached=false;
    if (wire_crc != calc) status=ERR_CRC;
#ifdef KLQ_DEMO_APP
    else if (power_key_shutdown_pending() && cmd!=CMD_INFO && cmd!=CMD_ECHO && cmd!=CMD_STOP && cmd!=CMD_VOICE_STATUS && cmd!=CMD_MOTOR_STATUS && cmd!=CMD_VOICE_CANCEL) status=ERR_STATE;
    else if (external_upgrade_active() &&
             (cmd==CMD_BEGIN || cmd==CMD_DATA || cmd==CMD_END || cmd==CMD_RUN ||
              cmd==CMD_DISPLAY || cmd==CMD_FINISH)) status=ERR_STATE;
#endif
    else switch (cmd) {
    case CMD_INFO:
        if (n) { status=ERR_LENGTH; break; }
        size = (uint32_t)sprintf((char *)out,
#ifdef KLQ_DEMO_APP
            "KLQ ROBOT FW 0.13; APP=0x%08lX; MAX=%lu; PAGE=%u; VALID=%u; "
            "USER_STATE=%u; USER_VALID=%u; USER_LENGTH=%lu; USER_RECEIVED=%lu; USER_MAX=%u; UI=%u; "
            "PY_ERROR=%u; PY_LINE=%u; PK_RAW=%u; PK_DOWN=%u; PK_SHORT=%u; USB_RESETS=%lu; EXT_UPGRADE=1; EXT_PORTS=3; EXT_MOTOR=1; EXT_VOICE=1; EXT_MOTOR_STATUS=1",
            (unsigned long)APP_BASE,(unsigned long)APP_MAX,(unsigned)FLASH_PAGE,(unsigned)image_valid(),
            (unsigned)user_program_state(),(unsigned)user_program_valid(),
            (unsigned long)user_program_length(),(unsigned long)user_program_received(),
            (unsigned)USER_PROGRAM_CAPACITY,(unsigned)robot_ui_state(),
            (unsigned)user_program_error(),(unsigned)user_program_error_line(),
            (unsigned)power_key_raw_level(),(unsigned)power_key_pressed(),
            (unsigned)power_key_short_count(),(unsigned long)usb_reset_count);
#else
            "KLQ USB BOOT 0.4; APP=0x%08lX; MAX=%lu; PAGE=%u; VALID=%u; RECEIVED=%lu; USB_RESETS=%lu",
            (unsigned long)APP_BASE,(unsigned long)APP_MAX,(unsigned)FLASH_PAGE,(unsigned)image_valid(),
            (unsigned long)written,(unsigned long)usb_reset_count);
#endif
        break;
    case CMD_ECHO:
        memcpy(out,p,n); size=n; break;
    case CMD_DISPLAY:
        if (n != 13) status=ERR_LENGTH;
        else {
#ifdef KLQ_DEMO_APP
            if (!user_program_set_display(p)) status=ERR_STATE;
#else
            display_columns(p);
#endif
        }
        break;
    case CMD_RESET:
        if (n) status=ERR_LENGTH;
        else {
#ifdef KLQ_DEMO_APP
            voice_control_set_enabled(0u);
            user_program_stop();
            external_upgrade_cancel();
#endif
            board_request_bootloader();
            action=2;
        }
        break;
#ifdef KLQ_DEMO_APP
    case CMD_EXT_OPEN: {
        uint32_t token;
        uint8_t active;
        if (n!=9u) { status=ERR_LENGTH; break; }
        token=get32(p+1); active=external_upgrade_active();
        status=external_upgrade_open(p[0],token,get16(p+5),get16(p+7));
        if (status==OK) {
            if (!active) { voice_control_cancel(); user_program_stop(); }
            put32(out,token); size=4u;
        }
        break;
    }
    case CMD_EXT_XFER:
        if (n<20u || n>152u) status=ERR_LENGTH;
        else status=external_upgrade_submit(get32(p),p+4,(uint16_t)(n-4u));
        break;
    case CMD_EXT_RESULT: {
        uint16_t count=0u;
        if (n!=4u) { status=ERR_LENGTH; break; }
        status=external_upgrade_result(get32(p),out,&count);
        size=count;
        break;
    }
    case CMD_EXT_CLOSE:
        if (n!=4u) status=ERR_LENGTH;
        else status=external_upgrade_close(get32(p));
        break;
    case CMD_MOTOR_STATUS: {
        unsigned i; const klq_port_status_t *s;
        if (n) { status=ERR_LENGTH; break; }
        out[0]=1u;
        for(i=0u;i<KLQ_EXTERNAL_PORT_COUNT;i++) {
            s=external_port_status(i);
            out[1u+6u*i]=external_port_motor_state(i);
            out[2u+6u*i]=(uint8_t)(s && s->online);
            put16(out+3u+6u*i,(uint16_t)external_port_motor_goal_pwm(i));
            put16(out+5u+6u*i,(uint16_t)external_port_motor_ack_pwm(i));
        }
        size=19u; break;
    }
    case CMD_VOICE_STATUS: {
        voice_link_status_t v; voice_control_status_t c; unsigned i;
        if (n) { status=ERR_LENGTH; break; }
        voice_link_get_status(&v); voice_control_get_status(&c); memset(out,0,108u);
        out[0]=1u; out[1]=c.enabled; out[2]=v.version_valid; out[3]=v.awake;
        memcpy(out+4,v.version,3u); out[7]=v.play_state; out[8]=v.request;
        out[9]=v.last_ack; out[10]=v.last_error; out[11]=v.last_cmd; out[12]=v.last_seq;
        out[13]=c.active; out[14]=c.port; out[15]=c.last_reason;
        put32(out+16,v.last_semantic); put32(out+20,v.rx_frames); put32(out+24,v.bad_frames);
        put32(out+28,v.uart_errors); put32(out+32,v.rx_overflows); put32(out+36,v.tx_errors);
        put32(out+40,v.timeouts); put32(out+44,c.accepted); put32(out+48,c.rejected);
        put32(out+52,c.completed); put32(out+56,c.errors);
        for (i=0;i<5u;i++) put16(out+60u+2u*i,v.event_count[i]);
        put16(out+70,v.recognition_count); put16(out+72,v.duplicate_count); put16(out+74,v.dropped_intents);
        put32(out+76,v.play_id); put16(out+80,v.play_ack_count); put16(out+82,v.play_done_count);
        put32(out+84,v.rx_bytes); put32(out+88,v.tx_frames); put32(out+92,v.last_rx_ms);
        out[96]=c.last_score; out[97]=c.last_seq; out[98]=c.phase; put32(out+100,c.cancelled); put16(out+104,c.pending_sound); put16(out+106,(uint16_t)c.pwm); size=108u;
        break;
    }
    case CMD_VOICE_QUERY:
        if (n) status=ERR_LENGTH;
        else if (!voice_link_query_version()) status=ERR_STATE;
        break;
    case CMD_VOICE_PLAY:
        if (n!=4u) status=ERR_LENGTH;
        else if (get32(p)<5000u || get32(p)>5007u) status=ERR_RANGE;
        else if (!voice_link_play(get32(p))) status=ERR_STATE;
        break;
    case CMD_VOICE_ARM:
        if (n!=1u) status=ERR_LENGTH;
        else if (p[0]>1u) status=ERR_RANGE;
        else if (p[0] && (protocol_reset_pending() || power_key_shutdown_pending() || external_upgrade_active() ||
            (user_program_state()!=USER_PROGRAM_EMPTY && user_program_state()!=USER_PROGRAM_READY) ||
            robot_ui_state()!=ROBOT_UI_STANDBY)) status=ERR_STATE;
        else voice_control_set_enabled(p[0]);
        break;
    case CMD_VOICE_CANCEL:
        if (n) status=ERR_LENGTH; else voice_control_cancel();
        break;
    case CMD_BEGIN: {
        uint32_t requested;
        if (n != 8) { status=ERR_LENGTH; break; }
        requested=get32(p);
        if (!requested || requested>USER_PROGRAM_CAPACITY) { status=ERR_RANGE; break; }
        if (!user_program_begin(requested,get32(p+4))) status=ERR_STATE;
        break;
    }
    case CMD_DATA: {
        uint32_t offset, count;
        if (n<5) { status=ERR_LENGTH; break; }
        if (user_program_state()!=USER_PROGRAM_DOWNLOADING) { status=ERR_STATE; break; }
        offset=get32(p); count=n-4;
        if (!user_program_write(offset,p+4,count)) { status=ERR_RANGE; break; }
        put32(out,user_program_received()); size=4;
        break;
    }
    case CMD_END:
        if (n) status=ERR_LENGTH;
        else if (user_program_state()!=USER_PROGRAM_DOWNLOADING) status=ERR_STATE;
        else if (!user_program_finish_download()) status=ERR_IMAGE;
        break;
    case CMD_RUN:
        if (n) status=ERR_LENGTH;
        else if (!user_program_valid()) status=ERR_IMAGE;
        else if (!user_program_start()) status=ERR_STATE;
        break;
    case CMD_STOP:
        if (n) status=ERR_LENGTH;
        else {
            if (external_motor_control_active()) external_upgrade_cancel();
            user_program_stop();
        }
        break;
    case CMD_FINISH:
        if (n) status=ERR_LENGTH;
        else if (user_program_state()!=USER_PROGRAM_RUNNING) status=ERR_STATE;
        else user_program_finish_execution();
        break;
#else
    case CMD_BEGIN: {
        uint32_t a, requested;
        if (n!=8) { status=ERR_LENGTH; break; }
        requested=get32(p);
        if (requested<8 || requested>APP_MAX) { status=ERR_RANGE; break; }
        downloading=false;
        /* Invalidate FIRST. Interrupted replacement must never boot a mixed image. */
        if (!erase_page(META_BASE)) { status=ERR_FLASH; break; }
        image_size=requested; image_crc=get32(p+4); written=0;
        for (a=APP_BASE; a<APP_BASE+image_size; a+=FLASH_PAGE) {
            if (!erase_page(a)) { status=ERR_FLASH; break; }
        }
        if (status==OK) { downloading=true; display_icon(DISPLAY_ICON_DOWNLOAD); }
        break;
    }
    case CMD_DATA: {
        uint32_t offset, count;
        if (n<5) { status=ERR_LENGTH; break; }
        if (!downloading) { status=ERR_STATE; break; }
        offset=get32(p); count=n-4;
        if (offset!=written || (offset&1) || count>image_size-written ||
            ((count&1) && count!=image_size-written)) { status=ERR_RANGE; break; }
        if (!program(APP_BASE+offset,p+4,count,false)) { status=ERR_FLASH; downloading=false; break; }
        written+=count; put32(out,written); size=4;
        break;
    }
    case CMD_END: {
        Image meta;
        if (n) { status=ERR_LENGTH; break; }
        if (!downloading || written!=image_size) { status=ERR_STATE; break; }
        downloading=false;
        if (!vector_valid(image_size) || crc32_update(0,(const void *)APP_BASE,image_size)!=image_crc) {
            status=ERR_IMAGE; display_icon(DISPLAY_ICON_ERROR); break;
        }
        meta.magic=IMAGE_MAGIC; meta.length=image_size; meta.crc=image_crc; meta.base=APP_BASE;
        meta.header_crc=crc32_update(0,&meta.length,12);
        /* Commit marker is the last write, after all other fields verify. */
        if (!program(META_BASE+4,((uint8_t *)&meta)+4,sizeof(meta)-4,true) ||
            !program(META_BASE,(uint8_t *)&meta,4,true) || !image_valid()) { status=ERR_FLASH; break; }
        display_icon(DISPLAY_ICON_COMPLETE); break;
    }
    case CMD_RUN:
        if (n) status=ERR_LENGTH;
        else if (downloading || !image_valid()) status=ERR_IMAGE;
        else action=1;
        break;
#endif
    default: status=ERR_COMMAND; break;
    }
    put32(response,FRAME_MAGIC); put32(response+4,seq);
    put16(response+8,cmd|0x8000u); put16(response+10,(uint16_t)(size+4));
    put32(response+16,status);
    put32(response+12,crc32_update(crc32_update(0,response,12),response+16,size+4));
    response_len=size+20;
    if (!bridge_command && wire_crc==calc) { cached=true; last_seq=seq; last_crc=wire_crc; last_cmd=cmd; last_len=n; }
    if (usb_serial_write(response,response_len) && status==OK && action) {
#ifdef KLQ_DEMO_APP
        reset_pending=1u; reset_deadline=board_ms+150u;
        if (external_ports_motor_stopped()) { delay_ms(30); usb_disconnect(); NVIC_SystemReset(); reset_pending=0u; }
#else
        delay_ms(30);
        if (action==1) board_jump(APP_BASE);
        else { usb_disconnect(); NVIC_SystemReset(); }
#endif
    }
}

static void reset_received(void)
{
    used=0; cached=false;
#ifdef KLQ_DEMO_APP
    voice_control_cancel();
    external_upgrade_cancel();
    if (user_program_state()==USER_PROGRAM_DOWNLOADING) user_program_stop();
#else
    downloading=false;
#endif
}

void protocol_poll(void)
{
    int b;
    uint16_t budget=256u;
#ifdef KLQ_DEMO_APP
    if (reset_pending) {
        if (external_ports_motor_stopped() || (int32_t)(board_ms-reset_deadline)>=0) { usb_disconnect(); NVIC_SystemReset(); reset_pending=0u; }
        return;
    }
#endif
    if (usb_serial_reset_seen()) reset_received();
    if (used && board_ms-last_byte>2000u) used=0;
    while (budget--) {
        if (usb_serial_reset_seen()) reset_received();
        b=usb_serial_read();
        if (b<0) break;
        frame[used++]=(uint8_t)b; last_byte=board_ms;
        if (used==4 && get32(frame)!=FRAME_MAGIC) { memmove(frame,frame+1,3); used=3; }
        if (used==16 && get16(frame+10)>MAX_PAYLOAD) { used=0; continue; }
        if (used>=16 && used==16u+get16(frame+10)) { process(); used=0;
#ifdef KLQ_DEMO_APP
            if (reset_pending) break;
#endif
        }
    }
}
