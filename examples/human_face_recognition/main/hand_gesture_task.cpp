#include "hand_gesture_task.hpp"

extern SemaphoreHandle_t g_espdl_mutex;

static const char *TAG = "HandGesture";

namespace who {
namespace gesture {

WhoHandGestureTask::WhoHandGestureTask(const std::string &name,
                                       frame_cap::WhoFrameCapNode *frame_cap_node,
                                       HandDetect *hand_detect,
                                       HandGestureRecognizer *gesture_recognizer)
    : task::WhoTask(name),
      m_frame_cap_node(frame_cap_node),
      m_hand_detect(hand_detect),
      m_gesture_recognizer(gesture_recognizer),
      m_interval(0)
{
    m_frame_cap_node->add_new_frame_signal_subscriber(this);
}

WhoHandGestureTask::~WhoHandGestureTask()
{
    delete m_hand_detect;
    delete m_gesture_recognizer;
}

void WhoHandGestureTask::set_result_cb(const hand_gesture_result_cb_t &cb)
{
    m_result_cb = cb;
}

void WhoHandGestureTask::set_fps(float fps)
{
    if (fps > 0) {
        m_interval = pdMS_TO_TICKS((int)(1000.f / fps));
    }
}

void WhoHandGestureTask::task()
{
    TickType_t last_wake_time = xTaskGetTickCount();

    while (true) {
        EventBits_t event_bits = xEventGroupWaitBits(
            m_event_group,
            frame_cap::WhoFrameCapNode::NEW_FRAME | TASK_PAUSE | TASK_STOP,
            pdTRUE, pdFALSE, portMAX_DELAY);

        if (event_bits & TASK_STOP) {
            break;
        } else if (event_bits & TASK_PAUSE) {
            xEventGroupSetBits(m_event_group, TASK_PAUSED);
            EventBits_t pause_bits = xEventGroupWaitBits(
                m_event_group, TASK_RESUME | TASK_STOP, pdTRUE, pdFALSE, portMAX_DELAY);
            if (pause_bits & TASK_STOP) {
                break;
            }
            last_wake_time = xTaskGetTickCount();
            continue;
        }

        // NEW_FRAME received
        auto fb = m_frame_cap_node->cam_fb_peek();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        dl::image::img_t img = static_cast<dl::image::img_t>(*fb);

        // Acquire shared DL mutex (voice task also uses it)
        if (!xSemaphoreTake(g_espdl_mutex, pdMS_TO_TICKS(50))) {
            continue;
        }

        // Step 1: Detect hands
        auto &det_result = m_hand_detect->run(img);

        // Step 2: Classify gesture
        std::vector<dl::cls::result_t> cls_results;
        if (!det_result.empty()) {
            cls_results = m_gesture_recognizer->recognize(img, det_result);
        }

        xSemaphoreGive(g_espdl_mutex);

        // Step 3: Report best result
        if (m_result_cb) {
            if (!cls_results.empty()) {
                const auto &best = cls_results[0];
                m_result_cb(best.cat_name, best.score);
            } else {
                m_result_cb("", 0.0f);
            }
        }

        // Throttle
        if (m_interval) {
            vTaskDelayUntil(&last_wake_time, m_interval);
        }
    }

    xEventGroupSetBits(m_event_group, TASK_STOPPED);
    vTaskDelete(NULL);
}

bool WhoHandGestureTask::run(const configSTACK_DEPTH_TYPE uxStackDepth,
                              UBaseType_t uxPriority,
                              const BaseType_t xCoreID)
{
    return task::WhoTask::run(uxStackDepth, uxPriority, xCoreID);
}

} // namespace gesture
} // namespace who
