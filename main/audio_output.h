#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Speaker and microphone share one full-duplex I2S at 48 kHz stereo. Two
 * playback lanes are mixed: lane 0 main audio (music, Siri, calls), lane 1
 * alternate audio (navigation prompts). Every stream is resampled to 48 kHz. */
#define TAB5_AUDIO_LANES 2
esp_err_t tab5_audio_init(void);
esp_err_t tab5_audio_routes_init(void);
/* Start replaces whatever the lane played before. Never called from the USB callback. */
esp_err_t tab5_audio_start(unsigned lane,uint64_t generation,uint32_t rate,unsigned channels);
/* Stops the lane only if it still plays this generation. */
void tab5_audio_stop(unsigned lane,uint64_t generation);
void tab5_audio_stop_all(void);
/* Copies authenticated PCM into the lane's jitter buffer; waits at most a few ms for the lock. */
bool tab5_audio_submit(unsigned lane,uint64_t generation,uint32_t timestamp,const uint8_t *,size_t,bool big_endian);
/* Lowers lane 0 while a navigation prompt plays. */
void tab5_audio_duck(bool ducked);
/* Microphone: reference counted; samples are 48 kHz mono averaged from both mics. */
esp_err_t tab5_mic_start(void);
void tab5_mic_stop(void);
/* Fills exactly `frames` samples at rate (8, 16, 24 or 48 kHz) or returns 0. */
size_t tab5_mic_read(unsigned rate,int16_t *samples,size_t frames);
#ifdef __cplusplus
}
#endif
