/**
 * @file xiaozhi_relay.c
 * @brief XiaoZhi AI voice relay — bridges UART Opus/PCM ↔ MQTT Cloud.
 *
 * Uses ESP-IDF native esp_mqtt_client. No xiaozhi C++ dependency.
 *
 * Protocol:
 *   P4 ──UART(type 0x02)──▶ Opus/PCM frames ──▶ MQTT publish
 *   P4 ◀──UART(type 0x03)── Opus/PCM frames ◀── MQTT subscribe
 *   P4 ◀──UART(type 0x01)── JSON messages   ◀── MQTT subscribe
 */

#include "xiaozhi_relay.h"
#include "xiaozhi/uart_frame_protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_http_client.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "driver/uart.h"
#include "esp_radar.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "xz_relay";

/* ── OTA / server endpoints ─────────────────── */
#define XZ_OTA_URL  "https://api.tenclass.net/xiaozhi/ota/"

/* ── MQTT state ─────────────────────────────── */
static esp_mqtt_client_handle_t g_mqtt = NULL;
static bool g_connected = false;
static bool g_active = false;
static char g_pub_topic[128] = {0};
static char g_sub_topic[128] = {0};
static char g_client_id[64]  = {0};
static uint8_t g_mac[6]      = {0};

/* ── Forward declarations ───────────────────── */
static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data);
static bool xz_ota_fetch_config(void);
static void xz_send_hello(void);
static void xz_handle_server_json(const char *json);

/* ══════════════════════════════════════════════
 *  OTA: fetch MQTT broker config from server
 * ══════════════════════════════════════════════ */
static bool xz_ota_fetch_config(void)
{
    ESP_LOGI(TAG, "Fetching OTA config from %s", XZ_OTA_URL);

    esp_http_client_config_t http_cfg = {
        .url = XZ_OTA_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t http = esp_http_client_init(&http_cfg);

    char ua[128]; snprintf(ua, sizeof(ua), "xiaozhi-esp32/2.2.4 (ESP32-S3)");
    esp_http_client_set_header(http, "User-Agent", ua);
    esp_http_client_set_header(http, "Device-Id", "a4cb8fda47cc"); // TODO: real MAC

    esp_err_t err = esp_http_client_perform(http);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA HTTP failed: %d", err);
        esp_http_client_cleanup(http);
        return false;
    }

    int status = esp_http_client_get_status_code(http);
    ESP_LOGI(TAG, "OTA response: %d", status);

    /* Parse JSON response for MQTT config */
    char *body = NULL;
    int body_len = esp_http_client_get_content_length(http);
    if (body_len > 0 && body_len < 8192) {
        body = malloc(body_len + 1);
        if (body) {
            int read = esp_http_client_read(http, body, body_len);
            if (read > 0) {
                body[read] = 0;
                ESP_LOGI(TAG, "OTA body: %s", body);

                cJSON *root = cJSON_Parse(body);
                if (root) {
                    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
                    if (mqtt) {
                        cJSON *endpoint = cJSON_GetObjectItem(mqtt, "endpoint");
                        cJSON *pub_t    = cJSON_GetObjectItem(mqtt, "publish_topic");
                        cJSON *sub_t    = cJSON_GetObjectItem(mqtt, "subscribe_topic");
                        cJSON *cid      = cJSON_GetObjectItem(mqtt, "client_id");

                        if (endpoint && cid && pub_t && sub_t) {
                            /* Store MQTT URI for later connection */
                            /* We'll parse the URI in xz_mqtt_connect */
                            extern char g_mqtt_broker_uri[256];
                            snprintf(g_mqtt_broker_uri, 256, "%s", endpoint->valuestring);
                            snprintf(g_client_id, sizeof(g_client_id), "%s", cid->valuestring);
                            snprintf(g_pub_topic, sizeof(g_pub_topic), "%s", pub_t->valuestring);
                            snprintf(g_sub_topic, sizeof(g_sub_topic), "%s", sub_t->valuestring);
                            ESP_LOGI(TAG, "MQTT config: broker=%s client=%s",
                                     g_mqtt_broker_uri, g_client_id);
                            cJSON_Delete(root);
                            free(body);
                            esp_http_client_cleanup(http);
                            return true;
                        }
                    }
                    cJSON_Delete(root);
                }
            }
            free(body);
        }
    }

    esp_http_client_cleanup(http);
    ESP_LOGW(TAG, "OTA config parse failed, using default");
    return false;
}

