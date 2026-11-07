#include "video_queue.h"
#include <string.h>
static void clear(cp_video_queue *q)
{ q->read=q->write=q->bytes=0; q->head=q->count=0; }
bool cp_video_init(cp_video_queue *q,uint8_t *storage,size_t capacity)
{
    if(!q || !storage || !capacity || capacity>512*1024) return false;
    *q=(cp_video_queue){.storage=storage,.capacity=capacity}; cp_video_reset(q,0); return true;
}
void cp_video_reset(cp_video_queue *q,uint64_t generation)
{
    clear(q); q->generation=generation; q->dropped=0;
    q->waiting_idr=q->request_idr=true;
}
bool cp_video_push(cp_video_queue *q,uint64_t generation,const uint8_t *data,size_t size,uint64_t pts,bool idr)
{
    if(!q || !generation || generation!=q->generation || !data || !size) return false;
    if(size>q->capacity || q->count==CP_VIDEO_MAX_UNITS || size>q->capacity-q->bytes) {
        q->dropped+=q->count; clear(q); q->waiting_idr=q->request_idr=true;
    }
    if(size>q->capacity || (q->waiting_idr && !idr)) { ++q->dropped; return false; }
    if(idr) { q->waiting_idr=false; q->request_idr=false; }
    size_t first=q->capacity-q->write; if(first>size) first=size;
    memcpy(q->storage+q->write,data,first); memcpy(q->storage,data+first,size-first);
    q->write=(q->write+size)%q->capacity; q->bytes+=size;
    q->units[(q->head+q->count)%CP_VIDEO_MAX_UNITS]=(cp_video_unit){size,pts,idr};
    ++q->count; return true;
}
bool cp_video_pop(cp_video_queue *q,uint64_t generation,uint8_t *out,size_t capacity,cp_video_unit *unit)
{
    if(!q || !generation || generation!=q->generation || !out || !unit || !q->count) return false;
    cp_video_unit u=q->units[q->head]; if(u.size>capacity) return false;
    size_t first=q->capacity-q->read; if(first>u.size) first=u.size;
    memcpy(out,q->storage+q->read,first); memcpy(out+first,q->storage,u.size-first);
    q->read=(q->read+u.size)%q->capacity; q->bytes-=u.size;
    q->head=(q->head+1)%CP_VIDEO_MAX_UNITS; --q->count; *unit=u; return true;
}
bool cp_video_take_idr_request(cp_video_queue *q)
{ if(!q) return false; bool request=q->request_idr; q->request_idr=false; return request; }
bool cp_video_config_changed(const uint8_t *current,size_t current_size,const uint8_t *next,size_t next_size)
{ return current_size!=next_size || memcmp(current,next,next_size)!=0; }
