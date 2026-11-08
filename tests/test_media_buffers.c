#include "core/pcm_buffer.h"
#include "core/video_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    uint8_t samples[32], valid[8], out[64], data[32];
    for(unsigned i=0;i<sizeof(data);++i) data[i]=(uint8_t)(i+1);
    cp_pcm p;
    assert(!cp_pcm_init(&p,samples,valid,8,22050,2,4));
    assert(cp_pcm_init(&p,samples,valid,8,24000,1,4));
    assert(cp_pcm_init(&p,samples,valid,8,48000,2,4)); cp_pcm_reset(&p,7);
    assert(!cp_pcm_push(&p,6,0,data,8,false));
    assert(cp_pcm_push(&p,7,UINT32_MAX-1,data,8,true));
    memset(out,0xff,sizeof(out)); assert(cp_pcm_read(&p,7,out,2)==0 && out[0]==0);
    assert(cp_pcm_push(&p,7,2,data+16,8,true)); /* out of order */
    assert(cp_pcm_push(&p,7,0,data+8,8,true));
    assert(cp_pcm_push(&p,7,0,data+8,8,true) && p.queued==6); /* duplicate */
    assert(cp_pcm_read(&p,7,out,8)==6 && p.underrun_frames==2);
    for(unsigned i=0;i<24;i+=2) assert(out[i]==data[i+1] && out[i+1]==data[i]);
    for(unsigned i=24;i<32;++i) assert(out[i]==0);
    assert(!cp_pcm_push(&p,7,0,data,8,false));
    cp_pcm_reset(&p,8); assert(!p.queued && !p.playing);
    assert(cp_pcm_read(&p,7,out,8)==0);
    assert(!cp_pcm_push(&p,8,0,data,3,false));
    cp_video_queue q; uint8_t memory[16]; cp_video_unit unit;
    assert(cp_video_init(&q,memory,sizeof(memory))); cp_video_reset(&q,1);
    assert(cp_video_take_idr_request(&q) && !cp_video_take_idr_request(&q));
    assert(!cp_video_push(&q,1,data,6,10,false));
    assert(cp_video_push(&q,1,data,6,11,true));
    assert(!cp_video_pop(&q,1,out,5,&unit) && q.count==1);
    assert(cp_video_pop(&q,1,out,sizeof(out),&unit) && unit.pts==11 && !memcmp(out,data,6));
    assert(cp_video_push(&q,1,data,12,12,false)); /* ring wrap */
    assert(cp_video_pop(&q,1,out,sizeof(out),&unit) && !memcmp(out,data,12));
    for(unsigned i=0;i<CP_VIDEO_MAX_UNITS;++i) assert(cp_video_push(&q,1,data,2,13+i,false));
    assert(!cp_video_push(&q,1,data,2,13+CP_VIDEO_MAX_UNITS,false)); /* count overload breaks reference chain */
    assert(q.count==0 && cp_video_take_idr_request(&q));
    assert(cp_video_push(&q,1,data,16,17,true));
    assert(!cp_video_push(&q,1,data,1,18,false) && q.count==0);
    assert(!cp_video_push(&q,1,data,17,19,true));
    assert(cp_video_push(&q,1,data,4,20,true)); cp_video_reset(&q,2);
    assert(!cp_video_pop(&q,1,out,sizeof(out),&unit) && !q.count);
    { const uint8_t a[]={1,2,3},b[]={1,2,4};
      assert(!cp_video_config_changed(a,3,a,3));
      assert(cp_video_config_changed(a,3,b,3));
      assert(cp_video_config_changed(a,2,a,3));
      assert(cp_video_config_changed(NULL,0,a,3)); }
    puts("media buffers: RTP wrap/reorder/duplicate/silence, AU bounds/wrap/IDR recovery passed");
 }
