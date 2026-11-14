#pragma once
#include <stdbool.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef void *nvs_iterator_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define NVS_TYPE_ANY 0xff
#define NVS_READWRITE 1
esp_err_t nvs_entry_find(const char *,const char *,int,nvs_iterator_t *);
void nvs_release_iterator(nvs_iterator_t);
esp_err_t nvs_open(const char *,int,nvs_handle_t *);
esp_err_t nvs_erase_all(nvs_handle_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
const char *esp_err_to_name(esp_err_t);
