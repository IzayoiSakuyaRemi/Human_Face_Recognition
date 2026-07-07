/* Board + Lang implementation for S3 */
#include "board.h"
#include "audio/audio_codec.h"
#include "system_info.h"
#include "settings.h"
#include <esp_mac.h>
#include <esp_chip_info.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_psram.h>
#include <esp_crt_bundle.h>
#include <esp_tls.h>
#include <esp_log.h>
#include <freertos/event_groups.h>
#include <mqtt_client.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

/* ── S3Mqtt (real esp_mqtt_client wrapper, synchronous Connect) ── */
class S3Mqtt : public Mqtt {
    esp_mqtt_client_handle_t client_ = nullptr;
    bool connected_ = false;
    int last_error_ = 0;
    std::string broker_uri_;
    std::string client_id_;
    std::string username_;
    std::string password_;
    EventGroupHandle_t sync_group_ = nullptr;

    static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                    int32_t event_id, void *event_data) {
        auto *self = static_cast<S3Mqtt *>(arg);
        self->onMqttEvent(event_id, (esp_mqtt_event_handle_t)event_data);
    }

    void onMqttEvent(int32_t event_id, esp_mqtt_event_handle_t ev) {
        switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI("S3Mqtt", "Connected");
            connected_ = true;
            if (sync_group_) xEventGroupSetBits(sync_group_, 1);
            if (on_conn_) on_conn_();
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI("S3Mqtt", "Disconnected");
            connected_ = false;
            if (on_disc_) on_disc_();
            break;
        case MQTT_EVENT_DATA: {
            std::string topic(ev->topic, ev->topic_len);
            std::string payload(ev->data, ev->data_len);
            if (on_msg_) on_msg_(topic, payload);
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGE("S3Mqtt", "Error");
            last_error_ = -1;
            if (sync_group_) xEventGroupSetBits(sync_group_, 2);  // error bit
            break;
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI("S3Mqtt", "Connecting to %s...", broker_uri_.c_str());
            break;
        default:
            break;
        }
    }

public:
    ~S3Mqtt() override { Disconnect(); if (sync_group_) vEventGroupDelete(sync_group_); }

    bool Connect(const std::string broker, int port, const std::string id,
                 const std::string user, const std::string pass) override {
        client_id_ = id;
        username_  = user;
        password_  = pass;

        /* Build broker URI (strip mqtts:// prefix if present, keep full URI) */
        if (broker.find("mqtts://") == 0 || broker.find("mqtt://") == 0) {
            broker_uri_ = broker;
        } else {
            broker_uri_ = (port == 8883 ? "mqtts://" : "mqtt://") + broker + ":" + std::to_string(port);
        }

        esp_mqtt_client_config_t cfg = {};
        cfg.broker.address.uri = broker_uri_.c_str();
        cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.credentials.client_id = client_id_.c_str();
        if (!username_.empty()) cfg.credentials.username = username_.c_str();
        if (!password_.empty()) cfg.credentials.authentication.password = password_.c_str();
        cfg.session.keepalive = keep_alive_;

        // Create sync EventGroup for blocking Connect()
        if (!sync_group_) sync_group_ = xEventGroupCreate();
        xEventGroupClearBits(sync_group_, 0xFF);

        client_ = esp_mqtt_client_init(&cfg);
        if (!client_) {
            ESP_LOGE("S3Mqtt", "esp_mqtt_client_init failed");
            last_error_ = -1;
            return false;
        }
        esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, mqtt_event_handler, this);
        esp_err_t err = esp_mqtt_client_start(client_);
        if (err != ESP_OK) {
            ESP_LOGE("S3Mqtt", "esp_mqtt_client_start failed: %d", err);
            last_error_ = err;
            esp_mqtt_client_destroy(client_);
            client_ = nullptr;
            return false;
        }

        // Block until connected or error (10s timeout)
        EventBits_t bits = xEventGroupWaitBits(sync_group_, 0x03, pdTRUE, pdFALSE,
                                                pdMS_TO_TICKS(10000));
        if (bits & 1) {
            ESP_LOGI("S3Mqtt", "Connect() completed successfully");
            return true;
        }
        ESP_LOGE("S3Mqtt", "Connect() timeout or error (bits=0x%02x)", (int)bits);
        return false;
    }

    void Disconnect() override {
        if (client_) {
            esp_mqtt_client_stop(client_);
            esp_mqtt_client_destroy(client_);
            client_ = nullptr;
        }
        connected_ = false;
    }

    bool Publish(const std::string topic, const std::string payload, int qos) override {
        if (!client_ || !connected_) return false;
        int ret = esp_mqtt_client_publish(client_, topic.c_str(), payload.c_str(), 0, qos, 0);
        return ret >= 0;
    }

    bool Subscribe(const std::string topic, int qos) override {
        if (!client_ || !connected_) return false;
        int ret = esp_mqtt_client_subscribe(client_, topic.c_str(), qos);
        return ret >= 0;
    }

    bool IsConnected() override { return connected_; }
    int GetLastError() override { return last_error_; }
};

