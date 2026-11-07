// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t media_bridge_set_h264_config(const uint8_t *data,size_t size);
void media_bridge_reset(void);
esp_err_t media_bridge_send_h264_borrowed(uint8_t *data,size_t prefix_capacity,size_t size,uint64_t sender_ns,uint64_t arrival_us);
