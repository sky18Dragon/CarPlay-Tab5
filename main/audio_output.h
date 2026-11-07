#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t tab5_audio_init(void);
esp_err_t tab5_audio_routes_init(void);
/* Start/stop are synchronous control operations, never called by USB callback.
 * Submit uses a zero-wait mutex and copies authenticated PCM into the jitter buffer. */
esp_err_t tab5_audio_start(uint64_t generation,uint32_t rate,unsigned channels);
void tab5_audio_stop(void);
bool tab5_audio_submit(uint64_t generation,uint32_t timestamp,const uint8_t *,size_t,bool big_endian);
#ifdef __cplusplus
}
#endif
