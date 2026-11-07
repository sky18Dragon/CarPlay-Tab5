#include "core/session.h"
#include <assert.h>
#include <stdio.h>
static void send(cp_session *s, cp_event e) { cp_session_event(s, s->generation, e, 0); }
int main(void)
{
    cp_session s; cp_session_init(&s);
    assert(cp_session_event(&s, 0, CP_ATTACH, 0) == CP_ACTION_START);
    uint64_t old = s.generation;
    send(&s, CP_MEDIA_OK); assert(s.stage == CP_ENUMERATING);
    send(&s, CP_ENUM_OK); send(&s, CP_MUX_OK); send(&s, CP_TRUST_OK);
    send(&s, CP_AUTH_BEGIN); send(&s, CP_AUTH_OK); send(&s, CP_NETWORK_OK);
    send(&s, CP_MEDIA_OK); assert(s.stage == CP_STREAMING);
    assert(cp_session_tick(&s, 999999) == CP_ACTION_NONE);
    assert(cp_session_event(&s, old, CP_DETACH, 0) == CP_ACTION_STOP);
    cp_session_event(&s, old, CP_ATTACH, 1);
    cp_session_event(&s, old, CP_DRAINED, 2); assert(s.stage == CP_DRAINING);
    send(&s, CP_DRAINED); assert(s.stage == CP_ENUMERATING);
    cp_session_event(&s, old, CP_ENUM_OK, 3); assert(s.stage == CP_ENUMERATING);
    for (unsigned i = 0; i < 4; ++i) {
        assert(cp_session_tick(&s, s.deadline_ms) == CP_ACTION_STOP);
        assert(s.stage == CP_DRAINING);
        assert(cp_session_tick(&s, UINT64_MAX) == CP_ACTION_NONE); /* Never free live DMA on timeout. */
        send(&s, CP_DRAINED);
        if (i < 3) {
            assert(s.stage == CP_BACKOFF);
            assert(cp_session_tick(&s, s.deadline_ms-1) == CP_ACTION_NONE);
            assert(cp_session_tick(&s, s.deadline_ms) == CP_ACTION_RECONNECT);
        } else assert(s.stage == CP_FAILED);
    }
    send(&s, CP_RETRY); assert(s.stage == CP_ENUMERATING);
    send(&s, CP_AUTH_MISSING); send(&s, CP_DRAINED);
    assert(s.stage == CP_FAILED && s.error == CP_ERR_AUTH_MISSING);
    send(&s, CP_RETRY); send(&s, CP_DETACH); send(&s, CP_DRAINED);
    assert(s.stage == CP_DETACHED);
    send(&s, CP_ATTACH); send(&s, CP_IO_ERROR);
    send(&s, CP_DETACH); send(&s, CP_ATTACH); send(&s, CP_DRAINED);
    assert(s.stage == CP_ENUMERATING && s.error == CP_ERR_NONE && s.retries == 0);
    puts("session: transition, generation, cancellation, timeout and retry tests passed");
}
