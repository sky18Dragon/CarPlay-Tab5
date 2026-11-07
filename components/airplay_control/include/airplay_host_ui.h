/* SPDX-License-Identifier: GPL-3.0-or-later; requestUI follows DiPlay. */
#pragma once
#include <stdbool.h>
#include "airplay_rtsp_codec.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Call only for requests from an authenticated control/event channel. */
bool airplay_host_ui_handle(const airplay_rtsp_request_t *request);
/* UI task consumes the notification; network tasks never draw or send HID. */
bool airplay_host_ui_take(void);
#ifdef __cplusplus
}
#endif
