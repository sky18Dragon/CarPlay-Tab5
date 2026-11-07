#pragma once
#include "esp_err.h"
#include "core/session.h"
#include "core/video_decoder.h"
#include "core/touch.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*tab5_touch_sink)(cp_touch_event event,void *context);
esp_err_t tab5_ui_init(void);
void tab5_ui_status(cp_stage stage,cp_error error,bool simulation);
/* data holds a frame packed by cp_i420_pack(). */
bool tab5_ui_frame(const uint8_t *data,unsigned frame_width,unsigned frame_height);
void tab5_ui_clear(void);
void tab5_ui_touch_sink(tab5_touch_sink sink,void *context);
#ifdef __cplusplus
}
#endif
