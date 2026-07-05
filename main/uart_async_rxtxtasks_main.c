/* S3 CSI Radar — UART1 bridge + WiFi provisioning + esp_console  Step 1 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_console.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── UART1 (to P4) ────────────────────────── */
#define TXD_PIN     GPIO_NUM_17
#define RXD_PIN     GPIO_NUM_18
#define UART_BAUD   921600
#define RX_BUF_SIZE 2048

/* ── WiFi ─────────────────────────────────── */
#define WIFI_CHANNEL    11
#define WIFI_MAX_RETRY  5
#define AP_SSID        "S3-Config"
#define AP_CHANNEL      11

static const char *TAG = "s3";
static EventGroupHandle_t s_wifi_evt;
#define WIFI_CONNECTED  BIT0
#define WIFI_FAIL       BIT1
#define WIFI_SCAN_DONE  BIT2
#define WIFI_CFG_DONE   BIT3

static int  s_retry = 0;
static bool g_wifi_ok = false;
static bool g_ap_mode = false;
static bool g_wifi_inited = false;
static esp_netif_t *g_ap_netif = NULL;
static httpd_handle_t g_httpd = NULL;

static char g_ssid[33] = {0};
static char g_pass[65] = {0};

/* scan cache for web portal */
static struct { char ssid[33]; int rssi; } g_aps[20];
static int g_ap_count = 0;
static char g_cfg_ssid[33] = {0};
static char g_cfg_pass[65] = {0};

/* ── UART1 ─────────────────────────────────── */
static void uart_init(void)
{
    uart_config_t c = { .baud_rate = UART_BAUD, .data_bits = UART_DATA_8_BITS,
                        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
                        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT };
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE, 1024, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &c);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void rx_task(void *arg)
{
    uint8_t *d = malloc(RX_BUF_SIZE + 1);
    while (1) {
        int r = uart_read_bytes(UART_NUM_1, d, RX_BUF_SIZE, pdMS_TO_TICKS(500));
        if (r > 0) { d[r] = 0; (void)d; }
    }
}

/* ── WiFi event handler ────────────────────── */
static void wifi_evt(void *a, esp_event_base_t b, int32_t id, void *d)
{
    if (b == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (!g_ap_mode) esp_wifi_connect();
    }
    else if (b == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!g_ap_mode) {
            g_wifi_ok = false;
            if (s_retry < WIFI_MAX_RETRY) { esp_wifi_connect(); s_retry++; }
            else xEventGroupSetBits(s_wifi_evt, WIFI_FAIL);
        }
    }
    else if (b == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        uint16_t num = 20;
        wifi_ap_record_t aps[20];
        esp_wifi_scan_get_ap_records(&num, aps);
        g_ap_count = (num > 20) ? 20 : num;
        for (int i = 0; i < g_ap_count; i++) {
            strncpy(g_aps[i].ssid, (const char *)aps[i].ssid, 32);
            g_aps[i].ssid[32] = '\0';
            g_aps[i].rssi = aps[i].rssi;
        }
        xEventGroupSetBits(s_wifi_evt, WIFI_SCAN_DONE);
    }
    else if (b == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)d;
        ESP_LOGI(TAG, "WiFi OK: " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0; g_wifi_ok = true;
        xEventGroupSetBits(s_wifi_evt, WIFI_CONNECTED);
    }
}

/* ── Idempotent WiFi init ─────────────────── */
static esp_err_t ensure_wifi(void)
{
    if (g_wifi_inited) return ESP_OK;
    if (!s_wifi_evt) s_wifi_evt = xEventGroupCreate();
    esp_netif_init(); esp_event_loop_create_default(); esp_netif_create_default_wifi_sta();
    wifi_init_config_t c = WIFI_INIT_CONFIG_DEFAULT(); esp_wifi_init(&c);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL);
    g_wifi_inited = true;
    return ESP_OK;
}

