#include "board_io.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_storage;
static portMUX_TYPE s_init = portMUX_INITIALIZER_UNLOCKED;

void tab5_board_io_lock(void)
{
    taskENTER_CRITICAL(&s_init);
    if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutexStatic(&s_lock_storage);
    taskEXIT_CRITICAL(&s_init);
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
}

void tab5_board_io_unlock(void)
{
    xSemaphoreGiveRecursive(s_lock);
}

esp_err_t tab5_feature_enable(bsp_feature_t feature, bool enable)
{
    tab5_board_io_lock();
    const esp_err_t rc = bsp_feature_enable(feature, enable);
    tab5_board_io_unlock();
    return rc;
}
