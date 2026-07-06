/* Stub: matching esp-ml307 API exactly — concrete classes with stub implementations */
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <mutex>

/* ── Forward declarations ──────────────────── */
class NetworkInterface;

/* ── Mqtt (matches esp-ml307 mqtt.h exactly) ── */
class Mqtt {
public:
    virtual ~Mqtt() {}
    void SetKeepAlive(int s) { keep_alive_ = s; }
    virtual bool Connect(const std::string broker_address, int broker_port,
                         const std::string client_id, const std::string username,
                         const std::string password) = 0;
    virtual void Disconnect() = 0;
    virtual bool Publish(const std::string topic, const std::string payload, int qos = 0) = 0;
    virtual bool Subscribe(const std::string topic, int qos = 0) = 0;
    virtual bool Unsubscribe(const std::string topic) { return false; }
    virtual bool IsConnected() = 0;
    virtual void OnConnected(std::function<void()> cb) { on_conn_ = std::move(cb); }
    virtual void OnDisconnected(std::function<void()> cb) { on_disc_ = std::move(cb); }
    virtual void OnMessage(std::function<void(const std::string&, const std::string&)> cb) { on_msg_ = std::move(cb); }
    virtual void OnError(std::function<void(const std::string&)> cb) { on_err_ = std::move(cb); }
    virtual int GetLastError() = 0;
protected:
    int keep_alive_ = 120;
    std::function<void(const std::string&,const std::string&)> on_msg_;
    std::function<void()> on_conn_, on_disc_;
    std::function<void(const std::string&)> on_err_;
};

/* ── Tcp (matches esp-ml307 tcp.h exactly) ── */
class Tcp {
public:
    virtual ~Tcp() = default;
    virtual bool Connect(const std::string& host, int port) = 0;
    virtual void Disconnect() = 0;
    virtual int Send(const std::string& data) = 0;
    virtual void OnStream(std::function<void(const std::string& data)> cb) { stream_cb_ = cb; }
    virtual void OnDisconnected(std::function<void()> cb) { disc_cb_ = cb; }
    bool connected() const { return connected_; }
    virtual int GetLastError() = 0;
protected:
    std::function<void(const std::string&)> stream_cb_;
    std::function<void()> disc_cb_;
    bool connected_ = false;
};

/* ── Udp (matches esp-ml307 udp.h) ─────────── */
class Udp {
public:
    virtual ~Udp() = default;
    virtual bool Connect(const std::string& host, int port) = 0;
    virtual void Disconnect() = 0;
    virtual bool Send(const std::string& data) = 0;  /* string, not vector */
    virtual bool Send(const std::vector<uint8_t>& data) = 0;
    virtual void OnMessage(std::function<void(const std::string&)> cb) = 0;  /* OnMessage, not OnReceive */
    virtual bool IsConnected() const = 0;
};

/* ── Http ───────────────────────────────────── */
class Http {
public:
    virtual ~Http() = default;
    virtual void SetHeader(const std::string& key, const std::string& value) = 0;
    virtual void SetContent(const std::string& data) = 0;
    virtual bool Open(const std::string& method, const std::string& url) = 0;
    virtual bool Write(const std::string& data) = 0;
    virtual int GetStatusCode() = 0;
    virtual int GetLastError() = 0;
    virtual int GetBodyLength() = 0;
    virtual int Read(char* buf, size_t len) = 0;
    virtual std::string ReadAll() = 0;
    virtual std::string GetResponse() = 0;
    virtual void Close() = 0;
};

/* ── WebSocket (matches esp-ml307 web_socket.h) ── */
class WebSocket {
public:
    virtual ~WebSocket() = default;
    void SetHeader(const char* key, const char* value) { headers_[key] = value; }
    void SetReceiveBufferSize(size_t s) { rx_buf_sz_ = s; }
    virtual bool IsConnected() const = 0;
    virtual bool Connect(const char* uri) = 0;
    virtual bool Send(const std::string& data) = 0;
    virtual bool Send(const void* data, size_t len, bool binary = false, bool fin = true) = 0;  /* 4-arg overload */
    void Ping() {}
    void Close() {}
    virtual void OnConnected(std::function<void()> cb) { on_conn_ = std::move(cb); }
    virtual void OnDisconnected(std::function<void()> cb) { on_disc_ = std::move(cb); }
    virtual void OnData(std::function<void(const char*, size_t, bool binary)> cb) { on_data_ = std::move(cb); }
    virtual void OnError(std::function<void(int)> cb) { on_err_ = std::move(cb); }
    virtual int GetLastError() = 0;
protected:
    std::map<std::string,std::string> headers_;
    size_t rx_buf_sz_ = 2048;
    std::function<void(const char*,size_t,bool)> on_data_;
    std::function<void(int)> on_err_;
    std::function<void()> on_conn_, on_disc_;
};

/* ── NetworkInterface (matches esp-ml307) ──── */
class NetworkInterface {
public:
    virtual ~NetworkInterface() = default;
    virtual std::unique_ptr<Mqtt> CreateMqtt(int id) = 0;
    virtual std::unique_ptr<WebSocket> CreateWebSocket(int id) = 0;
    virtual std::unique_ptr<Udp> CreateUdp(int id) = 0;
    virtual std::unique_ptr<Tcp> CreateTcp(int id) = 0;
    virtual std::unique_ptr<Tcp> CreateSsl(int id) = 0;  /* HTTPS = TLS socket */
    virtual std::unique_ptr<Http> CreateHttp(int id) = 0;
};