/* ── STA connect ───────────────────────────── */
static void wifi_connect(const char *ssid, const char *pass)
{
    ensure_wifi();
    if (!s_wifi_evt) s_wifi_evt = xEventGroupCreate();
    g_ap_mode = false;
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t wc = { .sta = { .threshold.authmode = WIFI_AUTH_WPA2_PSK } };
    strncpy((char*)wc.sta.ssid, ssid, sizeof(wc.sta.ssid)-1);
    strncpy((char*)wc.sta.password, pass, sizeof(wc.sta.password)-1);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_BELOW);
    esp_wifi_set_ps(WIFI_PS_NONE);
    xEventGroupClearBits(s_wifi_evt, WIFI_CONNECTED|WIFI_FAIL);
    s_retry = 0;
    esp_wifi_start();
    ESP_LOGI(TAG, "Connecting to %s...", ssid);
}

/* ── NVS helpers ───────────────────────────── */
static void nvs_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open("wifi_prov", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid", ssid); nvs_set_str(h, "pass", pass);
        nvs_commit(h); nvs_close(h);
    }
}

static bool nvs_load(char *ssid, size_t sl, char *pass, size_t pl)
{
    nvs_handle_t h;
    if (nvs_open("wifi_prov", NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t r = nvs_get_str(h, "ssid", ssid, &sl);
    if (r != ESP_OK) { nvs_close(h); return false; }
    r = nvs_get_str(h, "pass", pass, &pl);
    nvs_close(h);
    return (r == ESP_OK && strlen(ssid) >= 2);
}

/* ── HTML page ─────────────────────────────── */
static const char *HTML_PAGE = R"raw(<!DOCTYPE html>
<html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>S3 WiFi Setup</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee;display:flex;justify-content:center;align-items:center;min-height:100vh;padding:16px}
.c{background:#16213e;border-radius:12px;padding:28px 22px;max-width:380px;width:100%;box-shadow:0 6px 24px rgba(0,0,0,.35)}
h2{text-align:center;color:#e94560;margin:0 0 22px;font-size:20px}
label{display:block;margin:14px 0 5px;color:#a0a0b0;font-size:13px}
select,input{width:100%;padding:10px;border:1px solid #0f3460;border-radius:7px;background:#1a1a2e;color:#eee;font-size:14px}
select:focus,input:focus{outline:none;border-color:#e94560}
button{width:100%;padding:12px;border:none;border-radius:7px;background:#e94560;color:#fff;font-size:15px;font-weight:bold;cursor:pointer;margin-top:20px}
button.s{background:#0f3460;margin-top:8px}
#status{margin-top:14px;text-align:center;font-size:13px;color:#a0a0b0}
</style></head><body>
<div class="c"><h2>S3 WiFi Setup</h2>
<label>WiFi Network</label>
<select id="ssid"><option value="">Tap Scan first...</option></select>
<button class="s" onclick="scan()">Scan Nearby Networks</button>
<label>Password</label>
<input type="password" id="pass" placeholder="Enter WiFi password">
<button onclick="connect()">Connect</button>
<div id="status"></div></div>
<script>
function $(id){return document.getElementById(id)}
function show(m){$('status').textContent=m}
async function scan(){show('Scanning...');try{let r=await fetch('/api/scan'),j=await r.json(),s=$('ssid');s.innerHTML='';j.aps.forEach(a=>{let o=document.createElement('option');o.value=a.ssid;o.textContent=a.ssid+'  ('+a.rssi+' dBm)';s.appendChild(o)});show(j.aps.length+' networks found')}catch(e){show('Scan failed')}}
async function connect(){let x=$('ssid').value,y=$('pass').value;if(!x){show('Select a network first');return}show('Connecting...');try{let r=await fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(x)+'&password='+encodeURIComponent(y)}),j=await r.json();show(j.status=='ok'?'Saved! Connecting...':'Error')}catch(e){show('Request failed')}}
</script></body></html>)raw";

/* ── HTTP handlers ─────────────────────────── */
static esp_err_t http_idx(httpd_req_t *r) {
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache"); httpd_resp_set_type(r, "text/html");
    httpd_resp_send(r, HTML_PAGE, strlen(HTML_PAGE)); return ESP_OK;
}

static esp_err_t http_scan(httpd_req_t *r) {
    g_ap_count = 0; memset(g_aps, 0, sizeof(g_aps));
    xEventGroupClearBits(s_wifi_evt, WIFI_SCAN_DONE);
    esp_wifi_scan_start(NULL, false);
    xEventGroupWaitBits(s_wifi_evt, WIFI_SCAN_DONE, pdTRUE, pdTRUE, pdMS_TO_TICKS(5000));
    char j[4096]; int o = snprintf(j, sizeof(j), "{\"aps\":[");
    for (int i = 0; i < g_ap_count; i++)
        o += snprintf(j+o, sizeof(j)-o, "%s{\"ssid\":\"%s\",\"rssi\":%d}", i?",":"", g_aps[i].ssid, g_aps[i].rssi);
    o += snprintf(j+o, sizeof(j)-o, "]}");
    httpd_resp_set_type(r, "application/json"); httpd_resp_send(r, j, o); return ESP_OK;
}

static esp_err_t http_connect(httpd_req_t *r) {
    char buf[512]; int len = httpd_req_recv(r, buf, sizeof(buf)-1);
    if (len <= 0) { httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Empty"); return ESP_FAIL; }
    buf[len] = 0; memset(g_cfg_ssid, 0, 33); memset(g_cfg_pass, 0, 65);
    const char *p = strstr(buf, "ssid="); if (!p) goto err;
    p += 5; const char *e = strchr(p, '&'); size_t n = e ? (size_t)(e-p) : strlen(p);
    if (n > 32) { n = 32; }
    memcpy(g_cfg_ssid, p, n);
    for (size_t i = 0; g_cfg_ssid[i]; i++) {
        if (g_cfg_ssid[i] == '+') g_cfg_ssid[i] = ' ';
        else if (g_cfg_ssid[i] == '%' && g_cfg_ssid[i+1] && g_cfg_ssid[i+2])
        { char h[3]={g_cfg_ssid[i+1],g_cfg_ssid[i+2],0}; g_cfg_ssid[i]=(char)strtol(h,NULL,16);
          memmove(g_cfg_ssid+i+1,g_cfg_ssid+i+3,strlen(g_cfg_ssid+i+3)+1); }
    }
    p = strstr(buf, "password="); if (p) {
        p += 9; e = strchr(p, '&'); n = e ? (size_t)(e-p) : strlen(p);
        if (n > 63) { n = 63; }
        memcpy(g_cfg_pass, p, n);
        for (size_t i = 0; g_cfg_pass[i]; i++) {
            if (g_cfg_pass[i] == '+') g_cfg_pass[i] = ' ';
            else if (g_cfg_pass[i] == '%' && g_cfg_pass[i+1] && g_cfg_pass[i+2])
            { char h[3]={g_cfg_pass[i+1],g_cfg_pass[i+2],0}; g_cfg_pass[i]=(char)strtol(h,NULL,16);
              memmove(g_cfg_pass+i+1,g_cfg_pass+i+3,strlen(g_cfg_pass+i+3)+1); }
        }
    }
    if (!g_cfg_ssid[0]) goto err;
    ESP_LOGI(TAG, "Web: SSID=%s", g_cfg_ssid);
    nvs_save(g_cfg_ssid, g_cfg_pass);
    httpd_resp_set_type(r, "application/json"); httpd_resp_sendstr(r, "{\"status\":\"ok\"}");
    xEventGroupSetBits(s_wifi_evt, WIFI_CFG_DONE); return ESP_OK;
err: httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Bad request"); return ESP_FAIL;
}

/* ── Console commands ──────────────────────── */
static int cmd_scan(int argc, char **argv)
{
    ensure_wifi();
    printf("Scanning...\n"); g_ap_count = 0;
    xEventGroupClearBits(s_wifi_evt, WIFI_SCAN_DONE);
    esp_wifi_scan_start(NULL, true);
    xEventGroupWaitBits(s_wifi_evt, WIFI_SCAN_DONE, pdTRUE, pdTRUE, pdMS_TO_TICKS(5000));
    printf("%3s %-33s %4s\n", "#", "SSID", "RSSI");
    for (int i = 0; i < g_ap_count; i++)
        printf("%3d %-33s %4d\n", i+1, g_aps[i].ssid, g_aps[i].rssi);
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc < 3) { printf("Usage: wifi <ssid> <password>\n"); return 1; }
    strncpy(g_ssid, argv[1], 32); strncpy(g_pass, argv[2], 64);
    wifi_connect(g_ssid, g_pass); return 0;
}

static int cmd_ap(int argc, char **argv)
{
    ensure_wifi();
    if (!s_wifi_evt) s_wifi_evt = xEventGroupCreate();
    g_ap_mode = true;
    if (!g_ap_netif) g_ap_netif = esp_netif_create_default_wifi_ap();
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    wifi_config_t ac = {0};
    strncpy((char*)ac.ap.ssid, AP_SSID, sizeof(ac.ap.ssid)-1);
    ac.ap.ssid_len = strlen(AP_SSID); ac.ap.max_connection = 3;
    ac.ap.authmode = WIFI_AUTH_OPEN; ac.ap.channel = AP_CHANNEL;
    esp_wifi_set_config(WIFI_IF_AP, &ac); esp_wifi_start();
    printf("\nAP: %s  →  http://192.168.4.1\n", AP_SSID);

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG(); hc.lru_purge_enable = true; hc.stack_size = 8192;
    httpd_start(&g_httpd, &hc);
    httpd_uri_t u1 = {.uri="/", .method=HTTP_GET, .handler=http_idx};
    httpd_uri_t u2 = {.uri="/api/scan", .method=HTTP_GET, .handler=http_scan};
    httpd_uri_t u3 = {.uri="/api/connect", .method=HTTP_POST, .handler=http_connect};
    httpd_register_uri_handler(g_httpd, &u1);
    httpd_register_uri_handler(g_httpd, &u2);
    httpd_register_uri_handler(g_httpd, &u3);

    xEventGroupClearBits(s_wifi_evt, WIFI_CFG_DONE);
    xEventGroupWaitBits(s_wifi_evt, WIFI_CFG_DONE, pdFALSE, pdTRUE, portMAX_DELAY);

    httpd_stop(g_httpd); g_httpd = NULL; esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(500));

    g_ap_mode = false;
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t sc = { .sta = { .threshold.authmode = WIFI_AUTH_WPA2_PSK } };
    strncpy((char*)sc.sta.ssid, g_cfg_ssid, sizeof(sc.sta.ssid)-1);
    strncpy((char*)sc.sta.password, g_cfg_pass, sizeof(sc.sta.password)-1);
    esp_wifi_set_config(WIFI_IF_STA, &sc);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_BELOW);
    esp_wifi_set_ps(WIFI_PS_NONE);
    xEventGroupClearBits(s_wifi_evt, WIFI_CONNECTED|WIFI_FAIL);
    s_retry = 0; esp_wifi_start();
    printf("Connecting...\n");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_evt, WIFI_CONNECTED|WIFI_FAIL,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    if (bits & WIFI_CONNECTED) {
        strncpy(g_ssid, g_cfg_ssid, 32); strncpy(g_pass, g_cfg_pass, 64);
        printf("WiFi connected!\n"); return 0;
    }
    printf("WiFi connection failed\n"); return 1;
}

static void register_commands(void)
{
    esp_console_cmd_t cmds[] = {
        {.command="scan", .help="Scan WiFi", .func=&cmd_scan},
        {.command="wifi", .help="Connect: wifi <ssid> <pass>", .func=&cmd_wifi},
        {.command="ap",   .help="Start AP web portal", .func=&cmd_ap},
    };
    for (int i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++)
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
}

/* ── MAIN ─────────────────────────────────── */
void app_main(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND)
    { nvs_flash_erase(); nvs_flash_init(); }

    uart_init();
    ESP_LOGI(TAG, "UART1 ready: TX=%d RX=%d baud=%d", TXD_PIN, RXD_PIN, UART_BAUD);
    xTaskCreate(rx_task, "rx", 4096, NULL, 5, NULL);

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uc = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    rc.prompt = "s3> "; rc.task_stack_size = 4096; rc.task_priority = 5;
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uc, &rc, &repl));
    register_commands();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    /* Auto-connect if saved credentials exist */
    char saved_ssid[33]={0}, saved_pass[65]={0};
    if (nvs_load(saved_ssid, sizeof(saved_ssid), saved_pass, sizeof(saved_pass))) {
        ESP_LOGI(TAG, "Auto-connecting to saved: %s", saved_ssid);
        wifi_connect(saved_ssid, saved_pass);
    }
}
