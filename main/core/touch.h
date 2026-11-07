#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Coordinates are already rotated into display orientation by the BSP. */
typedef struct { int32_t x,y,width,height; uint16_t video_width,video_height,report_width,report_height; bool pressed; uint16_t last_x,last_y; } cp_touch;
typedef enum { CP_TOUCH_NONE, CP_TOUCH_DOWN, CP_TOUCH_MOVE, CP_TOUCH_UP } cp_touch_kind;
typedef struct { cp_touch_kind kind; uint16_t x,y; } cp_touch_event;
/* The picture fills the display at the given shape, which may differ from the
 * shape of the video: the two axes then scale separately. */
bool cp_touch_configure(cp_touch *, int32_t display_width,int32_t display_height,uint16_t video_width,uint16_t video_height,uint32_t aspect_width,uint32_t aspect_height);
cp_touch_event cp_touch_update(cp_touch *, bool pressed,int32_t x,int32_t y);
cp_touch_event cp_touch_cancel(cp_touch *);
#ifdef __cplusplus
}
#endif
