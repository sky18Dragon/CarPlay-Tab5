#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Acoustic echo cancellation for phone calls: removes the Tab5 speaker's own sound
 * from the microphone, so the far end does not hear itself. The engine is chosen at
 * build time (CONFIG_TAB5_ECHO_*). Single caller: the audio task. */
#define TAB5_ECHO_RATE 16000u
#define TAB5_ECHO_BLOCK 160u          /* 10 ms at TAB5_ECHO_RATE */
/* Allocates the engine. False when it is disabled or out of memory. */
bool tab5_echo_open(void);
void tab5_echo_close(void);
bool tab5_echo_active(void);
/* One 10 ms block: mic and speaker are the same period (speaker is what was sent to
 * the amplifier). out may be mic. Engines with a larger frame add up to one frame of
 * delay; until the first one is ready, out is silence. */
void tab5_echo_process(const int16_t *mic, const int16_t *speaker, int16_t *out);
const char *tab5_echo_engine(void);
#ifdef __cplusplus
}
#endif
