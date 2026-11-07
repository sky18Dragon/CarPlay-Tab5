#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
static inline void *heap_caps_calloc(size_t n,size_t size,int flags) { (void)flags; return calloc(n,size); }
