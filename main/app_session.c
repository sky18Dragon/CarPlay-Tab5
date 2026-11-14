#include "app_session.h"
#include "app_ui.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdatomic.h>
typedef struct { uint32_t transport; cp_event event; } message;
static QueueHandle_t events;
static cp_session state;
static uint32_t transport;
static atomic_bool overflow;
static cp_stage shown=CP_FAILED;
static cp_error shown_error=CP_ERR_AUTH_REJECTED;
static uint64_t now(void) { return esp_timer_get_time()/1000; }
static void display(void) {
    if(shown==state.stage && shown_error==state.error) return;
    shown=state.stage; shown_error=state.error;
    ESP_LOGI("SESSION","stage=%s error=%u",cp_stage_name(shown),(unsigned)shown_error);
    tab5_ui_status(shown,shown_error);
}
esp_err_t tab5_session_init(void) {
    if(events) return ESP_OK;
    events=xQueueCreate(24,sizeof(message));
    if(!events) return ESP_ERR_NO_MEM;
    cp_session_init(&state); return ESP_OK;
}
void tab5_session_attach(uint32_t epoch) {
    transport=epoch;
    cp_session_event(&state,0,CP_ATTACH,now());
    cp_session_event(&state,state.generation,CP_ENUM_OK,now());
    display();
}
void tab5_session_detach(void) {
    transport=0; cp_session_event(&state,0,CP_DETACH,now()); display();
}
bool tab5_session_post(uint32_t epoch,cp_event event) {
    if(!events || !epoch || event==CP_ATTACH || event==CP_DETACH || event==CP_DRAINED || event==CP_RETRY) return false;
    message msg={epoch,event};
    if(xQueueSend(events,&msg,0)==pdTRUE) return true;
    atomic_store(&overflow,true); return false;
}
cp_action tab5_session_poll(void) {
    cp_action action=CP_ACTION_NONE;
    if(atomic_exchange(&overflow,false)) action=cp_session_event(&state,state.generation,CP_IO_ERROR,now());
    message msg;
    while(xQueueReceive(events,&msg,0)==pdTRUE) {
        if(msg.transport!=transport || !transport) continue;
        cp_action next=cp_session_event(&state,state.generation,msg.event,now());
        if(next!=CP_ACTION_NONE) action=next;
    }
    cp_action next=cp_session_tick(&state,now());
    if(next!=CP_ACTION_NONE) action=next;
    display(); return action;
}
void tab5_session_drained(void) {
    cp_session_event(&state,state.generation,CP_DRAINED,now()); display();
}
