#pragma once
#include "lvgl.h"
#define ESP_LVGL_PORT_INIT_CONFIG() 0
typedef void *esp_lcd_panel_handle_t;
typedef struct { esp_lcd_panel_handle_t panel_handle; } lvgl_port_display_cfg_t;
typedef struct { int unused; } lvgl_port_display_dsi_cfg_t;
