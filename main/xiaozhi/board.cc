/* Board + Lang implementation for S3 */
#include "board.h"
#include "audio/audio_codec.h"
#include <esp_mac.h>
#include <cstdio>

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
    void Disconnect() override {}
    bool Send(const std::string& d) override { return false; }
    void OnData(std::function<void(const std::vector<uint8_t>&)> cb) override {}
    bool IsConnected() const override { return false; }
};

/* ── S3Http ─────────────────────────────────── */
class S3Http : public Http {
    void SetHeader(const std::string& k, const std::string& v) override {}
    void SetContent(const std::string& d) override {}
    bool Open(const std::string& m, const std::string& url) override { return false; }
    bool Write(const std::string& d) override { return false; }
    int GetStatusCode() override { return 0; }
    int GetLastError() override { return -1; }
    int GetBodyLength() override { return 0; }
    int Read(char* buf, size_t len) override { return 0; }
    std::string ReadAll() override { return ""; }
    std::string GetResponse() override { return ""; }
    void Close() override {}
};

/* ── S3NetworkInterface ─────────────────────── */
class S3NI : public NetworkInterface {
    std::unique_ptr<Mqtt> CreateMqtt(int i) override { return std::make_unique<S3Mqtt>(); }
    std::unique_ptr<WebSocket> CreateWebSocket(int i) override { return std::make_unique<S3WebSocket>(); }
    std::unique_ptr<Udp> CreateUdp(int i) override { return std::make_unique<S3Udp>(); }
    std::unique_ptr<Tcp> CreateTcp(int i) override { return std::make_unique<S3Tcp>(); }
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
