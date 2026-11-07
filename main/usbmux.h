// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "usb/usb_host.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t usbmux_start(usb_host_client_handle_t client, usb_device_handle_t device);
void usbmux_poll(); // Called only by USB client task.
void usbmux_disconnect();
bool usbmux_drained();
void usbmux_release();
uint32_t usbmux_epoch(void);
bool usbmux_worker_drained(void);

const char *usbmux_status(void);
#ifdef __cplusplus
}
#endif
