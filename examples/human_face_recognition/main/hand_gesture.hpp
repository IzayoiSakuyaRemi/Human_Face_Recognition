#pragma once
#include "dl_detect_espdet_postprocessor.hpp"
#include "hand_gesture_cls_postprocessor.hpp"
#include "dl_model_base.hpp"
#include "dl_image_preprocessor.hpp"
#include <list>
#include <vector>
#include <string>

class HandGesturePipeline {
public:
    struct GestureResult {
        std::string name;
        float score;
    };

    HandGesturePipeline();
    ~HandGesturePipeline();
    std::vector<GestureResult> run(const dl::image::img_t &img);

private:
    dl::Model *m_detect_model = nullptr;
    dl::Model *m_cls_model = nullptr;
    dl::image::ImagePreprocessor *m_detect_preprocess = nullptr;
    dl::image::ImagePreprocessor *m_cls_preprocess = nullptr;
    dl::detect::ESPDetPostProcessor *m_detect_postprocess = nullptr;
    dl::cls::HandGestureClsPostprocessor *m_cls_postprocess = nullptr;
    bool m_ok = false;
};
