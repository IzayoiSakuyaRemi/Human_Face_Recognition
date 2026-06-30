#include "wifi_provisioning.hpp"
#include <cstring>
#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_http_server.h>
#include <vector>
#include <algorithm>
#include <cstdlib>

// Global: last known IP string for app_main to read
char g_wifi_ip[32] = {0};

static const char *TAG = "wifi_prov";

// NVS keys
static const char *NVS_NAMESPACE = "wifi_prov";
static const char *NVS_KEY_SSID   = "ssid";
static const char *NVS_KEY_PASS   = "password";

// Event group bits
static const int BIT_CONNECTED   = BIT0;
static const int BIT_TIMEOUT     = BIT1;
static const int BIT_CONFIG_DONE = BIT2;
static const int BIT_SCAN_DONE   = BIT3;
static EventGroupHandle_t s_evt = nullptr;

// Config AP settings
static const char *AP_SSID     = "ESP32-P4-Config";
static const char *AP_PASSWORD = "12345678";
static const int   CONNECT_TIMEOUT_SEC = 60;
static bool s_ap_mode = false;
static bool s_wifi_inited = false;

static wifi_prov_status_cb_t s_status_cb = nullptr;
static esp_netif_t *s_sta_netif = nullptr;
static esp_netif_t *s_ap_netif  = nullptr;

// SSID/password received from config page
static char s_cfg_ssid[33] = {0};
static char s_cfg_password[65] = {0};

// Scan results
static struct { char ssid[33]; int rssi; } scan_results[20];
static int scan_count = 0;

// Forward declarations
static esp_err_t nvs_erase_creds(void);
static bool parse_ssid_pass(const char *body);

// --- NVS helpers ---
static esp_err_t nvs_save_creds(const char *ssid, const char *pass) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, pass);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

static esp_err_t nvs_load_creds(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t len = ssid_len;
    err = nvs_get_str(h, NVS_KEY_SSID, ssid_out, &len);
    if (err != ESP_OK) { nvs_close(h); return err; }
    len = pass_len;
    err = nvs_get_str(h, NVS_KEY_PASS, pass_out, &len);
    nvs_close(h);
    return err;
}

static esp_err_t nvs_erase_creds(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

// --- WiFi event handler ---
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            if (!s_ap_mode) esp_wifi_connect();  // only in STA mode
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "WiFi disconnected, reason=%d", ev->reason);
            if (!s_ap_mode) {
                esp_wifi_connect();  // auto-retry in STA mode
            }
        } else if (id == WIFI_EVENT_SCAN_DONE) {
            uint16_t num = 20;
            wifi_ap_record_t aps[20];
            memset(aps, 0, sizeof(aps));
            esp_wifi_scan_get_ap_records(&num, aps);
            scan_count = (num > 20) ? 20 : num;
            for (int i = 0; i < scan_count; i++) {
                strncpy(scan_results[i].ssid, (const char *)aps[i].ssid, 32);
                scan_results[i].rssi = aps[i].rssi;
            }
            std::sort(scan_results, scan_results + scan_count,
                      [](const auto &a, const auto &b) { return a.rssi > b.rssi; });
            xEventGroupSetBits(s_evt, BIT_SCAN_DONE);
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
            snprintf(g_wifi_ip, sizeof(g_wifi_ip), "WiFi: " IPSTR, IP2STR(&ev->ip_info.ip));
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
            if (!s_ap_mode) xEventGroupSetBits(s_evt, BIT_CONNECTED);
        }
    }
}

// --- WiFi init (once, at startup) ---
static esp_err_t wifi_init_once() {
    if (s_wifi_inited) return ESP_OK;

    // 创建 STA netif (总是需要)
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    s_wifi_inited = true;
    ESP_LOGI(TAG, "WiFi driver initialized");
    return ESP_OK;
}

