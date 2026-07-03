#pragma once
#include "hand_detect.hpp"
#include "hand_gesture_recognition.hpp"
#include "who_frame_cap.hpp"
#include "who_task.hpp"
#include <functional>
#include <string>

namespace who {
namespace gesture {

using hand_gesture_result_cb_t = std::function<void(const std::string &gesture_name, float score)>;

class WhoHandGestureTask : public task::WhoTask {
public:
    WhoHandGestureTask(const std::string &name,
                       frame_cap::WhoFrameCapNode *frame_cap_node,
                       HandDetect *hand_detect,
                       HandGestureRecognizer *gesture_recognizer);
    ~WhoHandGestureTask();

    void set_result_cb(const hand_gesture_result_cb_t &cb);
    void set_fps(float fps);

    bool run(const configSTACK_DEPTH_TYPE uxStackDepth,
             UBaseType_t uxPriority,
             const BaseType_t xCoreID) override;

private:
    void task() override;

    frame_cap::WhoFrameCapNode *m_frame_cap_node;
    HandDetect *m_hand_detect;
    HandGestureRecognizer *m_gesture_recognizer;
    hand_gesture_result_cb_t m_result_cb;
    TickType_t m_interval;
};

} // namespace gesture
} // namespace who
