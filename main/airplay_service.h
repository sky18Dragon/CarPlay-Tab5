#pragma once
#include "esp_err.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Called by the session owner only, after accessory authentication and NCM.
 * Stop must complete before the owner tears down the network interface. */
esp_err_t tab5_airplay_start(uint64_t generation);
void tab5_airplay_stop(void);
uint16_t tab5_airplay_port(void);
void tab5_airplay_media_ready(void);
#ifdef __cplusplus
}
#endif
