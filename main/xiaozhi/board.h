/* Board shim for S3 — provides NetworkInterface using esp-ml307 + ESP-IDF WiFi */
#pragma once
#include <string>
#include <memory>
#include <functional>
#include <esp_timer.h>
#include "network_interface.h"  /* local stub — replaces esp-ml307 */
#include "display.h"
#include "led/led.h"
#include "backlight.h"
#include "camera.h"

#define BOARD_NAME "WT99P4C5-S1-S3"
#define OPUS_FRAME_DURATION_MS 60
#define CONFIG_OTA_URL "https://api.tenclass.net/xiaozhi/ota/"

class AudioCodec;
class Board {
public:
    static Board& GetInstance() { static Board b; return b; }
    NetworkInterface* GetNetwork() { return net_.get(); }
    AudioCodec* GetAudioCodec();
    std::string GetUuid();
    Display* GetDisplay() { return &display_; }
    Led* GetLed() { return &led_; }
    Backlight* GetBacklight() { return nullptr; }
    Camera* GetCamera() { return nullptr; }
    std::string GetBoardType() { return "S3-XiaoZhi"; }
    void SetNetworkEventCallback(std::function<void(int,const std::string&)> cb) {}
    void SetPowerSaveLevel(int) {}
    std::string GetDeviceStatusJson() { return "{}"; }
    std::string GetBoardJson() { return "{}"; }
    std::string GetSystemInfoJson() { return "{}"; }
private:
    Board();
    std::unique_ptr<NetworkInterface> net_;
    NoDisplay display_;
    NoLed led_;
};
