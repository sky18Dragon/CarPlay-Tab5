#include "media_routes.h"
#include "video_output.h"
#include "core/video_queue.h"
#include "app_ui.h"
#include "hid_output.h"
extern "C" {
#include "airplay_control_port.h"
}
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
namespace {
SemaphoreHandle_t lock;
uint8_t config[4096],*scratch;
size_t config_size;
uint32_t config_repeats;
uint64_t epoch=1;
// A frozen picture waits for this request, and it is sent once per overflow.
constexpr uint32_t IDR_POLL_MS=20;
void control_worker(void *) {
    bool pending=false; uint64_t seen=0;
    for(;;) {
        xSemaphoreTake(lock,portMAX_DELAY);
        uint64_t current=epoch; bool active=config_size!=0;
        xSemaphoreGive(lock);
        if(current!=seen) { pending=false; seen=current; }
        if(tab5_video_needs_idr()) pending=true;
        if(active && pending) {
            bool sent=airplay_control_port_request_main_screen_keyframe();
            ESP_LOGI("VIDEO","IDR_REQUEST sent=%d config_repeats=%lu",sent,(unsigned long)config_repeats);
            if(sent) pending=false;
        }
        vTaskDelay(pdMS_TO_TICKS(IDR_POLL_MS));
    }
}
}
esp_err_t tab5_media_routes_init(void) {
    if(lock) return ESP_OK;
    lock=xSemaphoreCreateMutex();
    scratch=(uint8_t *)heap_caps_malloc(512*1024,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!lock || !scratch) {
        if(lock) vSemaphoreDelete(lock);
        free(scratch); lock=nullptr; scratch=nullptr; return ESP_ERR_NO_MEM;
    }
    if(xTaskCreate(control_worker,"media_control",4096,nullptr,3,nullptr)!=pdPASS) {
        vSemaphoreDelete(lock);free(scratch);lock=nullptr;scratch=nullptr;return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
extern "C" void video_live_reset_stream(void) {
    if(!lock) return;
    xSemaphoreTake(lock,portMAX_DELAY);
    ++epoch; if(!epoch) ++epoch;
    config_size=0; memset(config,0,sizeof(config)); tab5_video_stop(); tab5_hid_reset();
    xSemaphoreGive(lock);
}
extern "C" esp_err_t video_live_config(const uint8_t *data,size_t size) {
    if(!lock) return ESP_ERR_INVALID_STATE;
    if(!data || !size || size>sizeof(config)) return ESP_ERR_INVALID_SIZE;
    xSemaphoreTake(lock,portMAX_DELAY);
    // Restarting clears the canvas and queue, and the queue reset asks for a
    // keyframe. The phone answers that with these same bytes, so a restart on an
    // unchanged config loops forever and no frame stays on screen.
    if(!cp_video_config_changed(config,config_size,data,size)) { ++config_repeats; xSemaphoreGive(lock); return ESP_OK; }
    memcpy(config,data,size); config_size=size;
    ++epoch; if(!epoch) ++epoch;
    ESP_LOGI("VIDEO","CONFIG bytes=%u epoch=%llu",(unsigned)size,(unsigned long long)epoch);
    tab5_video_start(epoch);
    xSemaphoreGive(lock); return ESP_OK;
}
extern "C" esp_err_t video_live_frame_timed(const uint8_t *data,size_t size,uint64_t sender_ns,uint64_t arrival_us) {
    (void)arrival_us;
    if(!lock) return ESP_ERR_INVALID_STATE;
    if(!data || !size || size>512*1024) return ESP_ERR_INVALID_SIZE;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(!config_size || size>512*1024-config_size) { xSemaphoreGive(lock); return ESP_ERR_INVALID_SIZE; }
    // Repeat cached Annex-B SPS/PPS ahead of each AU. Decoder recovery therefore
    // does not depend on the sender repeating a configuration message after IDR.
    memcpy(scratch,config,config_size); memcpy(scratch+config_size,data,size);
    bool ok=tab5_video_submit(epoch,scratch,size+config_size,sender_ns/1000000);
    xSemaphoreGive(lock);
    return ok?ESP_OK:ESP_ERR_NO_MEM;
}
extern "C" esp_err_t video_live_frame(const uint8_t *data,size_t size) {
    return video_live_frame_timed(data,size,0,0);
}
