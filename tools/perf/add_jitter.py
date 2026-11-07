p='main/video_output.cpp'; s=open(p).read()
def R(a,b):
    global s
    assert a in s,a
    s=s.replace(a,b,1)
R('namespace {\n','''#include <stdlib.h>
namespace {
/* TEMP jitter statistics */
constexpr int kSamples=2048;
struct Series { uint32_t v[kSamples]; int n; };
Series *arrive_s,*show_s,*age_s; int64_t last_arrive,last_show; uint32_t depth_sum,depth_max,depth_n,gaps_100,gaps_200;
void add(Series *s,uint32_t ms) { if(s && s->n<kSamples) s->v[s->n++]=ms; }
int cmp(const void *a,const void *b) { uint32_t x=*(const uint32_t *)a,y=*(const uint32_t *)b; return x<y?-1:x>y; }
void report(const char *name,Series *s) {
    if(!s || !s->n) return;
    qsort(s->v,s->n,4,cmp); uint64_t sum=0; for(int i=0;i<s->n;++i) sum+=s->v[i];
    ESP_LOGW("JITTER","%s n=%d avg=%.1f p50=%.1f p90=%.1f p95=%.1f p99=%.1f max=%.1f",name,s->n,(double)sum/s->n/10,s->v[s->n/2]/10.0,s->v[s->n*9/10]/10.0,s->v[s->n*95/100]/10.0,s->v[s->n*99/100]/10.0,s->v[s->n-1]/10.0);
    s->n=0;
}
''')
R('struct FrameSlot { uint8_t *memory; unsigned width,height; uint64_t generation; };','struct FrameSlot { uint8_t *memory; unsigned width,height; uint64_t generation,pts; };')
R('    slot.width=frame.width; slot.height=frame.height; slot.generation=stream;\n','    slot.width=frame.width; slot.height=frame.height; slot.generation=stream; slot.pts=frame.pts;\n')
R('                stats.width=showing->width; stats.height=showing->height;\n','''                stats.width=showing->width; stats.height=showing->height;
                { int64_t t=esp_timer_get_time();
                  if(last_show) { uint32_t ms=(uint32_t)((t-last_show)/100); add(show_s,ms); if(ms>=1000) ++gaps_100; if(ms>=2000) ++gaps_200; }
                  last_show=t; add(age_s,(uint32_t)((t-(int64_t)showing->pts)/100));
                  if(show_s && show_s->n>=1200) { report("arrive_ms",arrive_s); report("show_ms",show_s); report("age_ms",age_s);
                      ESP_LOGW("JITTER","depth avg=%.2f max=%lu gaps>=100ms=%lu gaps>=200ms=%lu",(double)depth_sum/(depth_n?depth_n:1),(unsigned long)depth_max,(unsigned long)gaps_100,(unsigned long)gaps_200);
                      depth_sum=depth_n=depth_max=gaps_100=gaps_200=0; } }
''')
R('            bool have=cp_video_pop(&queue,current,input,512*1024,&unit);\n','''            { uint32_t d=queue.count; depth_sum+=d; ++depth_n; if(d>depth_max) depth_max=d; }
            bool have=cp_video_pop(&queue,current,input,512*1024,&unit);
''')
R('    bool ok=cp_video_push(&queue,epoch,data,size,pts,key);\n','''    { int64_t t=esp_timer_get_time(); if(last_arrive) add(arrive_s,(uint32_t)((t-last_arrive)/100)); last_arrive=t; pts=(uint64_t)t; }
    bool ok=cp_video_push(&queue,epoch,data,size,pts,key);
''')
R('    mutex=xSemaphoreCreateMutex();\n','''    arrive_s=(Series *)heap_caps_calloc(1,sizeof(Series),MALLOC_CAP_SPIRAM); show_s=(Series *)heap_caps_calloc(1,sizeof(Series),MALLOC_CAP_SPIRAM); age_s=(Series *)heap_caps_calloc(1,sizeof(Series),MALLOC_CAP_SPIRAM);
    mutex=xSemaphoreCreateMutex();
''')
open(p,'w').write(s)
p='main/app_ui.c'; s=open(p).read()
a='        if(!cp_touch_configure(&touch,width,height,frame_width,frame_height,profile->aspect_width,profile->aspect_height)) { bsp_display_unlock(); return false; }\n'
assert a in s
s=s.replace(a,a+'        ESP_LOGW("JITTER","area x=%ld y=%ld w=%ld h=%ld frame=%ux%u",(long)touch.x,(long)touch.y,(long)touch.width,(long)touch.height,frame_width,frame_height); /* TEMP */\n',1)
open(p,'w').write(s)
