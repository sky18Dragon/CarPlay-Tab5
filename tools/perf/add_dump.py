p='main/video_output.cpp'; s=open(p).read()
a='namespace {\n'
assert a in s
s=s.replace(a,'''#include "mbedtls/base64.h"
extern "C" { volatile int g_dump_request; }
namespace {
/* TEMP frame dump */
void dump_slot(const uint8_t *m,unsigned w,unsigned h) {
    static unsigned char out[4100]; size_t size=(size_t)w*h*3/2;
    printf("FRAMEDUMP %u %u\\n",w,h);
    for(size_t off=0;off<size;off+=3000) { size_t n=size-off<3000?size-off:3000,olen=0;
        mbedtls_base64_encode(out,sizeof(out),&olen,m+off,n); out[olen]=0; printf("F:%u:%s\\n",(unsigned)off,out); fflush(stdout); vTaskDelay(1); }
    printf("FRAMEDONE\\n"); fflush(stdout);
}
''',1)
a='                stats.width=showing->width; stats.height=showing->height;\n'
assert a in s
s=s.replace(a,a+'                if(g_dump_request) { g_dump_request=0; dump_slot(showing->memory,showing->width,showing->height); }\n',1)
open(p,'w').write(s)
p='main/app_ui.c'; s=open(p).read()
a='    ESP_LOGW("VIDEO","MOTION start");\n'
assert a in s
pass
a='    ESP_LOGW("VIDEO","MOTION end");\n'
assert a in s
s=s.replace(a,a+'    { extern volatile int g_dump_request; g_dump_request=1; }\n',1)
open(p,'w').write(s)
