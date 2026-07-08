/**
 * @file event_reporter.cpp
 * @brief Forwards P4 events to S3 as raw UART JSON lines (no wrapping).
 *
 * S3 detects {"dev":"p4-voice"...} lines and relays to HTTP /api/event.
 * Server expects: {"device":"p4-voice","event":"voice_command",...}
 */
#include "event_reporter.hpp"
#include "driver/uart.h"
#include <stdio.h>

#define UART_PORT UART_NUM_1

void report_event(const char *event, const char *json_fields)
{
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "{\"dev\":\"p4-voice\",\"event\":\"%s\",%s}\n",
        event, json_fields);
    if (len > 0 && len < (int)sizeof(buf)) {
        uart_write_bytes(UART_PORT, buf, len);
    }
}
