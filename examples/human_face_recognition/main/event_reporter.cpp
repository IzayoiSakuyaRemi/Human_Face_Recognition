/**
 * @file event_reporter.cpp
 * @brief Forwards P4 events to S3 via UART (pipe-delimited, S3 relays to HTTP).
 *
 * P4 has Ethernet but target server is on Wi-Fi subnet. S3 handles HTTP POST.
 * Format over UART: uart_bridge_send wraps as {"dev":"p4","msg":"EVENT|<type>|<k=v,...>"}
 */
#include "event_reporter.hpp"
#include "uart_bridge.hpp"
#include <cstdio>

void report_event(const char *event, const char *json_fields)
{
    char buf[240];
    snprintf(buf, sizeof(buf), "EVENT|%s|%s", event, json_fields);
    uart_bridge_send(buf);
}
