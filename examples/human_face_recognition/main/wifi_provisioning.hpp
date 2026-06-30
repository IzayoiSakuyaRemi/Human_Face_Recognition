#pragma once
#include <esp_err.h>
#include <string>

/**
 * WiFi 配网状态回调
 * @param status  状态字符串，用于 LVGL 标签显示
 * @param done    配网完成 (WiFi 已连接) 时为 true
 */
typedef void (*wifi_prov_status_cb_t)(const char *status, bool done);

/**
 * 启动 WiFi 配网流程 (模仿小智)
 *
 * 流程:
 *  1. 检查 NVS 是否有已保存的 WiFi 凭据
 *  2. 有凭据 → 尝试连接 (60s 超时)
 *  3. 无凭据或超时 → 启动 SoftAP + HTTP 配网页
 *  4. 用户在手机浏览器中输入 AP 地址配置 WiFi
 *  5. 连接成功后返回
 *
 * @param status_cb  状态回调，用于更新 LVGL / 串口日志
 * @return ESP_OK on success
 */
esp_err_t wifi_provisioning_start(wifi_prov_status_cb_t status_cb);
