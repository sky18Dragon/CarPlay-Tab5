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
void tab5_ui_status(cp_stage stage,cp_error error);
/* data holds a frame packed by cp_i420_pack(). */
bool tab5_ui_frame(const uint8_t *data,unsigned frame_width,unsigned frame_height);
void tab5_ui_clear(void);
void tab5_ui_touch_sink(tab5_touch_sink sink,void *context);
/* Battery state for the waiting screen, Tab5 Settings and, when low, a small icon
 * over CarPlay. percent is -1 when unknown. */
void tab5_ui_battery(int percent,bool charging,bool low);
/* Replaces everything with the power-off notice; video stops being drawn. */
void tab5_ui_power_off_notice(void);
#ifdef __cplusplus
}
#endif
