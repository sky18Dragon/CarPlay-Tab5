#include "iap2_client.h"
#include "app_session.h"
#include "airplay_service.h"
#include "core/iap2_params.h"
extern "C" {
#include "vendor/iap2_core.h"
#include "airplay_identity_port.h"
}
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
struct iap2_core { iap2_core_message_cb_t message; iap2_core_state_cb_t state;void *context; };
extern "C" int tab5_usb_ncm_interface(void) { return -1; }
static iap2_core core;
static unsigned step,starts,stops;
static int scenario;
static uint64_t clock_ms;
static std::vector<uint16_t> sent;
static std::vector<cp_event> events;
static bool identification_checked;
static bool lists(const iap2::Parameter &list,uint16_t id) {
 for(size_t i=0;i+1<list.size;i+=2)if(iap2::be16(list.data+i)==id)return true;
 return false;
}
// The phone rejects parameters 6 and 7 when only authentication and power
// messages are declared, and the route-guidance messages need component 30.
static void check_identification(const uint8_t *p,size_t n) {
 iap2::Parameters params(p,n);iap2::Parameter param;
 bool sent_list=false,received_list=false,route=false;
 while(params.next(param)) {
  if(param.id==6) {
   sent_list=true;
   for(uint16_t id:{0xaa01,0xaa03,0x5000,0x5002,0x5200,0x5203,0xae00,0xae02,0x4157,0x4159,0x4154,0x4156,0xae03,0x4301})assert(lists(param,id));
  }
  if(param.id==7) {
   received_list=true;
   for(uint16_t id:{0xaa00,0xaa02,0xaa04,0xaa05,0x5001,0x5201,0x5202,0xae01,0x4158,0x4155,0x4300})assert(lists(param,id));
  }
  route|=param.id==30;
 }
 assert(params.complete() && sent_list && received_list && route);
 identification_checked=true;
}
extern "C" int64_t esp_timer_get_time(void) { return clock_ms*1000; }
bool mux_transport_alive(uint32_t epoch) { return epoch==7 && step<8; }
extern "C" bool tab5_session_post(uint32_t epoch,cp_event e) { assert(epoch==7);events.push_back(e);return true; }
extern "C" bool ncm_host_ipv6(char *p,size_t n) { if(scenario==4)return false;snprintf(p,n,"fe80::1234");return true; }
extern "C" esp_err_t tab5_airplay_start(uint64_t epoch) { assert(epoch==7);++starts;return scenario==5?ESP_FAIL:ESP_OK; }
extern "C" void tab5_airplay_stop(void) { ++stops; }
extern "C" uint16_t tab5_airplay_port(void) { return 7000; }
extern "C" esp_err_t airplay_identity_port_get_config(airplay_identity_config_t *p) { *p={"device","public","version"};return ESP_OK; }
extern "C" int auth_port_is_configured(void) { return scenario!=1; }
extern "C" esp_err_t auth_port_read_certificate(uint8_t *p,size_t *n) { assert(*n>=4);memset(p,1,4);*n=4;return ESP_OK; }
extern "C" esp_err_t auth_port_sign_challenge(const uint8_t *,size_t n,uint8_t *p,size_t *len) { assert(n==32 && *len>=64);memset(p,2,64);*len=64;return ESP_OK; }
extern "C" iap2_core_t *iap2_core_create(iap2_core_tx_cb_t,iap2_core_message_cb_t m,iap2_core_state_cb_t s,void *ctx) { core={m,s,ctx};return &core; }
extern "C" void iap2_core_destroy(iap2_core_t *p) { assert(p==&core); }
extern "C" bool iap2_core_start(iap2_core_t *,uint64_t) { core.state(IAP2_CORE_READY,"test",core.context);return true; }
extern "C" void iap2_core_advance_time(iap2_core_t *,uint64_t) {}
extern "C" uint32_t iap2_core_rx_frames(const iap2_core_t *) { return step; }
extern "C" uint32_t iap2_core_bad_frames(const iap2_core_t *) { return 0; }
extern "C" bool iap2_core_send_csm_with_ack_callback(iap2_core_t *,uint16_t id,const uint8_t *p,size_t n,iap2_core_csm_acked_cb_t ack,void *ctx,uint64_t) {
 sent.push_back(id);
 iap2::Parameters params(p,n);iap2::Parameter param;while(params.next(param)) {}
 assert(params.complete());
 if(id==0x4301) { assert(starts==1);assert(n>0); }
 if(id==0x1d01)check_identification(p,n);
 if(ack && scenario!=3) ack(id,ctx);
 return true;
}
extern "C" void iap2_core_feed(iap2_core_t *,const uint8_t *,size_t,uint64_t) {
 static const uint16_t script[]={0xaa00,0xaa02,0xaa05,0x1d00,0x1d02,0x4300};
 if(step>=6)return;
 uint16_t id=script[step];
 if(scenario==2 && step==0)id=0xaa05;
 iap2::Body<40> body;uint8_t challenge[32]={};
 if(id==0xaa02)body.add(0,challenge,32);
 core.message(id,body.data(),body.size(),core.context);
 ++step;
}
static bool write_bytes(void *,const uint8_t *,size_t) { return true; }
static int read_bytes(void *,uint8_t *p,size_t,unsigned) { if(step>=6){clock_ms+=31000;++step;return 0;}*p=1;return 1; }
// The read index advances after dispatch, via the clock queried by the loop.
// Feed wrapper below cannot advance before accessing script; advance at end.
int main() {
 for(scenario=0;scenario<=5;++scenario) {
  step=starts=stops=0;clock_ms=0;sent.clear();events.clear();
  Iap2Transport io{nullptr,write_bytes,read_bytes};iap2_client_run(io,7);
  bool start_sent=false;for(auto id:sent)start_sent|=id==0x4301;
  if(CONFIG_TAB5_IAP2_NCM_INTERFACE<0) {
   assert(!starts && !stops && !start_sent);
   if(scenario==0 || scenario>=4) {
    bool found=false;for(auto e:events)found|=e==CP_PROFILE_MISSING;assert(found);
   }
  }
  else if(scenario==0)assert(starts==1 && stops==1 && start_sent && identification_checked);
  else if(scenario==5)assert(starts==1 && stops==0 && !start_sent);
  else assert(!starts && !stops && !start_sent);
 }
 puts("production iAP2 client: success/start/stop, missing provider, premature success, missing ACK, NCM timeout and service failure passed");
}
