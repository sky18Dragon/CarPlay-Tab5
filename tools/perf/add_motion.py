p='main/app_ui.c'; s=open(p).read()
a='bool tab5_ui_frame(const uint8_t *data'
s=s.replace(a,'''/* TEMP motion test */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
static void motion_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(16000));
    const unsigned w=touch.video_width,h=touch.video_height;
    if(!w) { vTaskDelete(NULL); }
    ESP_LOGW("VIDEO","MOTION start");
    int64_t end=esp_timer_get_time()+40000000;
    for(int dir=1;esp_timer_get_time()<end;dir=-dir) {
        int x=dir>0?(int)(w*15/100):(int)(w*50/100),y=(int)(h/2);
        deliver((cp_touch_event){CP_TOUCH_DOWN,(uint16_t)x,(uint16_t)y});
        for(int i=0;i<40;++i) {
            x+=dir*(int)(w*35/100)/40; y+=(i%2)?1:-1;
            deliver((cp_touch_event){CP_TOUCH_MOVE,(uint16_t)x,(uint16_t)y});
            vTaskDelay(pdMS_TO_TICKS(30));
        }
        deliver((cp_touch_event){CP_TOUCH_UP,(uint16_t)x,(uint16_t)y});
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    ESP_LOGW("VIDEO","MOTION end");
    vTaskDelete(NULL);
}
'''+a,1)
a='    const bool entering=!video_visible;\n'
s=s.replace(a,'    { static bool started; if(!started) { started=true; xTaskCreate(motion_task,"motion",4096,NULL,3,NULL); } }\n'+a,1)
open(p,'w').write(s)
