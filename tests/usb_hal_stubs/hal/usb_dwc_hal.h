#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint32_t intrs; bool enabled; unsigned disable_calls; } usb_dwc_host_chan_regs_t;
typedef enum { USB_DWC_HAL_CHAN_EVENT_NONE, USB_DWC_HAL_CHAN_EVENT_ERROR, USB_DWC_HAL_CHAN_EVENT_HALT_REQ, USB_DWC_HAL_CHAN_EVENT_CPLT } usb_dwc_hal_chan_event_t;
typedef enum { USB_DWC_HAL_CHAN_ERROR_NONE, USB_DWC_HAL_CHAN_ERROR_STALL, USB_DWC_HAL_CHAN_ERROR_PKT_BBL, USB_DWC_HAL_CHAN_ERROR_XCS_XACT } usb_dwc_hal_chan_error_t;
typedef struct {
    usb_dwc_host_chan_regs_t *regs;
    struct { unsigned chan_idx, active, halt_requested; } flags;
    usb_dwc_hal_chan_error_t error;
} usb_dwc_hal_chan_t;
