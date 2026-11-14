#pragma once
#include <stdio.h>
#define ESP_LOGI(tag,...) do { (void)(tag); printf(__VA_ARGS__); puts(""); } while(0)
#define ESP_LOGE(tag,...) ESP_LOGI(tag,__VA_ARGS__)
