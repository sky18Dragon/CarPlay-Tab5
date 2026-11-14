#pragma once
#include "esp_lcd_mipi_dsi.h"
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t,int,int,int,int,const void *);
