/* Replaces usb_dwc_hal_chan_decode_intr() from ESP-IDF v5.5.3 (linked with
 * -Wl,--wrap). The original asserts that every channel error also halts the
 * channel, but the DWC2 core reports BNA (and occasionally other errors)
 * without CHHLTD, which reboots the board under USB load
 * (espressif/esp-idf#19111; master removed the assert in 63b468539f).
 *
 * If hardware has already stopped, report the error immediately: disabling an
 * already disabled channel produces no new halt interrupt. Only a still-enabled
 * channel defers the error until it halts. BNA is reported as a transaction error:
 * the HCD of espressif/usb 1.6.0 aborts on the URB_NOT_AVAIL event that BNA
 * maps to. Everything else follows the original function. */
#include "hal/usb_dwc_hal.h"
#include "hal/usb_dwc_ll.h"
#include "esp_log.h"
#include <stdlib.h>

#define CHAN_INTRS_ERROR_MSK (USB_DWC_LL_INTR_CHAN_STALL | USB_DWC_LL_INTR_CHAN_BBLEER | \
                              USB_DWC_LL_INTR_CHAN_BNAINTR | USB_DWC_LL_INTR_CHAN_XCS_XACT_ERR)

static uint32_t deferred[16];          /* error bits waiting for the halt, per channel index */
static volatile uint32_t recovered;    /* errors that would have asserted */

uint32_t tab5_usb_hal_recovered_errors(void) { return recovered; }

void __real_usb_dwc_hal_chan_activate(usb_dwc_hal_chan_t *, void *, int, int);
void __wrap_usb_dwc_hal_chan_activate(usb_dwc_hal_chan_t *chan_obj, void *descs, int count, int start)
{
    /* A disconnect can discard a deferred interrupt. Never carry its error into
     * a new transfer or a newly allocated pipe using the same channel index. */
    deferred[chan_obj->flags.chan_idx] = 0;
    __real_usb_dwc_hal_chan_activate(chan_obj, descs, count, start);
}

usb_dwc_hal_chan_event_t __wrap_usb_dwc_hal_chan_decode_intr(usb_dwc_hal_chan_t *chan_obj)
{
    uint32_t chan_intrs = usb_dwc_ll_hcint_read_and_clear_intrs(chan_obj->regs);
    const unsigned index = chan_obj->flags.chan_idx;
    usb_dwc_hal_chan_event_t chan_event;

    if ((chan_intrs & CHAN_INTRS_ERROR_MSK) && !(chan_intrs & USB_DWC_LL_INTR_CHAN_CHHLTD)) {
        deferred[index] |= chan_intrs & CHAN_INTRS_ERROR_MSK;
        ++recovered;
        if (usb_dwc_ll_hcchar_chan_is_enabled(chan_obj->regs)) {
            usb_dwc_ll_hcchar_disable_chan(chan_obj->regs);
            return USB_DWC_HAL_CHAN_EVENT_NONE;
        }
    }
    if ((chan_intrs & USB_DWC_LL_INTR_CHAN_CHHLTD) ||
        !usb_dwc_ll_hcchar_chan_is_enabled(chan_obj->regs)) {
        chan_intrs |= deferred[index];
        deferred[index] = 0;
    }

    /* Errors > Channel Halt Request > Transfer completed (as in the original). */
    if (chan_intrs & CHAN_INTRS_ERROR_MSK) {
        /* HCD only wakes a synchronous halt waiter on HALT_REQ. An error that
         * races with that request must complete the cancellation too. */
        if (chan_obj->flags.halt_requested) {
            chan_obj->flags.halt_requested = 0;
            chan_obj->flags.active = 0;
            return USB_DWC_HAL_CHAN_EVENT_HALT_REQ;
        }
        usb_dwc_hal_chan_error_t error;
        if (chan_intrs & USB_DWC_LL_INTR_CHAN_STALL) {
            error = USB_DWC_HAL_CHAN_ERROR_STALL;
        } else if (chan_intrs & USB_DWC_LL_INTR_CHAN_BBLEER) {
            error = USB_DWC_HAL_CHAN_ERROR_PKT_BBL;
        } else {
            error = USB_DWC_HAL_CHAN_ERROR_XCS_XACT;   /* includes BNA, see above */
        }
        chan_obj->error = error;
        chan_obj->flags.active = 0;
        chan_event = USB_DWC_HAL_CHAN_EVENT_ERROR;
    } else if (chan_intrs & USB_DWC_LL_INTR_CHAN_CHHLTD) {
        if (chan_obj->flags.halt_requested) {
            chan_obj->flags.halt_requested = 0;
            chan_event = USB_DWC_HAL_CHAN_EVENT_HALT_REQ;
        } else {
            chan_event = USB_DWC_HAL_CHAN_EVENT_CPLT;   /* halted due to QTD HOC */
        }
        chan_obj->flags.active = 0;
    } else if (chan_intrs & USB_DWC_LL_INTR_CHAN_XFERCOMPL) {
        /* Short interrupt IN without HOC: halt manually, the halt interrupt reports CPLT. */
        usb_dwc_ll_hcchar_disable_chan(chan_obj->regs);
        chan_event = USB_DWC_HAL_CHAN_EVENT_NONE;
    } else {
        abort();
    }
    return chan_event;
}
