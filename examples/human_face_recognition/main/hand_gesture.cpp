#include "hand_gesture.hpp"
#include "esp_log.h"

static const char *TAG = "HandGesture";

// Model paths on SD card
static const char *DETECT_MODEL_PATH = "/sdcard/models/p4/espdet_pico_224_224_hand.espdl";
static const char *CLS_MODEL_PATH    = "/sdcard/models/p4/mobilenetv2_0_5_128_128_gesture.espdl";

// Hand detect: ESPDET_PICO with 3 anchor stages
static const std::vector<dl::detect::anchor_point_stage_t> DETECT_STAGES = {{8,8,4,4}, {16,16,8,8}, {32,32,16,16}};
static const float DETECT_SCORE_THR = 0.25f;
static const float DETECT_NMS_THR   = 0.5f;

// Gesture classification
static const int   CLS_TOP_K      = 3;
static const float CLS_SCORE_THR  = 0.3f;

HandGesturePipeline::HandGesturePipeline()
{
    ESP_LOGI(TAG, "Loading hand detect model from SD...");
    m_detect_model = new dl::Model(DETECT_MODEL_PATH, 0, fbs::MODEL_LOCATION_IN_SDCARD);
    m_detect_model->minimize();

    m_detect_preprocess = new dl::image::ImagePreprocessor(m_detect_model, {0, 0, 0}, {1, 1, 1});
    m_detect_preprocess->enable_letterbox({114, 114, 114});

    m_detect_postprocess = new dl::detect::ESPDetPostProcessor(
        m_detect_model, m_detect_preprocess, DETECT_SCORE_THR, DETECT_NMS_THR, 100, DETECT_STAGES);

    ESP_LOGI(TAG, "Loading gesture cls model from SD...");
    m_cls_model = new dl::Model(CLS_MODEL_PATH, 0, fbs::MODEL_LOCATION_IN_SDCARD);
    m_cls_model->minimize();

    m_cls_preprocess = new dl::image::ImagePreprocessor(m_cls_model,
        {123.675f, 116.28f, 103.53f}, {58.395f, 57.12f, 57.375f});

    m_cls_postprocess = new dl::cls::HandGestureClsPostprocessor(m_cls_model, CLS_TOP_K, CLS_SCORE_THR, true, "output");

    m_ok = true;
    ESP_LOGI(TAG, "Hand gesture pipeline ready");
}

HandGesturePipeline::~HandGesturePipeline()
{
    delete m_cls_postprocess;
    delete m_cls_preprocess;
    delete m_detect_postprocess;
    delete m_detect_preprocess;
    delete m_cls_model;
    delete m_detect_model;
}

std::vector<HandGesturePipeline::GestureResult> HandGesturePipeline::run(const dl::image::img_t &img)
{
    std::vector<GestureResult> results;
    if (!m_ok) return results;

    m_detect_preprocess->preprocess(img);
    m_detect_model->run(m_detect_preprocess->get_model_input());
    m_detect_postprocess->postprocess();
    auto &det_results = m_detect_postprocess->get_result(img.width, img.height);

    if (det_results.empty()) return results;

    std::vector<dl::cls::result_t> cls_results;
    for (auto &d : det_results) {
        std::vector<int> crop = {d.box[0], d.box[1], d.box[2], d.box[3]};
        m_cls_preprocess->preprocess(img, crop);
        m_cls_model->run(m_cls_preprocess->get_model_input());
        auto &r = m_cls_postprocess->postprocess();
        cls_results.insert(cls_results.end(), r.begin(), r.end());
    }

    if (cls_results.empty()) return results;

    GestureResult gr;
    gr.name  = cls_results[0].cat_name;
    gr.score = cls_results[0].score;
    results.push_back(gr);
    return results;
}
