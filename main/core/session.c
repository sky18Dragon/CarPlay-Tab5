#include "session.h"
#include <string.h>
#include <limits.h>

static uint64_t after(uint64_t now, uint64_t delay)
{ return now > UINT64_MAX - delay ? UINT64_MAX : now + delay; }
static void enter(cp_session *s, cp_stage stage, uint64_t now)
{
    s->stage = stage;
    uint64_t delay = 0;
    switch (stage) {
    case CP_ENUMERATING: case CP_USB_ADDRESSABLE: delay = 10000; break;
    case CP_TRUST_PENDING: delay = 180000; break; /* RSA generation + user consent */
    case CP_LOCKDOWN: case CP_AUTHENTICATING: case CP_NETWORK: case CP_SESSION:
        delay = 30000; break;
    case CP_BACKOFF: delay = 1000u << (s->retries - 1); break;
    default: break;
    }
    s->deadline_ms = delay ? after(now, delay) : 0;
}
static void invalidate(cp_session *s)
{ if (++s->generation == 0) ++s->generation; }
static cp_action stop(cp_session *s, cp_error error, bool retryable, uint64_t now)
{
    s->error = error; s->retryable = retryable;
    invalidate(s); /* Invalidate callbacks BEFORE asking adapters to cancel. */
    enter(s, CP_DRAINING, now);
    return CP_ACTION_STOP;
}
void cp_session_init(cp_session *s)
{ memset(s, 0, sizeof(*s)); s->stage = CP_DETACHED; }
cp_action cp_session_event(cp_session *s, uint64_t generation, cp_event event, uint64_t now)
{
    if (event == CP_DETACH) {
        s->attached = false;
        s->error = CP_ERR_NONE; s->retryable = false;
        if (s->stage == CP_DETACHED || s->stage == CP_DRAINING) return CP_ACTION_NONE;
        return stop(s, CP_ERR_NONE, false, now);
    }
    if (event == CP_ATTACH) {
        s->attached = true;
        if (s->stage != CP_DETACHED) return CP_ACTION_NONE;
        s->retries = 0; s->error = CP_ERR_NONE; invalidate(s);
        enter(s, CP_ENUMERATING, now); return CP_ACTION_START;
    }
    if (!generation || generation != s->generation) return CP_ACTION_NONE;
    if (event == CP_DRAINED && s->stage == CP_DRAINING) {
        if (!s->attached) { enter(s, CP_DETACHED, now); return CP_ACTION_NONE; }
        /* A detach followed by attach during cancellation is a new connection. */
        if (s->error == CP_ERR_NONE) {
            s->retries = 0; invalidate(s); enter(s, CP_ENUMERATING, now);
            return CP_ACTION_START;
        }
        if (s->retryable && s->retries < 3) { ++s->retries; enter(s, CP_BACKOFF, now); }
        else enter(s, CP_FAILED, now);
        return CP_ACTION_NONE;
    }
    if (event == CP_RETRY && s->stage == CP_FAILED && s->attached) {
        s->error = CP_ERR_NONE; s->retries = 0; invalidate(s);
        enter(s, CP_ENUMERATING, now); return CP_ACTION_RECONNECT;
    }
    if (s->stage == CP_DETACHED || s->stage >= CP_DRAINING) return CP_ACTION_NONE;
    if (event == CP_IO_ERROR) return stop(s, CP_ERR_IO, true, now);
    if (event == CP_STORAGE_ERROR) return stop(s, CP_ERR_STORAGE, false, now);
    if (event == CP_PROFILE_MISSING) return stop(s, CP_ERR_PROFILE, false, now);
    if (event == CP_AUTH_MISSING) return stop(s, CP_ERR_AUTH_MISSING, false, now);
    if (event == CP_AUTH_REJECTED) return stop(s, CP_ERR_AUTH_REJECTED, false, now);
    const struct { cp_stage from; cp_event event; cp_stage to; } transitions[] = {
        {CP_ENUMERATING, CP_ENUM_OK, CP_USB_ADDRESSABLE},
        {CP_USB_ADDRESSABLE, CP_MUX_OK, CP_TRUST_PENDING},
        {CP_TRUST_PENDING, CP_TRUST_OK, CP_LOCKDOWN},
        {CP_LOCKDOWN, CP_AUTH_BEGIN, CP_AUTHENTICATING},
        {CP_AUTHENTICATING, CP_AUTH_OK, CP_NETWORK},
        {CP_NETWORK, CP_NETWORK_OK, CP_SESSION},
        {CP_SESSION, CP_MEDIA_OK, CP_STREAMING}
    };
    for (unsigned i = 0; i < sizeof(transitions)/sizeof(transitions[0]); ++i)
        if (s->stage == transitions[i].from && event == transitions[i].event) {
            enter(s, transitions[i].to, now); break;
        }
    return CP_ACTION_NONE;
}
cp_action cp_session_tick(cp_session *s, uint64_t now)
{
    if (!s->deadline_ms || now < s->deadline_ms) return CP_ACTION_NONE;
    if (s->stage == CP_BACKOFF) {
        invalidate(s); enter(s, CP_ENUMERATING, now); return CP_ACTION_RECONNECT;
    }
    return stop(s, CP_ERR_TIMEOUT, true, now);
}
const char *cp_stage_name(cp_stage stage)
{
    static const char *names[] = {"Disconnected", "Enumerating", "USB ready",
        "Waiting for trust", "Paired", "Authenticating", "Starting network",
        "Starting media", "Streaming", "Stopping", "Retry wait", "Failed"};
    return (unsigned)stage < sizeof(names)/sizeof(names[0]) ? names[stage] : "Unknown";
}
