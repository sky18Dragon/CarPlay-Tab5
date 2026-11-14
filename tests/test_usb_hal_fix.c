#include <assert.h>
#include <stdio.h>
#include "hal/usb_dwc_ll.h"
usb_dwc_hal_chan_event_t __wrap_usb_dwc_hal_chan_decode_intr(usb_dwc_hal_chan_t *);
void __wrap_usb_dwc_hal_chan_activate(usb_dwc_hal_chan_t *,void *,int,int);
uint32_t tab5_usb_hal_recovered_errors(void);
void __real_usb_dwc_hal_chan_activate(usb_dwc_hal_chan_t *c,void *d,int n,int start) {
    (void)d; (void)n; (void)start; c->regs->enabled=true; c->flags.active=1;
}
int main(void) {
    usb_dwc_host_chan_regs_t regs={0};
    usb_dwc_hal_chan_t c={.regs=&regs,.flags={.chan_idx=3,.active=1}};
    /* Field failure: BNA without CHHLTD, hardware already disabled. */
    regs.intrs=USB_DWC_LL_INTR_CHAN_BNAINTR;
    assert(__wrap_usb_dwc_hal_chan_decode_intr(&c)==USB_DWC_HAL_CHAN_EVENT_ERROR);
    assert(!c.flags.active && !regs.disable_calls && c.error==USB_DWC_HAL_CHAN_ERROR_XCS_XACT);
    /* Hardware still enabled: don't release DMA storage until the halt IRQ. */
    c.flags.active=1; regs.enabled=true; regs.intrs=USB_DWC_LL_INTR_CHAN_BNAINTR;
    assert(__wrap_usb_dwc_hal_chan_decode_intr(&c)==USB_DWC_HAL_CHAN_EVENT_NONE);
    assert(c.flags.active && regs.disable_calls==1);
    regs.enabled=false; regs.intrs=USB_DWC_LL_INTR_CHAN_CHHLTD;
    assert(__wrap_usb_dwc_hal_chan_decode_intr(&c)==USB_DWC_HAL_CHAN_EVENT_ERROR);
    assert(!c.flags.active && c.error==USB_DWC_HAL_CHAN_ERROR_XCS_XACT);
    /* A deferred error canceled by unplug must not poison a reused channel. */
    regs.enabled=true; regs.intrs=USB_DWC_LL_INTR_CHAN_STALL;
    assert(__wrap_usb_dwc_hal_chan_decode_intr(&c)==USB_DWC_HAL_CHAN_EVENT_NONE);
    c.flags.active=0;
    __wrap_usb_dwc_hal_chan_activate(&c,NULL,1,0);
    regs.enabled=false; regs.intrs=USB_DWC_LL_INTR_CHAN_CHHLTD;
    assert(__wrap_usb_dwc_hal_chan_decode_intr(&c)==USB_DWC_HAL_CHAN_EVENT_CPLT);
    /* Error/cancel race must wake HCD's synchronous halt waiter. */
    c.flags.active=1; c.flags.halt_requested=1;
    regs.intrs=USB_DWC_LL_INTR_CHAN_STALL|USB_DWC_LL_INTR_CHAN_CHHLTD;
    assert(__wrap_usb_dwc_hal_chan_decode_intr(&c)==USB_DWC_HAL_CHAN_EVENT_HALT_REQ);
    assert(!c.flags.active && !c.flags.halt_requested);
    regs.intrs=USB_DWC_LL_INTR_CHAN_BBLEER|USB_DWC_LL_INTR_CHAN_CHHLTD;
    assert(__wrap_usb_dwc_hal_chan_decode_intr(&c)==USB_DWC_HAL_CHAN_EVENT_ERROR);
    assert(c.error==USB_DWC_HAL_CHAN_ERROR_PKT_BBL);
    c.flags.active=1; regs.enabled=true; regs.intrs=USB_DWC_LL_INTR_CHAN_XFERCOMPL;
    assert(__wrap_usb_dwc_hal_chan_decode_intr(&c)==USB_DWC_HAL_CHAN_EVENT_NONE);
    regs.enabled=false; regs.intrs=USB_DWC_LL_INTR_CHAN_CHHLTD;
    assert(__wrap_usb_dwc_hal_chan_decode_intr(&c)==USB_DWC_HAL_CHAN_EVENT_CPLT);
    assert(tab5_usb_hal_recovered_errors()==3);
    puts("usb_hal_fix: stopped/active errors, cancellation race and channel reuse passed");
}
