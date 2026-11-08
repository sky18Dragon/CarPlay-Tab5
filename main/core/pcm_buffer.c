#include "pcm_buffer.h"
#include <string.h>
bool cp_pcm_init(cp_pcm *p,uint8_t *samples,uint8_t *valid,size_t capacity,uint32_t rate,unsigned channels,size_t preroll)
{
    if(!p || !samples || !valid || !capacity || capacity>48000 ||
       (rate!=8000 && rate!=16000 && rate!=24000 && rate!=32000 && rate!=44100 && rate!=48000) || (channels!=1 && channels!=2) || preroll>capacity) return false;
    *p=(cp_pcm){.samples=samples,.valid=valid,.capacity=capacity,.sample_rate=rate,
                .frame_bytes=channels*2,.preroll=preroll};
    cp_pcm_reset(p,0); return true;
}
void cp_pcm_reset(cp_pcm *p,uint64_t generation)
{
    p->generation=generation; p->head=p->queued=0; p->anchored=p->playing=false;
    p->cursor=0; p->underrun_frames=p->rejected_packets=0;
    memset(p->valid,0,p->capacity); memset(p->samples,0,p->capacity*p->frame_bytes);
}
bool cp_pcm_push(cp_pcm *p,uint64_t generation,uint32_t timestamp,const uint8_t *data,size_t bytes,bool big)
{
    if(!p || !generation || generation!=p->generation || !data || !bytes || bytes%p->frame_bytes) return false;
    size_t frames=bytes/p->frame_bytes;
    if(frames>p->capacity) { ++p->rejected_packets; return false; }
    if(!p->anchored) { p->cursor=timestamp; p->anchored=true; }
    uint32_t delta=timestamp-p->cursor;
    /* Modulo subtraction handles RTP timestamp wrap. Late/ambiguous packets
     * are rejected, as are packets beyond the bounded playout window. */
    if(delta>=0x80000000u || delta>p->capacity || frames>p->capacity-delta) {
        ++p->rejected_packets; return false;
    }
    for(size_t i=0;i<frames;++i) {
        size_t slot=(p->head+delta+i)%p->capacity;
        if(p->valid[slot]) continue; /* Duplicate packets never overwrite audio. */
        uint8_t *out=p->samples+slot*p->frame_bytes;
        const uint8_t *in=data+i*p->frame_bytes;
        for(size_t j=0;j<p->frame_bytes;j+=2) {
            out[j]=in[j+(big?1:0)]; out[j+1]=in[j+(big?0:1)];
        }
        p->valid[slot]=1; ++p->queued;
    }
    return true;
}
size_t cp_pcm_read(cp_pcm *p,uint64_t generation,uint8_t *out,size_t frames)
{
    if(!p || !out || frames>SIZE_MAX/p->frame_bytes) return 0;
    memset(out,0,frames*p->frame_bytes);
    if(!generation || generation!=p->generation || !p->anchored) return 0;
    if(!p->playing) {
        if(p->queued<p->preroll) return 0;
        p->playing=true;
    }
    size_t real=0;
    for(size_t i=0;i<frames;++i) {
        if(p->valid[p->head]) {
            memcpy(out+i*p->frame_bytes,p->samples+p->head*p->frame_bytes,p->frame_bytes);
            p->valid[p->head]=0; --p->queued; ++real;
        } else ++p->underrun_frames;
        p->head=(p->head+1)%p->capacity; ++p->cursor;
    }
    return real;
}
