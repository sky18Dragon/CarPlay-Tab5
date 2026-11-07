#include "hid_output.h"
#include "app_ui.h"
#include "core/hid_report.h"
extern "C" {
#include "airplay_control_port.h"
}
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <atomic>
namespace {
struct Event { cp_touch_event touch; uint32_t epoch; };
QueueHandle_t events;
std::atomic<bool> cancel{false};
void touch(cp_touch_event event,void *) {
    Event item{event,airplay_control_port_hid_epoch()};
    if(item.epoch && xQueueSend(events,&item,0)!=pdTRUE) cancel=true;
}
void worker(void *) {
    cp_touch_event last={CP_TOUCH_UP,0,0};
    bool release_pending=false; uint32_t epoch=0;
    for(;;) {
        uint32_t current=airplay_control_port_hid_epoch();
        if(current!=epoch) { epoch=current; xQueueReset(events); release_pending=false; last.kind=CP_TOUCH_UP; }
        if(cancel.exchange(false)) { xQueueReset(events); release_pending=true; }
        if(release_pending) {
            last.kind=CP_TOUCH_UP; uint8_t report[12]; cp_hid_report(last,report);
            if(airplay_control_port_send_hid_for_epoch(epoch,0x2a2a2a2a,report,12)) release_pending=false;
            else { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        }
        Event item;
        if(xQueueReceive(events,&item,pdMS_TO_TICKS(50))!=pdTRUE) continue;
        if(cancel.load() || item.epoch!=epoch) continue;
        cp_touch_event event=item.touch;
        uint8_t report[12]; cp_hid_report(event,report);
        if(airplay_control_port_send_hid_for_epoch(epoch,0x2a2a2a2a,report,12)) last=event;
        else { xQueueReset(events); release_pending=true; }
    }
}
}
esp_err_t tab5_hid_init(void) {
    if(events) return ESP_OK;
    events=xQueueCreate(16,sizeof(Event)); if(!events) return ESP_ERR_NO_MEM;
    if(xTaskCreate(worker,"hid_send",4096,nullptr,4,nullptr)!=pdPASS) {
        vQueueDelete(events); events=nullptr; return ESP_ERR_NO_MEM;
    }
    tab5_ui_touch_sink(touch,nullptr); return ESP_OK;
}
void tab5_hid_reset(void) { cancel=true; }
