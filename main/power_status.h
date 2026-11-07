#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Turns on battery charging and logs the battery state once. */
esp_err_t tab5_power_init(void);

/* Logs charge-enable, charge status and battery voltage. */
void tab5_power_log(void);

#ifdef __cplusplus
}
#endif
