#pragma once
#include <stdbool.h>
typedef enum {
    TAB5_PAIR_IDLE, TAB5_PAIR_REQUESTED, TAB5_PAIR_CLEARING,
    TAB5_PAIR_CLEARED, TAB5_PAIR_EMPTY, TAB5_PAIR_FAILED, TAB5_PAIR_BLOCKED
} tab5_pair_reset_state;
/* UI calls only these nonblocking functions. No storage access from LVGL. */
bool tab5_pair_reset_ready(void);
bool tab5_pair_reset_request(void);
tab5_pair_reset_state tab5_pair_reset_status(void);
/* USB owner calls after event dispatch/teardown. quiescent must include all
 * devices, pending enumeration, workers and recovery, not just session state.
 * It must not start another pairing worker until this call returns. */
void tab5_pair_reset_poll(bool quiescent);
