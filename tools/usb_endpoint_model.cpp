/* Host-only semantic model, not a target driver or USB timing simulation.
 * Geehy APM32E103xCxE User Manual V1.6, 26.4.2.1, printed pp400-401:
 * https://global.geehy.com/uploads/tool/APM32E103xCxE%C2%A0User%C2%A0Manual%C2%A0V1.6.pdf
 * CTFR/CTFT: RC_W0; STS/DTOG: write-one-toggle; ADDR/TYPE/KIND: R/W.
 * The runner extracts the real driver functions and bit definitions verbatim.
 * EPRegister injects hardware flags after the driver's read, before its write.
 * No device, interrupt timing, Windows CDC cancellation or timeout cause is proved.
 */
#include <stdio.h>
#include <stdlib.h>
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef char require_u32[sizeof(uint32_t)==4 ? 1 : -1];
typedef uint32_t USBD_REG_EP_TYPE_T;
typedef uint32_t USBD_EP_STATUS_T;
#define __IOM
/* DRIVER_BITS */

/* Independent register semantics use the manual bit positions, not driver masks. */
enum {RC0=0x8080, TOGGLE=0x7070, NORMAL=0x070F, READONLY=0x0800};
static uint32_t pending, reads, writes;
struct EPRegister {
    uint32_t value;
    operator uint32_t() const { ++reads; return value; }
    EPRegister& operator=(uint32_t word) {
        uint32_t live=value|pending;
        ++writes; pending=0;
        value=(live&word&RC0)|((live^word)&TOGGLE)|(word&NORMAL)|(live&READONLY);
        return *this;
    }
};
struct ModelEndpoint { EPRegister EP; };
struct USBDevice { ModelEndpoint EP[8]; };
static USBDevice model_device;
#define USBD (&model_device)

/* DRIVER_FUNCTIONS */

static const char *names[]={
    "SetEPType","SetEPKind","ResetEPKind","ResetEPRxFlag","ResetEPTxFlag",
    "ToggleTx","ToggleRx","SetEpAddr","SetEPTxStatus","SetEPRxStatus","SetEPTxRxStatus"
};
static const uint32_t nargs[]={4,1,1,1,1,1,1,16,4,4,16};
static void call_driver(unsigned op,uint8_t ep,uint32_t arg) {
    switch(op) {
    case 0: USBD_SetEPType(ep,arg);break;
    case 1: USBD_SetEPKind(ep);break;
    case 2: USBD_ResetEPKind(ep);break;
    case 3: USBD_ResetEPRxFlag(ep);break;
    case 4: USBD_ResetEPTxFlag(ep);break;
    case 5: USBD_ToggleTx(ep);break;
    case 6: USBD_ToggleRx(ep);break;
    case 7: USBD_SetEpAddr(ep,(uint8_t)arg);break;
    case 8: USBD_SetEPTxStatus(ep,arg);break;
    case 9: USBD_SetEPRxStatus(ep,arg);break;
    case 10: USBD_SetEPTxRxStatus(ep,arg&3,arg>>2);break;
    }
}
static uint32_t expected(uint32_t initial,uint32_t inject,unsigned op,uint32_t arg) {
    uint32_t v=initial|inject;
    switch(op) {
    case 0: return (v&~0x0600u)|(arg<<9);
    case 1: return v|0x0100;
    case 2: return v&~0x0100u;
    case 3: return v&~0x8000u;
    case 4: return v&~0x0080u;
    case 5: return v^0x0040;
    case 6: return v^0x4000;
    case 7: return (v&~0x000Fu)|arg;
    case 8: return (v&~0x0030u)|(arg<<4);
    case 9: return (v&~0x3000u)|(arg<<12);
    case 10: return (v&~0x3030u)|((arg&3)<<4)|((arg>>2)<<12);
    }
    abort();return 0;
}
int main(void) {
    const uint8_t endpoints[]={0,1,7};
    unsigned op,ei,inject_index,first=0;
    uint32_t initial,arg,inject,want,got,cases=0,failures=0;
    uint32_t bad[11]={0};
    /* Every 16-bit starting value covers all flags, status/toggle bits, R/W and RO.
     * Control EP0, application EP1 and boundary EP7; every legal operation argument.
     */
    for(op=0;op<11;++op)for(ei=0;ei<3;++ei)
    for(initial=0;initial<65536;++initial)for(inject_index=0;inject_index<4;++inject_index)
    for(arg=0;arg<nargs[op];++arg) {
        uint8_t ep=endpoints[ei];
        inject=(inject_index&1 ? 0x8000u : 0)|(inject_index&2 ? 0x0080u : 0);
        USBD->EP[ep].EP.value=initial;
        pending=inject;reads=writes=0;
        call_driver(op,ep,arg);
        got=USBD->EP[ep].EP.value;want=expected(initial,inject,op,arg);
        ++cases;
        if(got!=want || reads!=1 || writes!=1 || pending) {
            ++bad[op];++failures;
            if(first<12) {
                printf("FAIL %s ep=%u before=%04X inject=%04X arg=%X got=%04X want=%04X r=%u w=%u\n",
                    names[op],(unsigned)ep,initial,inject,arg,got,want,reads,writes);
                ++first;
            }
        }
    }
    for(op=0;op<11;++op)printf("OP %s failures=%u\n",names[op],bad[op]);
    printf("RESULT cases=%u failures=%u\n",cases,failures);
    return failures ? 1 : 0;
}
