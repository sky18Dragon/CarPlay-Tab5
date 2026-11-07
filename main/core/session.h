#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Single-owner state machine. Worker events must be queued to the owner.
 * Time is monotonic milliseconds supplied by the caller (virtual in tests). */
typedef enum {
    CP_DETACHED, CP_ENUMERATING, CP_USB_ADDRESSABLE, CP_TRUST_PENDING,
    CP_LOCKDOWN, CP_AUTHENTICATING, CP_NETWORK, CP_SESSION, CP_STREAMING,
    CP_DRAINING, CP_BACKOFF, CP_FAILED
} cp_stage;
typedef enum {
    CP_ATTACH, CP_ENUM_OK, CP_MUX_OK, CP_TRUST_OK, CP_AUTH_BEGIN,
    CP_AUTH_OK, CP_NETWORK_OK, CP_MEDIA_OK, CP_DETACH, CP_IO_ERROR,
    CP_AUTH_MISSING, CP_AUTH_REJECTED, CP_DRAINED, CP_RETRY, CP_STORAGE_ERROR,
    CP_PROFILE_MISSING
} cp_event;
typedef enum { CP_ERR_NONE, CP_ERR_TIMEOUT, CP_ERR_IO, CP_ERR_AUTH_MISSING,
               CP_ERR_AUTH_REJECTED, CP_ERR_STORAGE, CP_ERR_PROFILE } cp_error;
typedef enum { CP_ACTION_NONE, CP_ACTION_START, CP_ACTION_STOP,
               CP_ACTION_RECONNECT } cp_action;
typedef struct {
    uint64_t generation;
    uint64_t deadline_ms;
    cp_stage stage;
    cp_error error;
    unsigned retries;
    bool attached;
    bool retryable;
} cp_session;
void cp_session_init(cp_session *s);
/* Generation zero is never a valid worker token. ATTACH/DETACH are owner-only. */
cp_action cp_session_event(cp_session *s, uint64_t generation, cp_event event, uint64_t now_ms);
cp_action cp_session_tick(cp_session *s, uint64_t now_ms);
const char *cp_stage_name(cp_stage stage);
#ifdef __cplusplus
}
#endif
