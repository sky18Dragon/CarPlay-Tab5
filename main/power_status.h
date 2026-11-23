#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Turns on battery charging and logs the battery state once. */
esp_err_t tab5_power_init(void);

/* Logs charge-enable, charge status and battery voltage. */
void tab5_power_log(void);

typedef struct {
    unsigned battery_mv;
    int charge_ma;      /* Positive while the pack is taking charge. */
    bool charging;
} tab5_power_reading;
/* One reading of the battery rail. False when the monitor cannot be read. */
bool tab5_power_read(tab5_power_reading *out);

/* Turns the Tab5 off: a pulse on the power controller's PWROFF line, then deep
 * sleep if power is still on, so a flat pack is never drained further. Does not
 * return. Call only after warning the user. */
void tab5_power_off(void);

#ifdef __cplusplus
}
#endif
