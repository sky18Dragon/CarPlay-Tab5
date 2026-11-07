#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t control,data,alternate,in,out,notification,mac_string;
    uint16_t in_mps,out_mps,notification_mps;
} cp_ncm_descriptor;
bool cp_ncm_discover(const uint8_t *,size_t,cp_ncm_descriptor *);
#ifdef __cplusplus
}
#endif
