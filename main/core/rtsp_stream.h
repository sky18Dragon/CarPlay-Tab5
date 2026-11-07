#pragma once
#include "airplay_rtsp_codec.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CP_RTSP_CAPACITY (AIRPLAY_RTSP_MAX_HEADER_BYTES + AIRPLAY_RTSP_MAX_BODY_BYTES)
typedef bool (*cp_rtsp_handler)(const airplay_rtsp_request_t *, void *);
/* Owner-thread only. Callback views expire on return; recursive feed prohibited.
 * A parse or handler error poisons the stream until reset, preventing resync into
 * attacker-controlled body data. Storage is caller-owned, never a task-stack array. */
typedef struct { uint8_t *buffer; size_t capacity, used; bool failed, busy; } cp_rtsp_stream;
bool cp_rtsp_init(cp_rtsp_stream *, uint8_t *storage, size_t capacity);
void cp_rtsp_reset(cp_rtsp_stream *);
bool cp_rtsp_feed(cp_rtsp_stream *, const uint8_t *, size_t, cp_rtsp_handler, void *);
#ifdef __cplusplus
}
#endif
