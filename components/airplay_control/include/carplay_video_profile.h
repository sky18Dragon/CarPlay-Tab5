#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { unsigned width,height,fps; const char *label; bool experimental; unsigned aspect_width,aspect_height; } carplay_video_profile_t;
unsigned carplay_video_profile_count(void);
unsigned carplay_video_profile_index(void);
const carplay_video_profile_t *carplay_video_profile_at(unsigned index);
const carplay_video_profile_t *carplay_video_profile_current(void);
/* Touch reports use the layout height, not the squeezed video height. */
unsigned carplay_video_profile_touch_height(void);
esp_err_t carplay_video_profile_select(unsigned index);
bool carplay_video_profile_did_fallback(void);
void carplay_video_profile_fallback(void);
/* An untried experimental profile is a trial: begin returns true at the first
 * displayed frame, then the caller confirms it or falls back to the first profile
 * from the next boot on. It returns false for a settled profile, which is never
 * judged again, so one busy moment cannot lower the resolution for good. */
bool carplay_video_profile_begin(void);
void carplay_video_profile_confirm(void);
#ifdef __cplusplus
}
#endif
