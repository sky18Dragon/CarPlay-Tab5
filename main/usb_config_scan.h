#pragma once
#include "usb/usb_host.h"
void usb_config_scan_start(usb_host_client_handle_t client, usb_device_handle_t device, unsigned address, unsigned count);
void usb_config_scan_poll(unsigned address);
/* Returns false while an in-flight transfer still owns its buffer. */
bool usb_config_scan_release(unsigned address);

/* Complete validated scan only; mode request shares the retained EP0 transfer. */
bool usb_config_scan_result(unsigned address, uint8_t *network_configuration);
bool usb_config_scan_request_mode(unsigned address);
