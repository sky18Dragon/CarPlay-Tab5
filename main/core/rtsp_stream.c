#include "rtsp_stream.h"
#include <string.h>
bool cp_rtsp_init(cp_rtsp_stream *s, uint8_t *storage, size_t capacity)
{
    if (!s || !storage || capacity < CP_RTSP_CAPACITY) return false;
    *s = (cp_rtsp_stream){.buffer=storage, .capacity=CP_RTSP_CAPACITY};
    return true;
}
void cp_rtsp_reset(cp_rtsp_stream *s)
{ if (s && !s->busy) { s->used=0; s->failed=false; } }
bool cp_rtsp_feed(cp_rtsp_stream *s, const uint8_t *p, size_t n, cp_rtsp_handler handler, void *ctx)
{
    if (!s || s->failed || s->busy || !s->buffer || (!p && n) || !handler) return false;
    s->busy=true;
    while (n || s->used) {
        size_t take=s->capacity-s->used;
        if (take>n) take=n;
        if (take) { memcpy(s->buffer+s->used,p,take); p+=take; n-=take; s->used+=take; }
        airplay_rtsp_request_t request;
        int result=airplay_rtsp_parse_request(s->buffer,s->used,&request);
        if (result==AIRPLAY_RTSP_PARSE_INCOMPLETE) {
            if (s->used==s->capacity) s->failed=true;
            break;
        }
        if (result<0 || !handler(&request,ctx)) { s->failed=true; break; }
        s->used-=request.consumed;
        memmove(s->buffer,s->buffer+request.consumed,s->used);
    }
    s->busy=false;
    return !s->failed;
}