/* ── S3WebSocket ────────────────────────────── */
class S3WebSocket : public WebSocket {
    bool Connect(const char* uri) override { return false; }
    bool Send(const std::string& d) override { return false; }
    bool Send(const void* d, size_t len, bool bin, bool fin) override { return false; }
    int GetLastError() override { return -1; }
    bool IsConnected() const override { return false; }
};

/* ── S3Udp (real lwIP UDP socket) ────────────── */
class S3Udp : public Udp {
    int sock_ = -1;
    bool connected_ = false;
    std::function<void(const std::string&)> on_message_;
    TaskHandle_t rx_task_ = nullptr;
    bool running_ = true;

    static void rx_task_func(void *arg) {
        auto *self = static_cast<S3Udp *>(arg);
        self->rxLoop();
        vTaskDelete(NULL);
    }

    void rxLoop() {
        uint8_t buf[2048];
        while (running_) {
            fd_set rfds; FD_ZERO(&rfds); FD_SET(sock_, &rfds);
            struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 }; // 500ms
            int ret = select(sock_ + 1, &rfds, NULL, NULL, &tv);
            if (ret <= 0) continue;
            int len = recvfrom(sock_, buf, sizeof(buf), 0, NULL, NULL);
            if (len > 0 && on_message_) {
                on_message_(std::string((const char *)buf, len));
            }
        }
    }

public:
    ~S3Udp() override { Disconnect(); }

    bool Connect(const std::string& host, int port) override {
        if (sock_ >= 0) Disconnect();

        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ < 0) {
            ESP_LOGE("S3Udp", "socket failed: %d", errno);
            return false;
        }

        // Resolve hostname
        struct addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        char port_str[8]; snprintf(port_str, sizeof(port_str), "%d", port);
        if (getaddrinfo(host.c_str(), port_str, &hints, &res) != 0 || !res) {
            ESP_LOGE("S3Udp", "getaddrinfo failed for %s:%d", host.c_str(), port);
            close(sock_); sock_ = -1;
            return false;
        }

        if (connect(sock_, res->ai_addr, res->ai_addrlen) < 0) {
            ESP_LOGE("S3Udp", "connect failed: %d", errno);
            freeaddrinfo(res);
            close(sock_); sock_ = -1;
            return false;
        }
        freeaddrinfo(res);

        // Set non-blocking
        int flags = fcntl(sock_, F_GETFL, 0);
        fcntl(sock_, F_SETFL, flags | O_NONBLOCK);

        // Spawn receive task (needs ~4KB for AES decryption in callback chain)
        running_ = true;
        xTaskCreate(rx_task_func, "s3udp_rx", 5120, this, 5, &rx_task_);

        connected_ = true;
        ESP_LOGI("S3Udp", "Connected to %s:%d", host.c_str(), port);
        return true;
    }

    void Disconnect() override {
        running_ = false;
        rx_task_ = nullptr; // task deletes itself
        if (sock_ >= 0) {
            close(sock_);
            sock_ = -1;
        }
        connected_ = false;
    }

    bool Send(const std::string& data) override {
        if (sock_ < 0 || !connected_) return false;
        int ret = send(sock_, data.data(), data.size(), 0);
        return ret >= 0;
    }

    bool Send(const std::vector<uint8_t>& data) override {
        if (sock_ < 0 || !connected_) return false;
        int ret = send(sock_, data.data(), data.size(), 0);
        return ret >= 0;
    }

    void OnMessage(std::function<void(const std::string&)> cb) override {
        on_message_ = std::move(cb);
    }

    bool IsConnected() const override { return connected_ && sock_ >= 0; }
};