// --- STA mode connect ---
static bool wifi_try_connect(const char *ssid, const char *pass) {
    s_ap_mode = false;
    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t cfg = {};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.scan_method = WIFI_FAST_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    xEventGroupClearBits(s_evt, BIT_CONNECTED);
    ESP_ERROR_CHECK(esp_wifi_start());

    if (s_status_cb) s_status_cb("Connecting...", false);

    EventBits_t bits = xEventGroupWaitBits(s_evt, BIT_CONNECTED,
                                           pdTRUE, pdFALSE, pdMS_TO_TICKS(CONNECT_TIMEOUT_SEC * 1000));
    if (bits & BIT_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected!");
        if (s_status_cb) s_status_cb("WiFi Connected", true);
        return true;
    }

    ESP_LOGW(TAG, "WiFi connection timeout");
    esp_wifi_stop();
    return false;
}

// ======================= HTTP Config Server =======================
static const char *HTML_PAGE = R"raw(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>WiFi 配网</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,sans-serif;background:#1a1a2e;color:#eee;min-height:100vh;display:flex;justify-content:center;align-items:center;padding:20px}
.card{background:#16213e;border-radius:16px;padding:30px 24px;max-width:420px;width:100%;box-shadow:0 8px 32px rgba(0,0,0,.4)}
h2{text-align:center;margin-bottom:24px;color:#e94560;font-size:22px}
label{display:block;margin:16px 0 6px;color:#a0a0b0;font-size:14px}
select,input{width:100%;padding:12px;border:1px solid #0f3460;border-radius:8px;background:#1a1a2e;color:#eee;font-size:15px}
select:focus,input:focus{outline:none;border-color:#e94560}
.btn{width:100%;margin-top:24px;padding:14px;border:none;border-radius:8px;background:#e94560;color:#fff;font-size:16px;font-weight:bold;cursor:pointer}
.btn:active{opacity:.85}
.btn-scan{margin-top:8px;background:#0f3460}
#status{margin-top:16px;text-align:center;font-size:13px;color:#a0a0b0}
#spinner{display:none;margin:8px auto;width:24px;height:24px;border:3px solid #333;border-top-color:#e94560;border-radius:50%;animation:spin .7s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
</style>
</head>
<body>
<div class="card">
<h2>ESP32-P4 WiFi 配网</h2>
<p style="text-align:center;color:#a0a0b0;margin-bottom:8px">连接 AP 后请打开 http://192.168.4.1</p>
<label>WiFi 网络</label>
<select id="ssid"><option value="">请先扫描...</option></select>
<button class="btn btn-scan" onclick="scanNetworks()">扫描附近网络</button>
<label>密码</label>
<input type="password" id="pass" placeholder="输入 WiFi 密码">
<button class="btn" onclick="doConnect()">连接</button>
<div id="spinner"></div>
<div id="status"></div>
</div>
<script>
var s=document.getElementById('status'),p=document.getElementById('spinner'),d=document.getElementById('ssid');
function t(m){s.textContent=m;p.style.display='none'}
function l(){p.style.display='block';s.textContent=''}
async function scanNetworks(){l();try{let r=await fetch('/api/scan'),j=await r.json();d.innerHTML='';j.ap_list.forEach(a=>{let o=document.createElement('option');o.value=a.ssid;o.textContent=a.ssid+' ('+a.rssi+'dBm)';d.appendChild(o)});t('扫描到 '+j.ap_list.length+' 个网络')}catch(e){t('扫描失败')}}
async function doConnect(){let x=d.value,y=document.getElementById('pass').value;if(!x){t('请选择网络');return}l();try{let r=await fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(x)+'&password='+encodeURIComponent(y)});if(r.ok){t('保存成功!设备将连接...')}else{t('失败')}}catch(e){t('请求出错')}}
</script>
</body>
</html>)raw";

static void wifi_do_scan(void) {
    scan_count = 0;
    memset(scan_results, 0, sizeof(scan_results));
    xEventGroupClearBits(s_evt, BIT_SCAN_DONE);
    esp_wifi_scan_start(nullptr, false);  // same as xiaozhi
    // Wait up to 5s for WIFI_EVENT_SCAN_DONE (handled in main event handler)
    xEventGroupWaitBits(s_evt, BIT_SCAN_DONE, pdTRUE, pdTRUE, pdMS_TO_TICKS(5000));
}

static esp_err_t http_scan_handler(httpd_req_t *req) {
    wifi_do_scan();
    char json[4096];
    int off = snprintf(json, sizeof(json), "{\"ap_list\":[");
    for (int i = 0; i < scan_count; i++)
        off += snprintf(json + off, sizeof(json) - off,
                        "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                        i > 0 ? "," : "", scan_results[i].ssid, scan_results[i].rssi);
    off += snprintf(json + off, sizeof(json) - off, "]}");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, off);
    return ESP_OK;
}

static esp_err_t http_connect_handler(httpd_req_t *req) {
    char buf[512];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty"); return ESP_FAIL; }
    buf[len] = '\0';

    if (!parse_ssid_pass(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Parse error");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Config page: SSID=%s", s_cfg_ssid);
    if (strlen(s_cfg_ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID empty");
        return ESP_FAIL;
    }
    nvs_save_creds(s_cfg_ssid, s_cfg_password);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    xEventGroupSetBits(s_evt, BIT_CONFIG_DONE);
    return ESP_OK;
}

static esp_err_t http_index_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_PAGE, strlen(HTML_PAGE));
    return ESP_OK;
}

// Parse SSID/password from BOTH JSON and URL-encoded POST bodies
static bool parse_ssid_pass(const char *body) {
    // Try URL-encoded first: ssid=xxx&password=yyy
    const char *p = strstr(body, "ssid=");
    if (p) {
        p += 5; // skip "ssid="
        const char *end = strchr(p, '&');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > 31) len = 31;
        memcpy(s_cfg_ssid, p, len); s_cfg_ssid[len] = '\0';
        // URL decode
        for (size_t i = 0; s_cfg_ssid[i]; i++) {
            if (s_cfg_ssid[i] == '+') s_cfg_ssid[i] = ' ';
            else if (s_cfg_ssid[i] == '%' && s_cfg_ssid[i+1] && s_cfg_ssid[i+2]) {
                char hex[3] = {s_cfg_ssid[i+1], s_cfg_ssid[i+2], 0};
                s_cfg_ssid[i] = (char)strtol(hex, NULL, 16);
                memmove(s_cfg_ssid+i+1, s_cfg_ssid+i+3, strlen(s_cfg_ssid+i+3)+1);
            }
        }
        // password
        p = strstr(body, "password=");
        if (p) {
            p += 9; end = strchr(p, '&'); len = end ? (size_t)(end - p) : strlen(p);
            if (len > 63) { len = 63; }
            memcpy(s_cfg_password, p, len); s_cfg_password[len] = '\0';
        }
        return true;
    }
    // Fallback JSON: {"ssid":"x","password":"y"} — find ssid value between quotes
    p = strstr(body, "\"ssid\"");
    if (p) {
        p = strchr(p + 6, '"'); if (p) p++; // skip to value start
        const char *end = p ? strchr(p, '"') : NULL;
        size_t len = end ? (size_t)(end - p) : 0;
        if (len > 31) len = 31;
        if (p && len > 0) { memcpy(s_cfg_ssid, p, len); s_cfg_ssid[len] = '\0'; }
        // password
        p = strstr(body, "\"password\"");
        if (p) {
            p = strchr(p + 10, '"'); if (p) p++;
            end = p ? strchr(p, '"') : NULL;
            len = end ? (size_t)(end - p) : 0;
            if (len > 63) { len = 63; }
            if (p && len > 0) { memcpy(s_cfg_password, p, len); s_cfg_password[len] = '\0'; }
        }
        return true;
    }
    return false;
}

// ======================= Main flow =======================
esp_err_t wifi_provisioning_start(wifi_prov_status_cb_t status_cb) {
    s_status_cb = status_cb;
    s_evt = xEventGroupCreate();

    if (s_status_cb) s_status_cb("Init WiFi...", false);

    // Init once (safe to call multiple times — returns ERR_INVALID_STATE if already done)
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_once();

    // 尝试已保存凭据
    char saved_ssid[33] = {0}, saved_pass[65] = {0};
    if (nvs_load_creds(saved_ssid, sizeof(saved_ssid), saved_pass, sizeof(saved_pass)) == ESP_OK
        && strlen(saved_ssid) >= 2 && !strchr(saved_ssid, '"') && !strchr(saved_ssid, ':')) {
        ESP_LOGI(TAG, "Found saved: %s", saved_ssid);
        if (s_status_cb) s_status_cb("Connecting to saved WiFi...", false);
        if (wifi_try_connect(saved_ssid, saved_pass))
            return ESP_OK;
        // Failed — erase bad creds
        nvs_erase_creds();
    }

    // === SoftAP 配网模式 ===
    ESP_LOGI(TAG, "Entering SoftAP config mode");
    s_ap_mode = true;

    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_APSTA);  // APSTA: STA can scan while AP runs

    // 创建 AP netif（如果还没有）
    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_config_t ap_cfg = {};
    strncpy((char *)ap_cfg.ap.ssid, AP_SSID, sizeof(ap_cfg.ap.ssid) - 1);
    strncpy((char *)ap_cfg.ap.password, AP_PASSWORD, sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.max_connection = 2;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.ssid_len = strlen(AP_SSID);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    char hint[128];
    snprintf(hint, sizeof(hint), "Connect to %s / Pass:%s\nOpen http://192.168.4.1",
             AP_SSID, AP_PASSWORD);
    ESP_LOGI(TAG, "%s", hint);
    if (s_status_cb) s_status_cb(hint, false);

    // 启动 HTTP 配网服务器
    httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();
    httpd_cfg.lru_purge_enable = true;
    httpd_cfg.stack_size = 8192;  // wifi_do_scan needs extra stack
    httpd_handle_t httpd = NULL;
    httpd_start(&httpd, &httpd_cfg);

    httpd_uri_t uri_idx = { .uri = "/", .method = HTTP_GET, .handler = http_index_handler, .user_ctx = NULL };
    httpd_uri_t uri_scn = { .uri = "/api/scan", .method = HTTP_GET, .handler = http_scan_handler, .user_ctx = NULL };
    httpd_uri_t uri_cnn = { .uri = "/api/connect", .method = HTTP_POST, .handler = http_connect_handler, .user_ctx = NULL };
    httpd_register_uri_handler(httpd, &uri_idx);
    httpd_register_uri_handler(httpd, &uri_scn);
    httpd_register_uri_handler(httpd, &uri_cnn);

    // 等待用户通过网页提交配置
    xEventGroupWaitBits(s_evt, BIT_CONFIG_DONE, pdFALSE, pdTRUE, portMAX_DELAY);

    // 停止 HTTP 和 AP
    httpd_stop(httpd);
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(500));

    // 用新凭据连接
    s_ap_mode = false;
    if (s_status_cb) s_status_cb("Connecting to configured WiFi...", false);
    if (wifi_try_connect(s_cfg_ssid, s_cfg_password))
        return ESP_OK;

    // 如果还是失败，递归重试
    ESP_LOGE(TAG, "Connection failed after config, restarting flow");
    if (s_status_cb) s_status_cb("WiFi Failed, restart...", false);
    vTaskDelay(pdMS_TO_TICKS(3000));
    return wifi_provisioning_start(status_cb);
}

// Async: start provisioning in background task
static void prov_task(void *arg) {
    wifi_prov_status_cb_t cb = (wifi_prov_status_cb_t)arg;
    wifi_provisioning_start(cb);
    vTaskDelete(NULL);
}

void wifi_provisioning_start_async(wifi_prov_status_cb_t status_cb) {
    xTaskCreate(prov_task, "wifi_prov", 6144, (void*)status_cb, 5, NULL);
}
