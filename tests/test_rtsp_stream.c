#include "core/rtsp_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t storage[CP_RTSP_CAPACITY], output[512];
static unsigned count;
static bool receive(const airplay_rtsp_request_t *r, void *ctx)
{
    (void)ctx;
    assert(!strcmp(r->method,"SETUP") && !strcmp(r->cseq,"42"));
    assert(r->body_len==4 && !memcmp(r->body,"a\0bc",4));
    size_t n;
    assert(!airplay_rtsp_build_response(r,200,"OK",NULL,NULL,0,output,sizeof(output),&n));
    assert(n && !memcmp(output,"RTSP/1.0 200 OK\r\nCSeq: 42",25));
    ++count; return true;
}
int main(void)
{
    static const uint8_t request[]="SETUP /stream RTSP/1.0\r\nCSeq: 42\r\nContent-Length: 4\r\n\r\na\0bc";
    const size_t size=sizeof(request)-1;
    cp_rtsp_stream s;
    for(size_t split=0;split<=size;++split) {
        assert(cp_rtsp_init(&s,storage,sizeof(storage))); count=0;
        assert(cp_rtsp_feed(&s,request,split,receive,NULL));
        assert(cp_rtsp_feed(&s,request+split,size-split,receive,NULL));
        assert(count==1 && s.used==0);
    }
    uint8_t pipeline[sizeof(request)*3];
    for(unsigned i=0;i<3;++i) memcpy(pipeline+i*size,request,size);
    count=0; assert(cp_rtsp_feed(&s,pipeline,size*3,receive,NULL) && count==3);
    const char *bad[]={
        "SETUP / RTSP/1.0\r\nCSeq: 1\r\nCSeq: 2\r\n\r\n",
        "SETUP / RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 999999999999999999999999999999\r\n\r\n",
        "SETUP / RTSP/1.0\r\nCSeq: 1\r\nContent-Length: -1\r\n\r\n",
        "SETUP / RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 32769\r\n\r\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        cp_rtsp_reset(&s);
        assert(!cp_rtsp_feed(&s,(const uint8_t *)bad[i],strlen(bad[i]),receive,NULL));
        assert(!cp_rtsp_feed(&s,request,size,receive,NULL));
    }
    cp_rtsp_reset(&s); memset(storage,'X',sizeof(storage));
    uint8_t huge[2049]; memset(huge,'X',sizeof(huge));
    assert(!cp_rtsp_feed(&s,huge,sizeof(huge),receive,NULL));
    puts("RTSP stream: all splits, pipelining, binary body, invalid lengths and poisoned-stream tests passed");
}
