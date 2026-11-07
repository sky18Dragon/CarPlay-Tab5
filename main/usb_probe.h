#pragma once
void usb_probe_task(void *arg);

#include "esp_err.h"
esp_err_t tab5_usb_start(void);

#ifdef __cplusplus
extern "C" {
#endif
int tab5_usb_ncm_interface(void);
#ifdef __cplusplus
}
#endif
