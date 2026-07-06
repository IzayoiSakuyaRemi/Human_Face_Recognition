/* Stub: esp-sr model_path.h — not needed on S3 (no wake word) */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { int num; } srmodel_list_t;
#define ESP_MN_PREFIX ""
#define ESP_WN_PREFIX ""
static inline srmodel_list_t *esp_srmodel_init(const char *p) { (void)p; return NULL; }
static inline char *esp_srmodel_filter(srmodel_list_t *m, const char *a, const char *b) { (void)m;(void)a;(void)b; return NULL; }
#ifdef __cplusplus
}
#endif
