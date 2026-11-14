#pragma once
#include <stddef.h>
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 1
#define ESP_CACHE_MSYNC_FLAG_UNALIGNED 2
static inline int esp_cache_msync(void *p,size_t n,int flags) { (void)p; (void)n; (void)flags; return 0; }
