#pragma once
#include <stdint.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define NVS_READWRITE 1
esp_err_t nvs_open(const char *space,int mode,nvs_handle_t *handle);
esp_err_t nvs_get_u8(nvs_handle_t,const char *key,uint8_t *value);
esp_err_t nvs_get_u16(nvs_handle_t,const char *key,uint16_t *value);
esp_err_t nvs_set_u8(nvs_handle_t,const char *key,uint8_t value);
esp_err_t nvs_set_u16(nvs_handle_t,const char *key,uint16_t value);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
