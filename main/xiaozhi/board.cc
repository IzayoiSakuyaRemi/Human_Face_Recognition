/* Board + Lang implementation for S3 */
#include "board.h"
#include "audio/audio_codec.h"
#include <esp_mac.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_tls.h>
#include <esp_log.h>
#include <cstdio>
#include <cstring>

/* ── S3Mqtt ─────────────────────────────────── */
class S3Mqtt : public Mqtt {
    bool Connect(const std::string broker, int port, const std::string id,
                 const std::string user, const std::string pass) override { return false; }
    void Disconnect() override {}
    bool Publish(const std::string t, const std::string d, int qos) override { return false; }
    bool Subscribe(const std::string t, int qos) override { return false; }
    bool IsConnected() override { return false; }
    int GetLastError() override { return -1; }
};

/* ── S3WebSocket ────────────────────────────── */
class S3WebSocket : public WebSocket {
    bool Connect(const char* uri) override { return false; }
    bool Send(const std::string& d) override { return false; }
    bool Send(const void* d, size_t len, bool bin, bool fin) override { return false; }
    int GetLastError() override { return -1; }
    bool IsConnected() const override { return false; }
};

/* ── S3Udp ──────────────────────────────────── */
class S3Udp : public Udp {
    bool Connect(const std::string& h, int p) override { return false; }
    void Disconnect() override {}
    bool Send(const std::string& d) override { return false; }
    bool Send(const std::vector<uint8_t>& d) override { return false; }
    void OnMessage(std::function<void(const std::string&)> cb) override {}
    bool IsConnected() const override { return false; }
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
    uint8_t m[6]; esp_read_mac(m, ESP_MAC_WIFI_STA);
    char b[13]; snprintf(b, 13, "%02X%02X%02X%02X%02X%02X", m[0],m[1],m[2],m[3],m[4],m[5]);
    return b;
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