/* ── S3Tcp ──────────────────────────────────── */
class S3Tcp : public Tcp {
    bool Connect(const std::string& h, int p) override { return false; }
    void Disconnect() override { connected_ = false; }
    int Send(const std::string& d) override { return 0; }
    int GetLastError() override { return -1; }
};

/* ── S3Ssl (TLS socket like xiaozhi EspSsl) ─── */
class S3Ssl : public Tcp {
    esp_tls_t* tls_ = nullptr;

    bool Connect(const std::string& host, int port) override {
        tls_ = esp_tls_init();
        if (!tls_) return false;
        esp_tls_cfg_t cfg = {};
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        int ret = esp_tls_conn_new_sync(host.c_str(), host.length(), port, &cfg, tls_);
        if (ret != 1) { esp_tls_conn_destroy(tls_); tls_ = nullptr; return false; }
        connected_ = true;
        return true;
    }
    void Disconnect() override {
        if (tls_) { esp_tls_conn_destroy(tls_); tls_ = nullptr; }
        connected_ = false;
    }
    int Send(const std::string& d) override {
        if (!tls_) return 0;
        size_t sent = 0;
        while (sent < d.size()) {
            int w = esp_tls_conn_write(tls_, d.data() + sent, d.size() - sent);
            if (w <= 0) return -1;
            sent += w;
        }
        /* Read response into stream callback */
        char buf[1024]; int r;
        while ((r = esp_tls_conn_read(tls_, buf, sizeof(buf) - 1)) > 0) {
            buf[r] = 0;
            if (stream_cb_) stream_cb_(std::string(buf, r));
        }
        return (int)sent;
    }
    int GetLastError() override { return -1; }
};

/* ── S3Http (same approach as xiaozhi EspSsl: raw esp_tls + manual HTTP) ── */
class S3Http : public Http {
    esp_tls_t* tls_ = nullptr;
    std::string host_;
    int port_ = 443;
    std::string path_;
    std::string method_, content_;
    std::map<std::string, std::string> headers_;
    bool connected_ = false;
    std::string cached_body_;
    int cached_status_ = 0;

    /* Parse URL: https://host:port/path */
    bool parseUrl(const std::string& url) {
        size_t ps = url.find("://");
        if (ps == std::string::npos) return false;
        std::string rest = url.substr(ps + 3);
        size_t sl = rest.find('/');
        std::string hp = (sl == std::string::npos) ? rest : rest.substr(0, sl);
        path_ = (sl == std::string::npos) ? "/" : rest.substr(sl);

        size_t cp = hp.find(':');
        if (cp != std::string::npos) {
            host_ = hp.substr(0, cp);
            port_ = std::stoi(hp.substr(cp + 1));
        } else {
            host_ = hp;
            port_ = 443;
        }
        return true;
    }

