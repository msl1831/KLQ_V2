#include "protocol.h"
#include "board.h"
#include "usb_serial.h"
#include <string.h>
#include <stdio.h>

#define IMAGE_MAGIC 0x4b4c5141u
typedef struct { uint32_t magic, length, crc, base, header_crc; } Image;
static uint8_t frame[16+MAX_PAYLOAD], response[20+MAX_PAYLOAD];
static uint32_t used, last_byte, image_size, image_crc, written;
static bool downloading, cached;
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
    calc=crc32_update(crc32_update(0,frame,12),p,n);
    if (wire_crc == calc && cached && seq==last_seq && wire_crc==last_crc && cmd==last_cmd && n==last_len) {
        usb_serial_write(response,response_len); return;
    }
    cached=false;
    if (wire_crc != calc) status=ERR_CRC;
    else switch (cmd) {
    case CMD_INFO:
        if (n) { status=ERR_LENGTH; break; }
        size = (uint32_t)sprintf((char *)out,
#ifdef KLQ_DEMO_APP
            "KLQ APP DEMO 0.1; "
#else
            "KLQ USB BOOT 0.1; "
#endif
            "APP=0x%08lX; MAX=%lu; PAGE=%u; VALID=%u; RECEIVED=%lu; USB_RESETS=%lu",
            (unsigned long)APP_BASE,(unsigned long)APP_MAX,(unsigned)FLASH_PAGE,(unsigned)image_valid(),
            (unsigned long)written,(unsigned long)usb_reset_count);
        break;
    case CMD_ECHO:
        memcpy(out,p,n); size=n; break;
    case CMD_DISPLAY:
        if (n != 13) status=ERR_LENGTH;
        else display_columns(p);
        break;
    case CMD_RESET:
        if (n) status=ERR_LENGTH;
        else action=2;
        break;
#ifndef KLQ_DEMO_APP
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
        if (status==OK) { downloading=true; display_icon(0); }
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
            status=ERR_IMAGE; display_icon(2); break;
        }
        meta.magic=IMAGE_MAGIC; meta.length=image_size; meta.crc=image_crc; meta.base=APP_BASE;
        meta.header_crc=crc32_update(0,&meta.length,12);
        /* Commit marker is the last write, after all other fields verify. */
        if (!program(META_BASE+4,((uint8_t *)&meta)+4,sizeof(meta)-4,true) ||
            !program(META_BASE,(uint8_t *)&meta,4,true) || !image_valid()) { status=ERR_FLASH; break; }
        display_icon(1); break;
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
    if (wire_crc==calc) { cached=true; last_seq=seq; last_crc=wire_crc; last_cmd=cmd; last_len=n; }
    if (usb_serial_write(response,response_len) && status==OK && action) {
        delay_ms(30);
        if (action==1) board_jump(APP_BASE);
        else { usb_disconnect(); NVIC_SystemReset(); }
    }
}

void protocol_poll(void)
{
    int b;
    if (usb_serial_reset_seen()) { used=0; cached=false; downloading=false; }
    if (used && board_ms-last_byte>2000u) used=0;
    while ((b=usb_serial_read()) >= 0) {
        frame[used++]=(uint8_t)b; last_byte=board_ms;
        if (used==4 && get32(frame)!=FRAME_MAGIC) { memmove(frame,frame+1,3); used=3; }
        if (used==16 && get16(frame+10)>MAX_PAYLOAD) { used=0; continue; }
        if (used>=16 && used==16u+get16(frame+10)) { process(); used=0; }
    }
}
