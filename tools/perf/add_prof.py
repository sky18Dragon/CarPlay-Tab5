import sys
s=open('main/offline_demo.c').read()
a='static void bench(void)\n{\n'
assert a in s
s=s.replace(a,'''#include "driver/gptimer.h"
#include "riscv/csr.h"
#define MAXS 12000
static uint32_t *pcs[2],*ras[2]; static volatile int npc[2],nra[2]; static volatile bool sampling;
static bool IRAM_ATTR on_alarm(gptimer_handle_t t,const gptimer_alarm_event_data_t *e,void *arg)
{
    int c=(int)arg; if(sampling && npc[c]<MAXS) { uint32_t pc=RV_READ_CSR(mepc); pcs[c][npc[c]]=pc; npc[c]=npc[c]+1;
        if(pc>=0x4fc00000 && pc<0x4fd00000) { uint32_t **tcb=(uint32_t **)xTaskGetCurrentTaskHandleForCore(c); uint32_t *frame=*tcb; ras[c][nra[c]]=frame[1]; nra[c]=nra[c]+1; } } return false;
}
static void timer_task(void *arg)
{
    gptimer_handle_t tm; gptimer_config_t cfg={.clk_src=GPTIMER_CLK_SRC_DEFAULT,.direction=GPTIMER_COUNT_UP,.resolution_hz=1000000};
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg,&tm));
    gptimer_event_callbacks_t cb={.on_alarm=on_alarm}; ESP_ERROR_CHECK(gptimer_register_event_callbacks(tm,&cb,arg));
    gptimer_alarm_config_t al={.alarm_count=(int)arg?953:997,.reload_count=0,.flags.auto_reload_on_alarm=true};
    ESP_ERROR_CHECK(gptimer_set_alarm_action(tm,&al)); ESP_ERROR_CHECK(gptimer_enable(tm)); ESP_ERROR_CHECK(gptimer_start(tm));
    vTaskDelete(NULL);
}
static void prof_init(void)
{
    for(int c=0;c<2;++c) { pcs[c]=heap_caps_malloc(MAXS*4,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT); if(!pcs[c]) pcs[c]=heap_caps_malloc(MAXS*4,MALLOC_CAP_SPIRAM); ras[c]=heap_caps_malloc(MAXS*4,MALLOC_CAP_SPIRAM); xTaskCreatePinnedToCore(timer_task,"t",4096,(void*)c,10,NULL,c); }
    vTaskDelay(pdMS_TO_TICKS(50));
}
static void prof_dump(void)
{
    for(int c=0;c<2;++c) { int n=npc[c]; printf("PCDUMP %d %d\\n",c,n);
        for(int i=0;i<n;i+=64) { printf("P%d:",c); for(int j=i;j<n&&j<i+64;++j) printf("%08lx",(unsigned long)pcs[c][j]); printf("\\n"); fflush(stdout); vTaskDelay(1); } }
    for(int c=0;c<2;++c) { int n=nra[c]; for(int i=0;i<n;i+=64) { printf("R%d:",c); for(int j=i;j<n&&j<i+64;++j) printf("%08lx",(unsigned long)ras[c][j]); printf("\\n"); fflush(stdout); vTaskDelay(1); } }
    printf("PCDONE\\n"); fflush(stdout);
}
'''+a,1)
a='        int64_t begin=esp_timer_get_time();\n'
assert a in s
s=s.replace(a,'        if(round==3) { npc[0]=npc[1]=nra[0]=nra[1]=0; sampling=true; }\n'+a,1)
a='        cp_decoder_wait(d);\n        int64_t total=esp_timer_get_time()-begin;\n'
assert a in s
s=s.replace(a,a+'        if(round==3) { sampling=false; prof_dump(); }\n',1)
a='    cp_decoder *d=cp_decoder_create(1280,720);\n    cp_decoder_set_sink(d,sink,NULL);\n'
assert a in s
s=s.replace(a,'    prof_init();\n'+a,1)
open('main/offline_demo.c','w').write(s)
s=open('main/CMakeLists.txt').read()
a='REQUIRES auth_software_provider'
assert a in s
s=s.replace(a,'REQUIRES esp_driver_gptimer auth_software_provider',1)
open('main/CMakeLists.txt','w').write(s)