    bool doRequest() {
        if (connected_) return true;

        /* ── TLS Connect (exactly like xiaozhi EspSsl::Connect) ── */
        tls_ = esp_tls_init();
        if (!tls_) { ESP_LOGE("S3Http", "esp_tls_init failed"); return false; }

        esp_tls_cfg_t cfg = {};
        cfg.crt_bundle_attach = esp_crt_bundle_attach;

        int ret = esp_tls_conn_new_sync(host_.c_str(), host_.length(), port_, &cfg, tls_);
        if (ret != 1) {
            ESP_LOGE("S3Http", "TLS connect to %s:%d failed: ret=%d", host_.c_str(), port_, ret);
            esp_tls_conn_destroy(tls_); tls_ = nullptr;
            return false;
        }

        /* ── Build HTTP request ── */
        std::string req = method_ + " " + path_ + " HTTP/1.1\r\n";
        req += "Host: " + host_ + "\r\n";
        for (auto& h : headers_) req += h.first + ": " + h.second + "\r\n";
        if (!content_.empty()) {
            req += "Content-Length: " + std::to_string(content_.size()) + "\r\n";
            req += "Content-Type: application/json\r\n";
        }
        req += "Connection: close\r\n\r\n";
        if (!content_.empty()) req += content_;

        /* ── Send ── */
        size_t sent = 0;
        while (sent < req.size()) {
            int w = esp_tls_conn_write(tls_, req.data() + sent, req.size() - sent);
            if (w <= 0) { ESP_LOGE("S3Http", "Write failed"); goto fail; }
            sent += w;
        }

        /* ── Read response ── */
        {
            char buf[1024];
            int r = esp_tls_conn_read(tls_, buf, sizeof(buf) - 1);
            if (r <= 0) { ESP_LOGE("S3Http", "Read failed"); goto fail; }
            buf[r] = 0;
            std::string resp(buf, r);

            /* Parse status line: "HTTP/1.1 200 OK\r\n..." */
            size_t nl = resp.find("\r\n");
            std::string status_line = (nl != std::string::npos) ? resp.substr(0, nl) : resp;
            size_t sp1 = status_line.find(' ');
            size_t sp2 = status_line.find(' ', sp1 + 1);
            cached_status_ = (sp1 != std::string::npos) ?
                std::stoi(status_line.substr(sp1 + 1, sp2 - sp1 - 1)) : 0;

            /* Find body (after \r\n\r\n) */
            size_t hdr_end = resp.find("\r\n\r\n");
            if (hdr_end != std::string::npos)
                cached_body_ = resp.substr(hdr_end + 4);

            /* Read remaining body */
            if (cached_status_ == 200) {
                while ((r = esp_tls_conn_read(tls_, buf, sizeof(buf) - 1)) > 0) {
                    buf[r] = 0; cached_body_ += buf;
                }
            }
        }

        connected_ = true;
        return true;

    fail:
        if (tls_) { esp_tls_conn_destroy(tls_); tls_ = nullptr; }
        return false;
    }

public:
    ~S3Http() { Close(); }

    void SetHeader(const std::string& k, const std::string& v) override { headers_[k] = v; }
    void SetContent(const std::string& d) override { content_ = d; }

    bool Open(const std::string& m, const std::string& url) override {
        method_ = m; cached_body_.clear(); cached_status_ = 0; connected_ = false;
        return parseUrl(url);
    }

    bool Write(const std::string& d) override { content_ += d; return true; }

    int GetStatusCode() override { doRequest(); return cached_status_; }
    int GetLastError() override { return cached_status_ != 200 ? -1 : 0; }
    int GetBodyLength() override { doRequest(); return (int)cached_body_.size(); }
    int Read(char* buf, size_t len) override {
        doRequest();
        size_t n = len < cached_body_.size() ? len : cached_body_.size();
        if (n == 0) return 0;
        memcpy(buf, cached_body_.data(), n);
        return (int)n;
    }
    std::string ReadAll() override { doRequest(); return cached_body_; }
    std::string GetResponse() override { return cached_body_; }
    void Close() override {
        if (tls_) { esp_tls_conn_destroy(tls_); tls_ = nullptr; }
        connected_ = false;
    }
};

