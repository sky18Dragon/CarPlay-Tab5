#include "entropy_source.h"
#include "bootloader_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <assert.h>
static StaticSemaphore_t storage;
static SemaphoreHandle_t mutex;
static portMUX_TYPE init_lock=portMUX_INITIALIZER_UNLOCKED;
static unsigned owners;
static void lock(void) {
    portENTER_CRITICAL(&init_lock);
    if(!mutex) mutex=xSemaphoreCreateMutexStatic(&storage);
    portEXIT_CRITICAL(&init_lock);
    xSemaphoreTake(mutex,portMAX_DELAY);
}
void tab5_entropy_acquire(void) {
    lock(); if(!owners++) bootloader_random_enable(); xSemaphoreGive(mutex);
}
void tab5_entropy_release(void) {
    lock(); assert(owners); if(!--owners) bootloader_random_disable(); xSemaphoreGive(mutex);
}
