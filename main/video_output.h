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
/* While paused (the Settings page covers the picture) frames are accepted and
 * dropped without decoding, so the page gets the CPU. Resuming waits for the
 * next keyframe. */
void tab5_video_pause(bool paused);
/* Row sharpening applied when packing frames: 0 off, 128 normal, 255 strong. */
void tab5_video_set_sharpening(unsigned strength);
#ifdef __cplusplus
}
#endif