char g_mqtt_broker_uri[256] = {0};

/* ══════════════════════════════════════════════
 *  MQTT event handler
 * ══════════════════════════════════════════════ */
static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        g_connected = true;
        xz_send_hello();
        /* Subscribe to downlink topic */
        if (g_sub_topic[0]) {
            esp_mqtt_client_subscribe(g_mqtt, g_sub_topic, 1);
            ESP_LOGI(TAG, "Subscribed to %s", g_sub_topic);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        g_connected = false;
        break;

    case MQTT_EVENT_DATA: {
        /* Received data from server */
        if (ev->data_len >= 4 && ev->data[0] == 0x00 && ev->data[1] == 0x01) {
            /* Binary protocol v2: Opus audio from server */
            /* Send as type 0x03 frame to P4 */
            uint8_t fbuf[2048];
            uint16_t payload_sz = (uint16_t)ev->data[12] | ((uint16_t)ev->data[13] << 8);
            /* For now, forward the raw Opus payload */
            if (payload_sz > 0 && payload_sz < 2000) {
                size_t flen = uart_frame_build_pcm(
                    fbuf, sizeof(fbuf), UART_FRAME_PCM_DOWN, 0, 0,
                    (const int16_t *)&ev->data[14], payload_sz / 2);
                if (flen > 0)
                    uart_write_bytes(UART_NUM_1, (const char *)fbuf, flen);
            }
        } else if (ev->data_len > 0 && ev->data[0] == '{') {
            /* JSON message from server */
            char *js = strndup(ev->data, ev->data_len < 1024 ? ev->data_len : 1023);
            if (js) {
                ESP_LOGI(TAG, "Server JSON: %s", js);
                xz_handle_server_json(js);
                /* Forward JSON to P4 via UART */
                uart_write_bytes(UART_NUM_1, js, strlen(js));
                uart_write_bytes(UART_NUM_1, "\n", 1);
                free(js);
            }
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

/* ══════════════════════════════════════════════
 *  Send hello message to xiaozhi server
 * ══════════════════════════════════════════════ */
static void xz_send_hello(void)
{
    esp_read_mac(g_mac, ESP_MAC_WIFI_STA);
    char js[512];
    snprintf(js, sizeof(js),
        "{\"type\":\"hello\","
        "\"version\":\"2.2.4\","
        "\"transport\":\"mqtt\","
        "\"features\":{\"aec\":false,\"mcp\":true},"
        "\"device_id\":\"%02x%02x%02x%02x%02x%02x\","
        "\"client_id\":\"%s\"}",
        g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5],
        g_client_id);

    ESP_LOGI(TAG, "Sending hello: %s", js);
    esp_mqtt_client_publish(g_mqtt, g_pub_topic, js, 0, 1, 0);
}

/* ══════════════════════════════════════════════
 *  Handle server JSON messages
 * ══════════════════════════════════════════════ */
static void xz_handle_server_json(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (type && cJSON_IsString(type)) {
        if (strcmp(type->valuestring, "hello") == 0) {
            /* Server hello — may contain MQTT/UDP config update */
            cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
            if (mqtt) {
                cJSON *pt = cJSON_GetObjectItem(mqtt, "publish_topic");
                cJSON *st = cJSON_GetObjectItem(mqtt, "subscribe_topic");
                if (pt) snprintf(g_pub_topic, sizeof(g_pub_topic), "%s", pt->valuestring);
                if (st) {
                    snprintf(g_sub_topic, sizeof(g_sub_topic), "%s", st->valuestring);
                    esp_mqtt_client_subscribe(g_mqtt, g_sub_topic, 1);
                }
            }
            cJSON *session = cJSON_GetObjectItem(root, "session_id");
            if (session) ESP_LOGI(TAG, "Session: %s", session->valuestring);
        }
        /* Other types (tts, stt, llm, mcp, system) forwarded to P4 as-is */
    }

    cJSON_Delete(root);
}

/* ══════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════ */

bool xiaozhi_relay_start(void)
{
    if (g_active) return true;

    ESP_LOGI(TAG, "Starting xiaozhi relay...");

    /* Step 1: Get MQTT config from OTA server */
    if (!xz_ota_fetch_config()) {
        /* Fallback: use hardcoded default */
        snprintf(g_mqtt_broker_uri, 256, "mqtts://mqtt.tenclass.net:8883");
        snprintf(g_client_id, 64, "xiaozhi-s3-%02x%02x%02x",
                 (unsigned)(esp_random() & 0xFF),
                 (unsigned)(esp_random() & 0xFF),
                 (unsigned)(esp_random() & 0xFF));
        snprintf(g_pub_topic, 128, "xiaozhi/up/%s", g_client_id);
        snprintf(g_sub_topic, 128, "xiaozhi/down/%s", g_client_id);
    }

    /* Step 2: Connect MQTT */
    /* Use non-TLS mqtt:// for local testing. For production, use mqtts:// with cert bundle. */
    char mqtt_uri[256];
    if (strncmp(g_mqtt_broker_uri, "mqtts://", 8) == 0) {
        snprintf(mqtt_uri, sizeof(mqtt_uri), "mqtt://%s", g_mqtt_broker_uri + 8);
    } else {
        snprintf(mqtt_uri, sizeof(mqtt_uri), "%s", g_mqtt_broker_uri);
    }
    ESP_LOGI(TAG, "MQTT connecting to: %s", mqtt_uri);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqtt_uri,
        .credentials.client_id = g_client_id,
    };
    g_mqtt = esp_mqtt_client_init(&mqtt_cfg);
    if (!g_mqtt) {
        ESP_LOGE(TAG, "MQTT init failed");
        return false;
    }

    esp_mqtt_client_register_event(g_mqtt, ESP_EVENT_ANY_ID,
                                    mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(g_mqtt);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT start failed: %d", err);
        return false;
    }

    g_active = true;
    ESP_LOGI(TAG, "Xiaozhi relay started");
    return true;
}

void xiaozhi_relay_stop(void)
{
    if (!g_active) return;

    ESP_LOGI(TAG, "Stopping xiaozhi relay...");
    if (g_mqtt) {
        esp_mqtt_client_stop(g_mqtt);
        esp_mqtt_client_destroy(g_mqtt);
        g_mqtt = NULL;
    }
    g_connected = false;
    g_active = false;
    ESP_LOGI(TAG, "Xiaozhi relay stopped");
}

void xiaozhi_relay_on_opus_from_p4(const uint8_t *opus_data, uint16_t len)
{
    if (!g_active || !g_connected) return;

    /* Build binary protocol v3 frame and publish to MQTT */
    /* xiaozhi uses BinaryProtocol3: type(1) + reserved(1) + payload_size(2) + payload */
    uint8_t bp3[2048];
    bp3[0] = 0x00;  /* type: OPUS */
    bp3[1] = 0x00;  /* reserved */
    bp3[2] = (uint8_t)(len & 0xFF);
    bp3[3] = (uint8_t)((len >> 8) & 0xFF);
    if (len <= 2048 - 4) {
        memcpy(bp3 + 4, opus_data, len);
        esp_mqtt_client_publish(g_mqtt, g_pub_topic,
                                (const char *)bp3, len + 4, 1, 0);
    }
}

void xiaozhi_relay_on_ctrl_from_p4(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    ESP_LOGI(TAG, "Control from P4: cmd=%d len=%d", cmd, len);

    switch (cmd) {
    case CTRL_ENTER_XIAOZHI:
        /* Suppress CSI warnings during xiaozhi mode */
        esp_log_level_set("esp_radar_csi_rx_cb", ESP_LOG_ERROR);
        esp_radar_stop();
        xiaozhi_relay_start();
        {
            uint8_t ack[4] = {UART_FRAME_CTRL, CTRL_XIAOZHI_READY, 0, 0};
            uart_write_bytes(UART_NUM_1, (const char *)ack, 4);
        }
        break;

    case CTRL_EXIT_XIAOZHI:
        xiaozhi_relay_stop();
        esp_log_level_set("esp_radar_csi_rx_cb", ESP_LOG_WARN);
        esp_radar_start();
        {
            uint8_t ack[4] = {UART_FRAME_CTRL, CTRL_GUARD_READY, 0, 0};
            uart_write_bytes(UART_NUM_1, (const char *)ack, 4);
        }
        break;

    default:
        break;
    }
}

void xiaozhi_relay_on_json_from_p4(const char *json)
{
    if (!g_active || !g_connected) return;
    /* Forward JSON text to MQTT */
    esp_mqtt_client_publish(g_mqtt, g_pub_topic, json, 0, 0, 0);
}

bool xiaozhi_relay_is_active(void)
{
    return g_active;
}
