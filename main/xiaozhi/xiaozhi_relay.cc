/**
 * @file xiaozhi_relay.cc
 * @brief XiaoZhi AI voice relay — MqttProtocol + AudioService + UartAudioCodec.
 *
 * Architecture (refactored):
 *   P4 Mic → UART PCM_UP → UartAudioCodec::Read()
 *          → AudioService (Opus Encoder) → send_queue
 *          → MqttProtocol::SendAudio() → Cloud
 *
 *   Cloud → MqttProtocol::OnIncomingAudio → decode_queue
 *         → AudioService (Opus Decoder) → playback_queue
 *         → UartAudioCodec::Write() → UART PCM_DOWN → P4 Speaker
 */

#include "xiaozhi_relay.h"
#include "xiaozhi/uart_frame_protocol.h"
#include "xiaozhi/ota.h"
#include "xiaozhi/settings.h"
#include "xiaozhi/board.h"
#include "xiaozhi/protocols/mqtt_protocol.h"
#include "xiaozhi/audio/audio_service.h"
#include "xiaozhi/audio/codecs/uart_audio_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "driver/uart.h"
#include "esp_radar.h"
#include <string>
#include <memory>
#include <string.h>
#include <stdio.h>

static const char *TAG = "xz_relay";
static bool g_active = false;

/* ── Forward xiaozhi status to P4 via UART JSON ── */
static void xz_send_to_p4(const char *xz_type, const char *key, const char *value)
{
    if (!g_active || !xz_type || !key || !value) return;
    char buf[512];
    int len = snprintf(buf, sizeof(buf),
        "{\"dev\":\"s3\",\"xz\":{\"type\":\"%s\",\"%s\":\"%s\"}}\n",
        xz_type, key, value);
    if (len > 0 && len < (int)sizeof(buf)) {
        int sent = uart_write_bytes(UART_NUM_1, buf, len);
        if (sent != len) {
            ESP_LOGW(TAG, "xz_send_to_p4: short write %d/%d", sent, len);
        }
    }
}

/* ── Protocol stack ───────────────────────────── */
static std::unique_ptr<MqttProtocol> g_protocol;
static std::unique_ptr<AudioService>  g_audio_service;
static bool g_mic_open  = false;

/* ── Forward ──────────────────────────────────── */
static bool xz_ota_fetch_config(void);

/* ══════════════════════════════════════════════
 *  OTA: fetch MQTT broker config from server
 * ══════════════════════════════════════════════ */
static bool xz_ota_fetch_config(void)
{
    ESP_LOGI(TAG, "Fetching OTA config via xiaozhi Ota class...");

    Ota ota;
    esp_err_t err = ota.CheckVersion();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ota::CheckVersion failed: %d", err);
        return false;
    }

    // Handle activation if needed (user must enter code on xiaozhi.me)
    if (ota.HasActivationCode()) {
        ESP_LOGW(TAG, "========================================");
        ESP_LOGW(TAG, "ACTIVATION CODE: %s", ota.GetActivationCode().c_str());
        ESP_LOGW(TAG, "%s", ota.GetActivationMessage().c_str());
        ESP_LOGW(TAG, "Go to xiaozhi.me and enter this code!");
        ESP_LOGW(TAG, "========================================");
        // Poll Activate() until user enters code on website
        for (int i = 0; i < 120; ++i) {  // ~6 minutes timeout
            err = ota.Activate();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Activation successful!");
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));  // 3s between polls
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));  // 10s on error
            }
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Activation timeout, continuing with test identity");
        } else {
            // Re-fetch config after activation — server now returns real MQTT identity
            ESP_LOGI(TAG, "Re-fetching OTA config after activation...");
            err = ota.CheckVersion();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Re-CheckVersion failed: %d", err);
            }
        }
    }

    // Ota::CheckVersion() already saved MQTT config to Settings("mqtt") via NVS.
    // Verify it was actually saved.
    if (ota.HasMqttConfig()) {
        Settings mqtt("mqtt", false);
        std::string endpoint  = mqtt.GetString("endpoint");
        std::string client_id = mqtt.GetString("client_id");
        if (!endpoint.empty() && !client_id.empty()) {
            ESP_LOGI(TAG, "MQTT from OTA: %s client=%s", endpoint.c_str(), client_id.c_str());
            return true;
        }
    }

    if (ota.HasWebsocketConfig()) {
        ESP_LOGW(TAG, "Server returned websocket config — not supported yet");
    }

    ESP_LOGW(TAG, "No MQTT config in OTA response");
    return false;
}

