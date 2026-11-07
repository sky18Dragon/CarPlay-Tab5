#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define USB_TRANSFER_STATUS_COMPLETED 0
typedef void *usb_host_client_handle_t;
typedef void *usb_device_handle_t;
typedef struct { uint8_t bmRequestType,bRequest; uint16_t wValue,wIndex,wLength; } usb_setup_packet_t;
typedef struct usb_transfer usb_transfer_t;
struct usb_transfer { uint8_t *data_buffer; void *device_handle; unsigned bEndpointAddress; void (*callback)(usb_transfer_t *); void *context; int num_bytes,actual_num_bytes,status; };
int usb_host_transfer_alloc(size_t,int,usb_transfer_t **);
int usb_host_transfer_free(usb_transfer_t *);
int usb_host_transfer_submit_control(void *,usb_transfer_t *);
const char *esp_err_to_name(int);
