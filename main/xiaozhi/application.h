/* Application shim for S3 */
#pragma once
#include <functional>
#include <string>
#include "device_state.h"
class Application {
public:
    static Application& GetInstance() { static Application a; return a; }
    DeviceState GetDeviceState() const { return kDeviceStateIdle; }
    void Schedule(std::function<void()>&& cb) { cb(); }
    void SetDeviceState(DeviceState) {}
};
