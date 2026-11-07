#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Install once before RTSP starts. Calls are serialized by the audio receiver.
 * open returns a nonzero local generation. Only authenticated PCM is delivered. */
typedef struct {
    uint64_t (*open)(unsigned rate,unsigned channels);
    bool (*write)(uint64_t generation,uint32_t timestamp,const uint8_t *,size_t);
    void (*close)(uint64_t generation);
} airplay_audio_sink;
bool airplay_audio_install_sink(const airplay_audio_sink *sink);
