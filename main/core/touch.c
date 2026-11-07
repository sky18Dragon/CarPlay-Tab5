#include "touch.h"
#include <limits.h>
#include <string.h>
bool cp_touch_configure(cp_touch *t,int32_t dw,int32_t dh,uint16_t vw,uint16_t vh,uint32_t aw,uint32_t ah)
{
    if(!t || dw<=0 || dh<=0 || !vw || !vh || !aw || !ah || t->pressed) return false;
    int32_t w,h;
    if((int64_t)dw*ah <= (int64_t)dh*aw) { w=dw; h=(int32_t)((int64_t)dw*ah/aw); }
    else { h=dh; w=(int32_t)((int64_t)dh*aw/ah); }
    if(!w || !h) return false;
    /* Reports use the layout's own height, not the squeezed video's, so that a tall
     * display does not move in steps of several pixels. */
    *t=(cp_touch){.x=(dw-w)/2,.y=(dh-h)/2,.width=w,.height=h,.video_width=vw,.video_height=vh,
                  .report_width=vw,.report_height=(uint16_t)((uint32_t)vw*ah/aw)};
    return true;
}
cp_touch_event cp_touch_cancel(cp_touch *t)
{
    if(!t || !t->pressed) return (cp_touch_event){0};
    t->pressed=false;
    return (cp_touch_event){CP_TOUCH_UP,t->last_x,t->last_y};
}
cp_touch_event cp_touch_update(cp_touch *t,bool pressed,int32_t x,int32_t y)
{
    if(!t || t->width<=0 || t->height<=0) return (cp_touch_event){0};
    if(!pressed) return cp_touch_cancel(t);
    int64_t px=(int64_t)x-t->x,py=(int64_t)y-t->y;
    if(!t->pressed && (px<0 || py<0 || px>=t->width || py>=t->height)) return (cp_touch_event){0};
    if(px<0) px=0;
    if(py<0) py=0;
    if(px>=t->width) px=t->width-1;
    if(py>=t->height) py=t->height-1;
    uint16_t vx=t->width==1?0:(uint16_t)(px*(t->report_width-1)/(t->width-1));
    uint16_t vy=t->height==1?0:(uint16_t)(py*(t->report_height-1)/(t->height-1));
    if(t->pressed && vx==t->last_x && vy==t->last_y) return (cp_touch_event){0};
    cp_touch_kind kind=t->pressed?CP_TOUCH_MOVE:CP_TOUCH_DOWN;
    t->pressed=true; t->last_x=vx; t->last_y=vy;
    return (cp_touch_event){kind,vx,vy};
}
