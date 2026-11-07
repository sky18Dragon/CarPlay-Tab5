#pragma once
#include "lvgl.h"
#include "esp_err.h"
lv_display_t *bsp_display_start(void);
bool bsp_display_lock(int timeout);
void bsp_display_unlock(void);
void bsp_display_rotate(lv_display_t *,lv_display_rotation_t);
esp_err_t bsp_display_brightness_set(int level);
