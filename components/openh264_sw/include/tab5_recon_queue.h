// SPDX-License-Identifier: GPL-3.0-or-later
// Work that must follow the reconstruction queued so far, in order, on the
// reconstruction task. A picture that has been queued is final and stays unchanged
// while such work runs, even if the decoder has already released it, because the
// next picture is written only by a later job on the same task.
#ifndef TAB5_RECON_QUEUE_H_
#define TAB5_RECON_QUEUE_H_
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TAB5_RECON_PAYLOAD_BYTES 56
typedef void (*tab5_recon_work)(const void *payload);
// Returns false when no worker could be started; the caller then runs it itself.
bool tab5_recon_post(tab5_recon_work work,const void *payload,size_t bytes);
// Like tab5_recon_post, for work that only reads the picture holding picture_byte,
// such as copying a finished frame out. It runs on a helper task on the other core,
// one at a time and in order, while reconstruction goes on. Reconstruction waits
// only before writing into that same picture, and a drain waits for it too.
bool tab5_recon_post_reader(tab5_recon_work work,const void *payload,size_t bytes,const void *picture_byte);
// Waits until everything queued so far has finished.
void tab5_recon_drain(void);
// True once queued reconstruction has failed; queued work is then skipped.
bool tab5_recon_failed(void);
// Call after a drain, when the decoder that failed has been discarded.
void tab5_recon_clear_failure(void);
// Running totals since boot, in microseconds, for the video statistics.
typedef struct {
    unsigned long long recon_us;     // Reconstruction and border expansion on the worker core.
    unsigned long long reader_us;    // Reader work on the helper task.
    unsigned long long wait_us;      // Worker time spent waiting for the helper.
    unsigned long waits;             // Waits because a picture was still being read.
    unsigned long long parse_wait_us; // Worker idle, waiting for parsing to get ahead.
    unsigned long long stage_wait_us; // Parsing idle, waiting for the worker to free a stage.
    unsigned long long deblock_us;   // Deblocking, part of recon_us.
    unsigned long long expand_us;    // Border expansion, part of recon_us.
} tab5_recon_stats_t;
void tab5_recon_get_stats(tab5_recon_stats_t *stats);
#ifdef __cplusplus
}
#endif
#endif
