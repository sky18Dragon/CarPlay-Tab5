#pragma once
#include "freertos/FreeRTOS.h"
typedef int *SemaphoreHandle_t;
static int host_mutex_storage;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &host_mutex_storage; }
static inline void vSemaphoreDelete(SemaphoreHandle_t m) { (void)m; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t m, unsigned t) { (void)t; ++*m; return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t m) { --*m; return pdTRUE; }
