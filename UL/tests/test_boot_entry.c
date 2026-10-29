#define main shared_boot_entry_checks
#include "../../sensor_boot/tests/test_app_entry.c"
#undef main

int main(void)
{
    frame_parser_t parser;
    klq_frame_t f;
    uint8_t expected[sizeof(sent)];

    if (shared_boot_entry_checks()) return 1;
    frame_parser_init(&parser);
    device_service_init(&parser);
    memset(&f, 0, sizeof(f));
    resets = 0U; accept = 1U; idle = 0U; now = 100U;
    f.command = KLQ_CMD_ENTER_BOOTLOADER; f.sequence = 20U;
    device_service_handle_frame(&f);
    assert(get32(sent + 16U) == 0U);
    memcpy(expected, sent, sizeof(sent));
    f.command = KLQ_CMD_INFO; f.sequence = 21U; f.payload_length = 1U;
    device_service_handle_frame(&f);
    if (memcmp(expected, sent, sizeof(sent))) {
        puts("FAIL: malformed request appended response during pending reset");
        return 1;
    }
    f.command = KLQ_CMD_ECHO; f.payload_length = 0U;
    device_service_handle_frame(&f);
    assert(!memcmp(expected, sent, sizeof(sent)));
    device_service_poll(102U); assert(!resets);
    idle = 1U; device_service_poll(102U); assert(resets == 1U);
    puts("UL pending reset suppresses valid and malformed requests PASS");
    return 0;
}
