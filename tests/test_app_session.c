#include "app_session.h"
#include "freertos/queue.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint8_t queue[24][16];
static unsigned head,count,capacity; static size_t item_size;
static int64_t clock_ms;
static cp_stage displayed; static cp_error displayed_error;
QueueHandle_t xQueueCreate(unsigned n,size_t size) { assert(n<=24 && size<=16);capacity=n;item_size=size;return queue; }
int xQueueSend(QueueHandle_t q,const void *p,unsigned wait) { (void)q;(void)wait;if(count==capacity)return 0;memcpy(queue[(head+count++)%capacity],p,item_size);return 1; }
int xQueueReceive(QueueHandle_t q,void *p,unsigned wait) { (void)q;(void)wait;if(!count)return 0;memcpy(p,queue[head],item_size);head=(head+1)%capacity;--count;return 1; }
int64_t esp_timer_get_time(void) { return clock_ms*1000; }
void tab5_ui_status(cp_stage stage,cp_error error) {displayed=stage;displayed_error=error; }
int main(void) {
 assert(tab5_session_init()==ESP_OK);
 tab5_session_attach(1);assert(displayed==CP_USB_ADDRESSABLE);
 assert(tab5_session_post(1,CP_MUX_OK));assert(tab5_session_poll()==CP_ACTION_NONE);assert(displayed==CP_TRUST_PENDING);
 assert(tab5_session_post(1,CP_TRUST_OK));assert(tab5_session_post(1,CP_AUTH_BEGIN));
 tab5_session_poll();assert(displayed==CP_AUTHENTICATING);
 tab5_session_post(1,CP_AUTH_MISSING);tab5_session_post(1,CP_IO_ERROR);
 assert(tab5_session_poll()==CP_ACTION_STOP);tab5_session_drained();
 assert(displayed==CP_FAILED && displayed_error==CP_ERR_AUTH_MISSING);
 clock_ms=999999;assert(tab5_session_poll()==CP_ACTION_NONE);
 tab5_session_detach();tab5_session_drained();assert(displayed==CP_DETACHED);
 tab5_session_attach(3);tab5_session_post(1,CP_TRUST_OK);tab5_session_poll();assert(displayed==CP_USB_ADDRESSABLE);
 for(unsigned i=0;i<4;++i) {
  clock_ms+=10001;assert(tab5_session_poll()==CP_ACTION_STOP);tab5_session_drained();
  if(i<3) {
   assert(displayed==CP_BACKOFF);clock_ms+=1000u<<i;
   assert(tab5_session_poll()==CP_ACTION_RECONNECT);tab5_session_attach(5+i*2);
  } else assert(displayed==CP_FAILED);
 }
 tab5_session_detach();tab5_session_drained();tab5_session_attach(20);
 tab5_session_post(20,CP_STORAGE_ERROR);tab5_session_post(20,CP_IO_ERROR);
 assert(tab5_session_poll()==CP_ACTION_STOP);tab5_session_drained();
 assert(displayed==CP_FAILED && displayed_error==CP_ERR_STORAGE);
 clock_ms+=1000000;assert(tab5_session_poll()==CP_ACTION_NONE);
 puts("app session: adapter events, stale epoch, auth/storage terminal failures and bounded retries passed");
}
