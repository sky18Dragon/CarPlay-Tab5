#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Install once before RTSP starts. Calls are serialized by the audio receiver.
 * open returns a nonzero local generation. Only authenticated PCM is delivered.
 * type is the AirPlay stream type: 100 main audio, 101 alternate (navigation). */
typedef struct {
    uint64_t (*open)(unsigned type,unsigned rate,unsigned channels);
    bool (*write)(uint64_t generation,uint32_t timestamp,const uint8_t *,size_t);
    void (*close)(uint64_t generation);
    /* Optional: the phone's duckAudio/unduckAudio around navigation prompts. */
    void (*duck)(bool ducked);
    /* Optional microphone. mic_read fills exactly `frames` mono samples at
     * `rate` (native endian) and returns frames, or returns 0 when not enough
     * audio is captured yet. */
    bool (*mic_start)(void);
    size_t (*mic_read)(unsigned rate,int16_t *samples,size_t frames);
    void (*mic_stop)(void);
} airplay_audio_sink;
bool airplay_audio_install_sink(const airplay_audio_sink *sink);
