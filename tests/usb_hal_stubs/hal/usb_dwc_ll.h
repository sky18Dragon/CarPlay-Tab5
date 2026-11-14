#pragma once
#include "usb_dwc_hal.h"
#define USB_DWC_LL_INTR_CHAN_XFERCOMPL (1U << 0)
#define USB_DWC_LL_INTR_CHAN_CHHLTD (1U << 1)
#define USB_DWC_LL_INTR_CHAN_STALL (1U << 3)
#define USB_DWC_LL_INTR_CHAN_BBLEER (1U << 8)
#define USB_DWC_LL_INTR_CHAN_BNAINTR (1U << 11)
#define USB_DWC_LL_INTR_CHAN_XCS_XACT_ERR (1U << 13)
static inline uint32_t usb_dwc_ll_hcint_read_and_clear_intrs(usb_dwc_host_chan_regs_t *r) { uint32_t v=r->intrs; r->intrs=0; return v; }
static inline bool usb_dwc_ll_hcchar_chan_is_enabled(usb_dwc_host_chan_regs_t *r) { return r->enabled; }
/* A halt request does not synchronously stop hardware or guarantee an IRQ. */
static inline void usb_dwc_ll_hcchar_disable_chan(usb_dwc_host_chan_regs_t *r) { ++r->disable_calls; }