/* ══════════════════════════════════════════════
 *  xiaozhi_relay_start — using MqttProtocol + AudioService
 * ══════════════════════════════════════════════ */
bool xiaozhi_relay_start(void)
{
    if (g_active) return true;

    ESP_LOGI(TAG, "Starting xiaozhi relay (protocol stack mode)...");

    /* ── Step 1: Fetch MQTT broker config via OTA ── */
    if (!xz_ota_fetch_config()) {
        /* Fallback: write default MQTT config to Settings so MqttProtocol can read it */
        Settings mqtt("mqtt", true);
        if (mqtt.GetString("endpoint").empty()) {
            mqtt.SetString("endpoint", "mqtt.tenclass.net:8883");
            char cid[64]; snprintf(cid, sizeof(cid), "xiaozhi-s3-%02x%02x%02x",
                (unsigned)(esp_random() & 0xFF),
                (unsigned)(esp_random() & 0xFF),
                (unsigned)(esp_random() & 0xFF));
            mqtt.SetString("client_id", cid);
            mqtt.SetString("publish_topic", std::string("xiaozhi/up/") + cid);
            ESP_LOGI(TAG, "Using fallback MQTT config: endpoint=mqtts://mqtt.tenclass.net:8883 client=%s", cid);
        }
    }

    /* ── Step 2: Create UartAudioCodec + AudioService ── */
    auto *codec = new UartAudioCodec();
    g_uart_audio_codec = codec;
    g_uart_codec       = codec;  // also set board's global codec pointer

    g_audio_service = std::make_unique<AudioService>();
    g_audio_service->SetModelsList(nullptr);  // S3 has no SR models — no wake word
    g_audio_service->Initialize(codec);

    /* ── Step 3: Create MqttProtocol ── */
    g_protocol = std::make_unique<MqttProtocol>();

    /* ── Step 4: Wire AudioService → Protocol (uplink) ── */
    AudioServiceCallbacks cbs;
    cbs.on_send_queue_available = []() {
        if (!g_protocol || !g_protocol->IsAudioChannelOpened()) return;
        while (auto packet = g_audio_service->PopPacketFromSendQueue()) {
            if (!g_protocol->SendAudio(std::move(packet))) break;
        }
    };
    // No wake word or VAD callbacks needed on S3
    g_audio_service->SetCallbacks(cbs);

    /* ── Step 5: Wire Protocol → AudioService (downlink) ── */
    g_protocol->OnIncomingAudio([](std::unique_ptr<AudioStreamPacket> packet) {
        if (g_audio_service) {
            g_audio_service->PushPacketToDecodeQueue(std::move(packet));
        }
    });

    g_protocol->OnAudioChannelOpened([]() {
        ESP_LOGI(TAG, "Audio channel opened — starting codec + AudioService");
        // Flush UART backlog accumulated during MQTT connect (~10s of PCM frames)
        uart_flush_input(UART_NUM_1);
        if (g_uart_audio_codec) g_uart_audio_codec->FlushRingBuf();
        g_mic_open = true;
        g_audio_service->Start();
        g_audio_service->EnableVoiceProcessing(true);
        // Tell server to start listening — without this, server won't process audio
        g_protocol->SendStartListening(kListeningModeAutoStop);
        ESP_LOGI(TAG, "Sent start listening command to server");
    });

    g_protocol->OnAudioChannelClosed([]() {
        ESP_LOGI(TAG, "Audio channel closed");
        g_mic_open = false;
        if (g_audio_service) {
            g_audio_service->EnableVoiceProcessing(false);
        }
    });

    g_protocol->OnNetworkError([](const std::string& msg) {
        ESP_LOGE(TAG, "Network error: %s", msg.c_str());
    });

    /* ── Step 5.5: Handle server JSON messages (xiaozhi-style logging) ── */
    g_protocol->OnIncomingJson([](const cJSON* root) {
        auto type = cJSON_GetObjectItem(root, "type");
        if (!cJSON_IsString(type)) return;

        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (cJSON_IsString(state)) {
                if (strcmp(state->valuestring, "start") == 0) {
                    ESP_LOGI(TAG, "TTS start");
                } else if (strcmp(state->valuestring, "stop") == 0) {
                    ESP_LOGI(TAG, "TTS stop — restarting listening");
                    g_protocol->SendStartListening(kListeningModeAutoStop);
                } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                    auto text = cJSON_GetObjectItem(root, "text");
                    if (cJSON_IsString(text)) {
                        ESP_LOGI(TAG, "<< %s", text->valuestring);
                        xz_send_to_p4("tts", "text", text->valuestring);
                    }
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                xz_send_to_p4("stt", "text", text->valuestring);
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                ESP_LOGI(TAG, "Emotion: %s", emotion->valuestring);
                xz_send_to_p4("emotion", "emotion", emotion->valuestring);
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            ESP_LOGI(TAG, "MCP message received");
            xz_send_to_p4("mcp", "text", "mcp");
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command))
                ESP_LOGI(TAG, "System: %s", command->valuestring);
        }
    });

    /* ── Step 6: Start protocol + open audio channel ── */
    if (!g_protocol->Start()) {
        ESP_LOGE(TAG, "MqttProtocol::Start() failed");
        xiaozhi_relay_stop();
        return false;
    }

    /* OpenAudioChannel sends hello + waits for server hello (up to 10s).
     * This is called from UART demux task (not time-critical), so blocking is OK. */
    if (!g_protocol->OpenAudioChannel()) {
        ESP_LOGE(TAG, "MqttProtocol::OpenAudioChannel() failed");
        xiaozhi_relay_stop();
        return false;
    }

    g_active = true;
    ESP_LOGI(TAG, "Xiaozhi relay started — AudioService + MqttProtocol active");
    return true;
}

