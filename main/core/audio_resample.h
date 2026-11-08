#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The Tab5 codecs share one full-duplex I2S, so both directions run at
 * CP_AUDIO_RATE stereo and every stream is converted in software. */
#define CP_AUDIO_RATE 48000u
#define CP_AUDIO_BLOCK_FRAMES (CP_AUDIO_RATE / 100u) /* 10 ms */

/* Linear-interpolation upsampler for one stream. `last` carries the final
 * input frame of the previous block, so consecutive blocks join smoothly. */
typedef struct { int16_t last[2]; } cp_upsampler;
void cp_upsampler_reset(cp_upsampler *);
/* Converts in_frames of interleaved PCM (1 or 2 channels) to out_frames of
 * stereo, stretching or compressing the block to fit. */
void cp_upsample_block(cp_upsampler *, const int16_t *in, size_t in_frames, unsigned channels,
                       int16_t *out_stereo, size_t out_frames);

/* True when CP_AUDIO_RATE is a whole multiple of rate (8, 16, 24, 48 kHz). */
bool cp_decimate_supported(unsigned rate);
/* Box-filter decimation of 48 kHz mono to rate; in_frames must be
 * out_frames * (CP_AUDIO_RATE / rate). */
void cp_decimate_mono(const int16_t *in, size_t out_frames, unsigned rate, int16_t *out);
#ifdef __cplusplus
}
#endif
