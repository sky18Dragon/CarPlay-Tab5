// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
struct Iap2Transport {
    void *context;
    bool (*write)(void *,const uint8_t *,size_t);
    int (*read)(void *,uint8_t *,size_t,unsigned);
};
void iap2_client_run(const Iap2Transport &transport,uint32_t epoch);
