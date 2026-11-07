#include "core/touch.h"
#include "core/hid_report.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
int main(void)
{
    { /* A squeezed video fills the panel, and each axis maps on its own. */
        cp_touch s={0};
        assert(cp_touch_configure(&s,1280,720,1024,192,16,9));
        assert(s.x==0 && s.y==0 && s.width==1280 && s.height==720);
        cp_touch_event e=cp_touch_update(&s,true,1279,719);
        assert(e.kind==CP_TOUCH_DOWN && e.x==1023 && e.y==575);
        e=cp_touch_update(&s,true,640,360);
        assert(e.kind==CP_TOUCH_MOVE && e.x==511 && e.y==287);
        assert(!cp_touch_configure(&s,1280,720,1024,192,16,0));
    }
    cp_touch t={0};
    assert(!cp_touch_configure(&t,0,720,640,480,640,480));
    assert(cp_touch_configure(&t,1280,720,640,480,640,480));
    assert(t.width==960 && t.height==720 && t.x==160 && t.y==0);
    assert(cp_touch_update(&t,true,0,360).kind==CP_TOUCH_NONE);
    cp_touch_event e=cp_touch_update(&t,true,160,0);
    assert(e.kind==CP_TOUCH_DOWN && e.x==0 && e.y==0);
    assert(!cp_touch_configure(&t,720,1280,640,480,640,480));
    e=cp_touch_update(&t,true,1119,719);
    assert(e.kind==CP_TOUCH_MOVE && e.x==639 && e.y==479);
    assert(cp_touch_update(&t,true,INT_MAX,INT_MAX).kind==CP_TOUCH_NONE);
    e=cp_touch_update(&t,false,INT_MIN,INT_MIN);
    assert(e.kind==CP_TOUCH_UP && e.x==639 && e.y==479);
    assert(cp_touch_cancel(&t).kind==CP_TOUCH_NONE);
    assert(cp_touch_configure(&t,720,1280,640,480,640,480));
    assert(t.width==720 && t.height==540 && t.y==370);
    e=cp_touch_update(&t,true,360,640); assert(e.kind==CP_TOUCH_DOWN);
    e=cp_touch_update(&t,true,INT_MIN,INT_MIN); assert(e.kind==CP_TOUCH_MOVE && !e.x && !e.y);
    assert(cp_touch_cancel(&t).kind==CP_TOUCH_UP);
    assert(cp_touch_configure(&t,1,1,1,1,1,1));
    e=cp_touch_update(&t,true,0,0); assert(e.kind==CP_TOUCH_DOWN && !e.x && !e.y);
    uint8_t report[12];
    cp_hid_report((cp_touch_event){CP_TOUCH_DOWN,0x1234,0xabcd},report);
    assert(report[1]==1 && report[2]==0x34 && report[3]==0x12 && report[4]==0xcd && report[5]==0xab && report[6]==1 && report[7]==0);
    cp_hid_report((cp_touch_event){CP_TOUCH_UP,0x1234,0xabcd},report);
    assert(report[1]==0 && report[7]==0);
    puts("touch: letterbox, portrait, extremes, drag clamp and release tests passed");
}
