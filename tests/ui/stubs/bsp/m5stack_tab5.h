#pragma once
#include "lvgl.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"
#define BSP_LCD_H_RES 720
#define CONFIG_BSP_LCD_DRAW_BUF_HEIGHT 64
typedef struct { int lvgl_port_cfg; unsigned buffer_size; int double_buffer;
    struct { bool buff_dma,buff_spiram,sw_rotate; } flags; } bsp_display_cfg_t;
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *);
lv_indev_t *bsp_display_get_input_dev(void);
lv_display_t *bsp_display_start(void);
bool bsp_display_lock(int timeout);
void bsp_display_unlock(void);
void bsp_display_rotate(lv_display_t *,lv_display_rotation_t);
esp_err_t bsp_display_brightness_set(int level);
