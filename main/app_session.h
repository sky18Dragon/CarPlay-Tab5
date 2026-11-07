#pragma once
#include "core/session.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/* USB client task owns attach/detach/poll; protocol workers only post events.
 * transport_epoch is captured before starting each worker, never read at exit. */
esp_err_t tab5_session_init(void);
void tab5_session_attach(uint32_t transport_epoch);
void tab5_session_detach(void);
bool tab5_session_post(uint32_t transport_epoch,cp_event event);
cp_action tab5_session_poll(void);
void tab5_session_drained(void);
#ifdef __cplusplus
}
#endif
