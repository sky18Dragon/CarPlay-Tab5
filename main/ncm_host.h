// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif.h"
#include "usb/usb_host.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t ncm_host_init();
esp_err_t ncm_host_start(usb_host_client_handle_t client,usb_device_handle_t device);
void ncm_host_poll();
void ncm_host_disconnect();
bool ncm_host_drained();
/* True once after the link failed (USB pipe error, bad NTB, ...); clears the flag. */
bool ncm_host_take_fault();
void ncm_host_release();
esp_netif_t *ncm_host_netif();
bool ncm_host_ipv6(char *out,size_t capacity);

#ifdef __cplusplus
}
#endif
