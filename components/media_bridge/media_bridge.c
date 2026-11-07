// SPDX-License-Identifier: GPL-3.0-or-later
#include "media_bridge.h"
extern esp_err_t video_live_config(const uint8_t *,size_t);
extern esp_err_t video_live_frame(const uint8_t *,size_t);
extern esp_err_t video_live_frame_timed(const uint8_t *,size_t,uint64_t,uint64_t);
extern void video_live_reset_stream(void);
void media_bridge_reset(void) { video_live_reset_stream(); }
esp_err_t media_bridge_set_h264_config(const uint8_t *data,size_t size) { return video_live_config(data,size); }
esp_err_t media_bridge_send_h264_borrowed(uint8_t *data,size_t prefix_capacity,size_t size,uint64_t sender_ns,uint64_t arrival_us) {
    (void)prefix_capacity;
    return video_live_frame_timed(data,size,sender_ns,arrival_us);
}
