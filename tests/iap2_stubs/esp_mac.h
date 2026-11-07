#pragma once
#include <stdint.h>
static inline int esp_efuse_mac_get_default(uint8_t *p) { for(unsigned i=0;i<6;++i)p[i]=i;return 0; }
