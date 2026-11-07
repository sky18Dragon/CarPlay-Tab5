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
// Waits until everything queued so far has finished.
void tab5_recon_drain(void);
// True once queued reconstruction has failed; queued work is then skipped.
bool tab5_recon_failed(void);
// Call after a drain, when the decoder that failed has been discarded.
void tab5_recon_clear_failure(void);
#ifdef __cplusplus
}
#endif
#endif
