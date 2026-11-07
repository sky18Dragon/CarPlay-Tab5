#include "offline_demo.h"
#include "app_ui.h"
#include "video_output.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern const uint8_t fixture_start[] asm("_binary_high_fixture_bin_start");
extern const uint8_t fixture_end[] asm("_binary_high_fixture_bin_end");
static void demo_touch(cp_touch_event event,void *context)
{
    (void)context;
    ESP_LOGI("DEMO","Local touch kind=%d x=%u y=%u",event.kind,event.x,event.y);
}
static void demo(void *arg)
{
    (void)arg;
    tab5_ui_touch_sink(demo_touch,NULL);
    tab5_ui_status(CP_SESSION,CP_ERR_NONE,true);
    tab5_video_start(1);
    tab5_ui_status(CP_STREAMING,CP_ERR_NONE,true);
    uint64_t pts=0;
    for(;;) {
        if(!tab5_video_submit(1,fixture_start,fixture_end-fixture_start,pts)) ESP_LOGW("DEMO","Frame not queued");
        pts+=250; vTaskDelay(pdMS_TO_TICKS(250));
    }
}
void tab5_offline_demo_start(void)
{
    if(xTaskCreate(demo,"offline_demo",4096,NULL,3,NULL)!=pdPASS) abort();
}
