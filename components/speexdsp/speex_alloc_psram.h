/* Force-included into every SpeexDSP file on the target (see CMakeLists.txt).
 * SpeexDSP allocates its echo and preprocessor state with calloc, and ESP-IDF puts
 * allocations of up to 16 KiB in internal RAM. Internal RAM is scarce while CarPlay
 * streams, so all of it goes to PSRAM instead. */
#pragma once
#include <string.h>
#include "esp_heap_caps.h"
#define SPEEX_PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define OVERRIDE_SPEEX_ALLOC
static inline void *speex_alloc(int size) { return heap_caps_calloc(1, (size_t)size, SPEEX_PSRAM_CAPS); }
#define OVERRIDE_SPEEX_ALLOC_SCRATCH
static inline void *speex_alloc_scratch(int size) { return heap_caps_calloc(1, (size_t)size, SPEEX_PSRAM_CAPS); }
#define OVERRIDE_SPEEX_REALLOC
static inline void *speex_realloc(void *ptr, int size) { return heap_caps_realloc(ptr, (size_t)size, SPEEX_PSRAM_CAPS); }
#define OVERRIDE_SPEEX_FREE
static inline void speex_free(void *ptr) { heap_caps_free(ptr); }
#define OVERRIDE_SPEEX_FREE_SCRATCH
static inline void speex_free_scratch(void *ptr) { heap_caps_free(ptr); }