/* ── S3NetworkInterface ─────────────────────── */
class S3NI : public NetworkInterface {
    std::unique_ptr<Mqtt> CreateMqtt(int i) override { return std::make_unique<S3Mqtt>(); }
    std::unique_ptr<WebSocket> CreateWebSocket(int i) override { return std::make_unique<S3WebSocket>(); }
    std::unique_ptr<Udp> CreateUdp(int i) override { return std::make_unique<S3Udp>(); }
    std::unique_ptr<Tcp> CreateTcp(int i) override { return std::make_unique<S3Tcp>(); }
    std::unique_ptr<Tcp> CreateSsl(int i) override { return std::make_unique<S3Ssl>(); }
    std::unique_ptr<Http> CreateHttp(int i) override { return std::make_unique<S3Http>(); }
};

/* ── Board ──────────────────────────────────── */
Board::Board() { net_ = std::make_unique<S3NI>(); }
AudioCodec *g_uart_codec = nullptr;
AudioCodec* Board::GetAudioCodec() { return g_uart_codec; }
std::string Board::GetUuid() {
    // Return UUID v4 (same as in GetSystemInfoJson), not MAC hex.
    // The OTA server requires Client-Id header to be a UUID.
    Settings board_settings("board", true);
    std::string uuid = board_settings.GetString("uuid");
    if (uuid.empty()) {
        // Generate on first boot
        uint8_t rnd[16]; esp_fill_random(rnd, sizeof(rnd));
        rnd[6] = (rnd[6] & 0x0F) | 0x40;
        rnd[8] = (rnd[8] & 0x3F) | 0x80;
        char buf[37];
        snprintf(buf, sizeof(buf),
            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            rnd[0],rnd[1],rnd[2],rnd[3], rnd[4],rnd[5],rnd[6],rnd[7],
            rnd[8],rnd[9],rnd[10],rnd[11], rnd[12],rnd[13],rnd[14],rnd[15]);
        uuid = buf;
        board_settings.SetString("uuid", uuid);
    }
    return uuid;
}
std::string Board::GetSystemInfoJson() {
    /* Match original xiaozhi-esp32 format exactly so OTA server accepts this device */

    // Generate UUID v4 if not already stored
    Settings board_settings("board", true);
    std::string uuid = board_settings.GetString("uuid");
    if (uuid.empty()) {
        uint8_t rnd[16]; esp_fill_random(rnd, sizeof(rnd));
        rnd[6] = (rnd[6] & 0x0F) | 0x40;   // version 4
        rnd[8] = (rnd[8] & 0x3F) | 0x80;   // variant 1
        char buf[37];
        snprintf(buf, sizeof(buf),
            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            rnd[0],rnd[1],rnd[2],rnd[3], rnd[4],rnd[5],rnd[6],rnd[7],
            rnd[8],rnd[9],rnd[10],rnd[11], rnd[12],rnd[13],rnd[14],rnd[15]);
        uuid = buf;
        board_settings.SetString("uuid", uuid);
    }

    // MAC address with colons: "xx:xx:xx:xx:xx:xx"
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str),
        "%02x:%02x:%02x:%02x:%02x:%02x", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

    auto app_desc = esp_app_get_description();
    esp_chip_info_t ci; esp_chip_info(&ci);

    std::string json = "{\"version\":2,\"language\":\"en-US\",";
    json += "\"flash_size\":" + std::to_string(SystemInfo::GetFlashSize()) + ",";
#ifdef CONFIG_SPIRAM
    json += "\"psram_size\":" + std::to_string(esp_psram_get_size()) + ",";
#else
    json += "\"psram_size\":0,";
