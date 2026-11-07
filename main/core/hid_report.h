#pragma once
#include "touch.h"
#include <string.h>
/* Matches the two-contact descriptor; Tab5 currently sends one finger.
 * Little-endian coordinates, contact 0 active, contact 1 explicitly released. */
static inline void cp_hid_report(cp_touch_event event,uint8_t output[12]) {
    memset(output,0,12); output[6]=1;
    output[1]=(event.kind==CP_TOUCH_DOWN || event.kind==CP_TOUCH_MOVE)?1:0;
    output[2]=(uint8_t)event.x; output[3]=(uint8_t)(event.x>>8);
    output[4]=(uint8_t)event.y; output[5]=(uint8_t)(event.y>>8);
}
