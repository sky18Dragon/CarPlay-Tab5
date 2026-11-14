#pragma once
#include "esp_lvgl_port.h"
#include "esp_err.h"
esp_err_t esp_lcd_dpi_panel_get_frame_buffer(esp_lcd_panel_handle_t,int,void **,void **);