#endif
    json += "\"minimum_free_heap_size\":\"" + std::to_string(SystemInfo::GetMinimumFreeHeapSize()) + "\",";
    json += "\"mac_address\":\"" + SystemInfo::GetMacAddress() + "\",";
    json += "\"uuid\":\"" + uuid + "\",";
    json += "\"chip_model_name\":\"" + SystemInfo::GetChipModelName() + "\",";

    json += "\"chip_info\":{";
    json += "\"model\":" + std::to_string(ci.model) + ",";
    json += "\"cores\":" + std::to_string(ci.cores) + ",";
    json += "\"revision\":" + std::to_string(ci.revision) + ",";
    json += "\"features\":" + std::to_string(ci.features) + "},";

    json += "\"application\":{";
    json += "\"name\":\"" + std::string(app_desc->project_name) + "\",";
    json += "\"version\":\"" + std::string(app_desc->version) + "\",";
    json += "\"compile_time\":\"" + std::string(app_desc->date) + "T" + std::string(app_desc->time) + "Z\",";
    json += "\"idf_version\":\"" + std::string(app_desc->idf_ver) + "\",";
    // Compute elf_sha256
    char sha256_str[65];
    for (int i = 0; i < 32; i++) {
        snprintf(sha256_str + i * 2, sizeof(sha256_str) - i * 2, "%02x", app_desc->app_elf_sha256[i]);
    }
    json += "\"elf_sha256\":\"" + std::string(sha256_str) + "\"";
    json += "},";

    json += "\"partition_table\":[";
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it) {
        const esp_partition_t *p = esp_partition_get(it);
        json += "{\"label\":\"" + std::string(p->label) + "\",";
        json += "\"type\":" + std::to_string(p->type) + ",";
        json += "\"subtype\":" + std::to_string(p->subtype) + ",";
        json += "\"address\":" + std::to_string(p->address) + ",";
        json += "\"size\":" + std::to_string(p->size) + "},";
        it = esp_partition_next(it);
    }
    if (json.back() == ',') json.pop_back();
    json += "],";

    json += "\"ota\":{\"label\":\"factory\"},";

    // Display info (S3 has no display hardware)
    json += "\"display\":{\"monochrome\":false,\"width\":0,\"height\":0},";

    json += "\"board\":" + GetBoardJson();
    json += "}";
    return json;
}

/* ── Lang strings ───────────────────────────── */
namespace Lang {
    const char *CODE = "en-US";
    namespace Strings {
        const char *SERVER_NOT_FOUND="NF", *SERVER_NOT_CONNECTED="NC", *SERVER_ERROR="ERR", *SERVER_TIMEOUT="TO";
        const char *STANDBY="Ready", *CONNECTING="...", *LISTENING="...", *SPEAKING="...", *VERSION="v";
        const char *LOADING_PROTOCOL="", *CHECKING_NEW_VERSION="", *ACTIVATION="";
        const char *FOUND_NEW_ASSETS="", *DOWNLOAD_ASSETS_FAILED="", *CHECK_NEW_VERSION_FAILED="";
        const char *OTA_UPGRADE="OTA", *UPGRADING="...", *NEW_VERSION="New", *UPGRADE_FAILED="Fail", *PLEASE_WAIT="";
        const char *SCANNING_WIFI="", *CONNECT_TO="", *CONNECTED_TO="";
        const char *REGISTERING_NETWORK="", *DETECTING_MODULE="";
        const char *ERROR="Err", *PIN_ERROR="", *REG_ERROR="", *MODEM_INIT_ERROR="";
        const char *RTC_MODE_OFF="Off", *RTC_MODE_ON="On";
    }
    namespace Sounds {
        const char *OGG_0="",*OGG_1="",*OGG_2="",*OGG_3="",*OGG_4="";
        const char *OGG_5="",*OGG_6="",*OGG_7="",*OGG_8="",*OGG_9="";
        const char *OGG_ACTIVATION="",*OGG_EXCLAMATION="",*OGG_POPUP="",*OGG_SUCCESS="";
        const char *OGG_UPGRADE="",*OGG_VIBRATION="";
    }
}
