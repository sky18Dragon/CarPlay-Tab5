#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t tab5_video_init(void);
void tab5_video_start(uint64_t generation);
void tab5_video_stop(void);
bool tab5_video_submit(uint64_t generation,const uint8_t *annex_b,size_t size,uint64_t pts);
bool tab5_video_needs_idr(void);
#ifdef __cplusplus
}
#endif
