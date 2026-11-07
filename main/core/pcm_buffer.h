#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Timestamp units are sample frames, not bytes. Input must be authenticated
 * 16-bit PCM; conversion to little endian occurs here. External lock required. */
typedef struct {
    uint8_t *samples, *valid;
    size_t capacity, head, queued, preroll, frame_bytes;
    uint32_t cursor, sample_rate;
    uint64_t generation, underrun_frames, rejected_packets;
    bool anchored, playing;
} cp_pcm;
bool cp_pcm_init(cp_pcm *, uint8_t *samples, uint8_t *valid, size_t capacity_frames,
                 uint32_t sample_rate, unsigned channels, size_t preroll_frames);
void cp_pcm_reset(cp_pcm *, uint64_t generation);
bool cp_pcm_push(cp_pcm *, uint64_t generation, uint32_t timestamp,
                 const uint8_t *, size_t bytes, bool big_endian);
/* Always fills requested output with PCM or silence. Returns real frame count. */
size_t cp_pcm_read(cp_pcm *, uint64_t generation, uint8_t *, size_t frames);
#ifdef __cplusplus
}
#endif
