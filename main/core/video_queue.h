#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Absorbs bursts from TCP and slow frames. Overflowing costs an IDR, which is far
 * dearer than a queued frame; when decoding keeps up the queue stays near empty. */
#define CP_VIDEO_MAX_UNITS 6
typedef struct { size_t size; uint64_t pts; bool idr; } cp_video_unit;
/* Serialized producer/consumer access. Pop copies an entire AU into decoder
 * owned storage, so a reset cannot invalidate a decode already in progress. */
typedef struct {
    uint8_t *storage; size_t capacity,read,write,bytes;
    cp_video_unit units[CP_VIDEO_MAX_UNITS]; unsigned head,count;
    uint64_t generation,dropped;
    bool waiting_idr,request_idr;
} cp_video_queue;
bool cp_video_init(cp_video_queue *,uint8_t *,size_t);
void cp_video_reset(cp_video_queue *,uint64_t);
bool cp_video_push(cp_video_queue *,uint64_t,const uint8_t *,size_t,uint64_t pts,bool idr);
bool cp_video_pop(cp_video_queue *,uint64_t,uint8_t *,size_t,cp_video_unit *);
bool cp_video_take_idr_request(cp_video_queue *);
/* The phone repeats unchanged parameter sets around every keyframe, so only a
 * real difference may restart the stream. */
bool cp_video_config_changed(const uint8_t *current,size_t current_size,const uint8_t *next,size_t next_size);
#ifdef __cplusplus
}
#endif
