#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#define PPA_OPERATION_SRM 0
#define PPA_SRM_COLOR_MODE_YUV420 0
#define PPA_SRM_COLOR_MODE_RGB565 1
#define PPA_COLOR_RANGE_LIMIT 0
#define PPA_COLOR_CONV_STD_RGB_YUV_BT601 0
#define PPA_SRM_ROTATION_ANGLE_90 1
#define PPA_TRANS_MODE_BLOCKING 0
typedef void *ppa_client_handle_t;
typedef struct { int oper_type; } ppa_client_config_t;
typedef struct { const void *buffer; unsigned pic_w,pic_h,block_w,block_h,block_offset_x,block_offset_y;
    size_t buffer_size; int srm_cm,yuv_range,yuv_std; } test_ppa_image;
typedef struct { test_ppa_image in,out; int rotation_angle,mode; float scale_x,scale_y; } ppa_srm_oper_config_t;
esp_err_t ppa_register_client(const ppa_client_config_t *,ppa_client_handle_t *);
esp_err_t ppa_do_scale_rotate_mirror(ppa_client_handle_t,const ppa_srm_oper_config_t *);