/* ══════════════════════════════════════════════
 *  xiaozhi_relay_stop
 * ══════════════════════════════════════════════ */
void xiaozhi_relay_stop(void)
{
    if (!g_active) return;

    ESP_LOGI(TAG, "Stopping xiaozhi relay...");

    g_mic_open = false;

    if (g_protocol) {
        g_protocol->CloseAudioChannel();
    }
    if (g_audio_service) {
        g_audio_service->EnableVoiceProcessing(false);
        g_audio_service->Stop();
        g_audio_service.reset();
    }
    g_protocol.reset();

    if (g_uart_audio_codec) {
        delete g_uart_audio_codec;
        g_uart_audio_codec = nullptr;
        g_uart_codec       = nullptr;
    }

    g_active = false;
    ESP_LOGI(TAG, "Xiaozhi relay stopped");
}

/* ══════════════════════════════════════════════
 *  Control from P4 (mode switch, etc.)
 * ══════════════════════════════════════════════ */
void xiaozhi_relay_on_ctrl_from_p4(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    ESP_LOGI(TAG, "Control from P4: cmd=%d len=%d", cmd, len);

    switch (cmd) {
    case CTRL_ENTER_XIAOZHI:
        /* Stop radar to free WiFi channel for xiaozhi audio */
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
        /* Resume radar for guard mode */
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

/* ══════════════════════════════════════════════
 *  JSON forwarding from P4 to cloud
 * ══════════════════════════════════════════════ */
void xiaozhi_relay_on_json_from_p4(const char *json)
{
    if (!g_active || !g_protocol) return;
    g_protocol->SendMcpMessage(json);
}

/* ══════════════════════════════════════════════
 *  Opus from P4 — DEPRECATED in protocol-stack mode.
 *  Raw PCM now goes through UartAudioCodec::FeedPcmUp().
 *  Kept for API compatibility.
 * ══════════════════════════════════════════════ */
void xiaozhi_relay_on_opus_from_p4(const uint8_t *opus_data, uint16_t len)
{
    (void)opus_data;
    (void)len;
    // No-op: PCM is handled by UartAudioCodec::FeedPcmUp() from the demux task.
}

bool xiaozhi_relay_is_active(void)
{
    return g_active;
}
