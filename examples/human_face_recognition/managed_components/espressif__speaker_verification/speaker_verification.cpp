#include "speaker_verification.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <math.h>

static const char *TAG = "speaker_verification";

// Log heap + timestamp at each construction step (diagnosing 30s hang + abort)
#define SV_STEP(msg) ESP_LOGI(TAG, "[%.1fs] %s | DRAM largest=%u KB, PSRAM largest=%u KB", \
    esp_timer_get_time() / 1000000.0, msg, \
    (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024), \
    (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024))

#if CONFIG_SPEAKER_VERIFICATION_MODEL_IN_FLASH_RODATA
extern const uint8_t sv_model_3s_espdl[] asm("_binary_sv_tdnn_tiny_3s_espdl_start");
extern const uint8_t sv_model_6s_espdl[] asm("_binary_sv_tdnn_tiny_6s_espdl_start");
#elif CONFIG_SPEAKER_VERIFICATION_MODEL_IN_FLASH_PARTITION
// Partition labels are selected at runtime based on target_seconds.
#else
#if !defined(CONFIG_BSP_SD_MOUNT_POINT)
#define CONFIG_BSP_SD_MOUNT_POINT "/sdcard"
#endif
#endif

SpeakerVerification::SpeakerVerification(int target_sec) : target_seconds(target_sec)
{
    // Only the 3s and 6s models are shipped; anything else falls back to 6s.
    if (target_seconds != 3 && target_seconds != 6) {
        ESP_LOGE(TAG, "Unsupported target_seconds=%d; only 3 or 6 are supported. Falling back to 6.", target_seconds);
        target_seconds = 6;
    }

#if !CONFIG_SPEAKER_VERIFICATION_MODEL_IN_SDCARD
#if CONFIG_SPEAKER_VERIFICATION_MODEL_IN_FLASH_RODATA
    const char *path = (const char *)(target_seconds == 3 ? sv_model_3s_espdl : sv_model_6s_espdl);
#else // CONFIG_SPEAKER_VERIFICATION_MODEL_IN_FLASH_PARTITION
    const char *path = "sv_model_3s";  // partition label (holds 3s or 6s .espdl)
#endif
    model = new dl::Model(path, static_cast<fbs::model_location_type_t>(CONFIG_SPEAKER_VERIFICATION_MODEL_LOCATION));
#else
    char sd_path[256];
    snprintf(sd_path,
             sizeof(sd_path),
             "%s/%s/%s",
             CONFIG_BSP_SD_MOUNT_POINT,
             CONFIG_SPEAKER_VERIFICATION_MODEL_SDCARD_DIR,
             target_seconds == 3 ? "sv_lite_p4.espdl" : "sv_lite_p4.espdl");
    SV_STEP("pre-load: reading .espdl from SD to PSRAM");
    // FAST PATH: read the whole .espdl into PSRAM ourselves with chunked fread,
    // then hand the memory pointer to dl::Model. Verified reads (see below) guard
    // against silent SD corruption (SPI mode has no data CRC).
    FILE *f = fopen(sd_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", sd_path);
        model = nullptr;
        fbank = nullptr;
        return;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fclose(f);
    uint8_t *model_data = (uint8_t *)heap_caps_aligned_alloc(16, fsize, MALLOC_CAP_SPIRAM);
    if (!model_data) {
        ESP_LOGE(TAG, "Cannot alloc %ld bytes PSRAM for model", fsize);
        model = nullptr;
        fbank = nullptr;
        return;
    }
    const size_t CHUNK = 16 * 1024;

    // ── INTERNAL RAM staging buffer ─────────────────────
    // KEY FIX: fread destination must be INTERNAL SRAM, not PSRAM.
    // SDSPI DMA→PSRAM writes corrupt randomly under heavy PSRAM contention
    // (LVGL 73MB/s + camera 37MB/s). DMA→internal SRAM is always reliable;
    // CPU memcpy internal→PSRAM is cache-coherent.
    uint8_t *stage = (uint8_t *)heap_caps_malloc(CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!stage) {
        ESP_LOGE(TAG, "Cannot alloc %u bytes internal staging buffer", (unsigned)CHUNK);
        heap_caps_free(model_data);
        model = nullptr;
        fbank = nullptr;
        return;
    }

    // ── Data integrity: read + verify, retry up to 3 rounds ──
    static const uint8_t expect_hdr[16] = {
        0x45,0x44,0x4c,0x32, 0x00,0x00,0x00,0x00, 0x60,0x67,0x2b,0x00, 0x00,0x00,0x00,0x00 };
    bool verified = false;
    for (int attempt = 1; attempt <= 3 && !verified; attempt++) {
        // Pass 1: read whole file via internal staging → memcpy to PSRAM
        f = fopen(sd_path, "rb");
        if (!f) break;
        setvbuf(f, NULL, _IONBF, 0);  // no newlib buffering — direct chunk reads
        size_t got2 = 0;
        size_t n1;
        while (got2 < (size_t)fsize &&
               (n1 = fread(stage, 1,
                           ((size_t)fsize - got2) < CHUNK ? ((size_t)fsize - got2) : CHUNK, f)) > 0) {
            memcpy(model_data + got2, stage, n1);
            got2 += n1;
        }
        fclose(f);
        if (got2 != (size_t)fsize) { ESP_LOGW(TAG, "Attempt %d: short read", attempt); continue; }

        bool hdr_ok = (target_seconds != 3) || (memcmp(model_data, expect_hdr, 8) == 0);
        uint32_t sum1 = 0;
        for (long i = 0; i < fsize; i++) sum1 += model_data[i];

        // Pass 2: re-read into rolling checksum only (internal staging too)
        f = fopen(sd_path, "rb");
        uint32_t sum2 = 0;
        if (f) {
            setvbuf(f, NULL, _IONBF, 0);
            size_t n2;
            while ((n2 = fread(stage, 1, CHUNK, f)) > 0)
                for (size_t i = 0; i < n2; i++) sum2 += stage[i];
            fclose(f);
        }
        SV_STEP("verify round done");
        ESP_LOGI(TAG, "Attempt %d: hdr=%s sum1=0x%08lx sum2=0x%08lx %s",
                 attempt, hdr_ok ? "OK" : "BAD",
                 (unsigned long)sum1, (unsigned long)sum2,
                 (hdr_ok && sum1 == sum2) ? "VERIFIED" : "MISMATCH, retrying...");
        verified = hdr_ok && (sum1 == sum2);
    }
    heap_caps_free(stage);
    if (!verified) {
        ESP_LOGE(TAG, "Model data unstable after 3 attempts — aborting SV init (no crash)");
        heap_caps_free(model_data);
        model = nullptr;
        fbank = nullptr;
        return;
    }
    // ── End integrity check ─────────────────────────────
    // Construct model from memory pointer (RODATA semantics — no further SD I/O).
    // model_data stays allocated for the model's lifetime.
    SV_STEP("dl::Model construct start");
    model = new dl::Model((const char *)model_data, fbs::MODEL_LOCATION_IN_FLASH_RODATA);
    SV_STEP("dl::Model construct done");
#endif

    SV_STEP("minimize start");
    model->minimize();
    SV_STEP("minimize done");

    dl::audio::SpeechFeatureConfig config;
    config.sample_rate = 16000;
    config.num_mel_bins = 80;
    config.frame_length = 25.0f;
    config.frame_shift = 10.0f;
    config.window_type = dl::audio::WinType::HAMMING;
    config.use_energy = false;
    config.use_log_fbank = 1;
    config.low_freq = 0.0f;
    config.high_freq = 0.0f;
    // ── Match TinyModel training (torchaudio MelSpectrogram) ──
    config.preemphasis = 0.0f;
    config.remove_dc_offset = false;

    SV_STEP("Fbank construct start");
    fbank = new dl::audio::Fbank(config);
    SV_STEP("Fbank construct done");

    feature_dim = config.num_mel_bins;
    // Derive the frame count from the loaded model's input so the audio is always
    // cropped/padded to exactly the model window.
    const int frame_shift_samples = (int)(config.frame_shift * config.sample_rate / 1000);   // 160
    const int frame_length_samples = (int)(config.frame_length * config.sample_rate / 1000); // 400
    dl::TensorBase *input_tensor = model->get_inputs().begin()->second;
    num_frames = input_tensor->size / feature_dim;
    target_samples = (num_frames - 1) * frame_shift_samples + frame_length_samples;
    embedding_dim = model->get_outputs().begin()->second->size;
    audio_buffer = (float *)malloc(sizeof(float) * target_samples);
    features_buffer = (float *)malloc(sizeof(float) * num_frames * feature_dim);
    SV_STEP("constructor complete");
}

SpeakerVerification::~SpeakerVerification()
{
    free(audio_buffer);
    free(features_buffer);
    delete fbank;
    delete model;
}

void SpeakerVerification::normalize_audio(const int16_t *src, int src_len)
{
    // RIGHT-aligned crop: the ring buffer captures audio from oldest to newest.
    // The user's speech is at the END of the buffer (most recent) — taking the
    // first part captures silence or incomplete speech, causing inconsistent
    // embeddings for the same person. Training used random crops so the model
    // is robust to offset; inference must use the most recent speech.
    // Right-pad with zeros if too short, and scale int16 -> [-1, 1).
    if (src_len < target_samples) {
        for (int i = 0; i < target_samples; i++) audio_buffer[i] = (i < src_len) ? src[i] / 32768.0f : 0.0f;
    } else {
        int start = src_len - target_samples;  // take the LAST target_samples
        for (int i = 0; i < target_samples; i++) audio_buffer[i] = src[start + i] / 32768.0f;
    }
}

bool SpeakerVerification::preprocess(const uint8_t *wav_start, size_t wav_len)
{
    dl::audio::dl_audio_t *audio = dl::audio::decode_wav(wav_start, wav_len);
    if (!audio) {
        ESP_LOGE(TAG, "Failed to decode WAV.");
        return false;
    }
    normalize_audio(audio->data, audio->length);
    free(audio->data);
    free(audio);

    extract_features();
    return true;
}

bool SpeakerVerification::preprocess(const int16_t *samples, size_t num_samples)
{
    if (!samples || num_samples == 0) {
        ESP_LOGE(TAG, "Invalid PCM input.");
        return false;
    }
    normalize_audio(samples, (int)num_samples);
    extract_features();
    return true;
}

void SpeakerVerification::extract_features()
{
    // FBank
    auto input_tensor = model->get_inputs().begin()->second;
    fbank->process(audio_buffer, target_samples, features_buffer);

    // CMVN: mean-only subtraction (matching training featurizer.py).
    // Training uses: feature = feature - feature.mean(1, keepdim=True)
    // NO variance normalization — training does NOT divide by std.
    for (int d = 0; d < feature_dim; d++) {
        float mean = 0.0f;
        for (int t = 0; t < num_frames; t++) mean += features_buffer[t * feature_dim + d];
        mean /= num_frames;
        for (int t = 0; t < num_frames; t++) features_buffer[t * feature_dim + d] -= mean;
    }

    int in_dim = input_tensor->size;
    int16_t *quantized_input = (int16_t *)input_tensor->data;
    for (int i = 0; i < in_dim; i++)
        quantized_input[i] = dl::quantize<int16_t>(features_buffer[i], DL_RESCALE(input_tensor->exponent));
}

float *SpeakerVerification::run_model()
{
    model->run();

    dl::TensorBase *output_tensor = model->get_outputs().begin()->second;

    float *embedding = (float *)malloc(sizeof(float) * embedding_dim);
    if (!embedding) {
        ESP_LOGE(TAG, "Failed to allocate embedding buffer.");
        return nullptr;
    }

    int8_t *ptr = (int8_t *)output_tensor->data;
    float l2 = 0.0f;
    for (int i = 0; i < embedding_dim; i++) {
        embedding[i] = dl::dequantize(ptr[i], DL_SCALE(output_tensor->exponent));
        l2 += embedding[i] * embedding[i];
    }
    l2 = sqrtf(l2);
    if (l2 < 1e-10f) l2 = 1e-10f;
    for (int i = 0; i < embedding_dim; i++) embedding[i] /= l2;

    return embedding;
}

float *SpeakerVerification::run(const uint8_t *wav_start, size_t wav_len)
{
    if (!model || !fbank) {
        ESP_LOGE(TAG, "Model not loaded.");
        return nullptr;
    }
    if (!preprocess(wav_start, wav_len))
        return nullptr;
    return run_model();
}

float *SpeakerVerification::run(const int16_t *samples, size_t num_samples)
{
    if (!model || !fbank) {
        ESP_LOGE(TAG, "Model not loaded.");
        return nullptr;
    }
    if (!preprocess(samples, num_samples))
        return nullptr;
    return run_model();
}

float SpeakerVerification::compute_similarity(const float *e1, const float *e2)
{
    if (!e1 || !e2) {
        ESP_LOGE(TAG, "Invalid embedding.");
        return 0.0f;
    }

    float dot = 0.0f;
    float norm1 = 0.0f;
    float norm2 = 0.0f;

    for (int i = 0; i < embedding_dim; i++) {
        dot += e1[i] * e2[i];
        norm1 += e1[i] * e1[i];
        norm2 += e2[i] * e2[i];
    }

    if (norm1 == 0.0f || norm2 == 0.0f)
        return 0.0f;

    return dot / (sqrtf(norm1) * sqrtf(norm2));
}
