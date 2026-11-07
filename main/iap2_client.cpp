// SPDX-License-Identifier: GPL-3.0-or-later
// Wired identity and authentication order follow local DiPlay-Android-public.
#include "iap2_client.h"
#include "usb_probe.h"
#include "core/iap2_params.h"
#include "core/iap2_auth_state.h"
#include "app_session.h"
#include "airplay_service.h"
#include "mux_channels.h"
#include "ncm_host.h"

#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "mbedtls/platform_util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern "C" {
#include "vendor/iap2_core.h"
#include "airplay_identity_port.h"



#include "auth_port.h"
}

namespace {
constexpr char tag[]="IAP2";
uint64_t now() { return uint64_t(esp_timer_get_time()/1000); }
struct Client {
    const Iap2Transport &transport;
    explicit Client(const Iap2Transport &t):transport(t) {}
    iap2_core_t *core=nullptr;
    bool service_started=false;
    uint32_t epoch=0;
    bool availability_pending=false;
    bool stopped=false,identity_sent=false,identified=false,certificate_sent=false,signature_pending=false,signature_acked=false,authenticated=false;
    uint64_t deadline=0;
    iap2::AuthState auth;
    void fail(const char *reason,cp_event event=CP_IO_ERROR) {
        auth.reject(); ESP_LOGE(tag,"CONTROL_STOP reason=%s",reason); tab5_session_post(epoch,event); stopped=true;
    }
    bool send(uint16_t id,const uint8_t *body,size_t size,bool signature=false) {
        bool ok=iap2_core_send_csm_with_ack_callback(core,id,body,size,signature?acked:nullptr,signature?this:nullptr,now());
        if (!ok) fail("csm_send_failed");
        return ok;
    }
    template<size_t N> bool send(uint16_t id,const iap2::Body<N> &body,bool signature=false) {
        if (!body.ok()) { fail("body_capacity"); return false; }
        return send(id,body.data(),body.size(),signature);
    }
    static bool tx(const uint8_t *p,size_t n,void *ctx) {
        auto &c=*static_cast<Client *>(ctx); return c.transport.write(c.transport.context,p,n);
    }
    static void state(iap2_core_state_t state,const char *reason,void *ctx) {
        auto &c=*static_cast<Client *>(ctx);
        ESP_LOGI(tag,"LINK_STATE=%u reason=%s",unsigned(state),reason?reason:"");
        if (state==IAP2_CORE_NEGOTIATING) ESP_LOGI(tag,"IAP2_MARKER_OK; synchronizing link");
        if (state==IAP2_CORE_READY) { ESP_LOGI(tag,"IAP2_LINK_READY"); c.deadline=now()+30000; }
        if (state==IAP2_CORE_DEAD) c.stopped=true;
    }
    static void acked(uint16_t id,void *ctx) {
        auto &c=*static_cast<Client *>(ctx);
        if (id==0xaa03 && c.signature_pending) { if(!c.auth.signature_acknowledged()) { c.fail("invalid_signature_ack"); return; } c.signature_acked=true; ESP_LOGI(tag,"MFI_SIGNATURE_ACKED"); }
    }
    void identity() {
        int ncm_interface=tab5_usb_ncm_interface();
        if(ncm_interface<0) ncm_interface=CONFIG_TAB5_IAP2_NCM_INTERFACE;
        if(ncm_interface<0) { fail("ncm_profile_not_configured",CP_PROFILE_MISSING); return; }
        iap2::Body<> b;
        uint8_t mac[6]; if(esp_efuse_mac_get_default(mac)!=ESP_OK) { fail("chip_identity_unavailable"); return; }
        char serial[13]; snprintf(serial,sizeof(serial),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        b.text(0,"CarPlay Tab5"); b.text(1,"ESP32-P4 Tab5"); b.text(2,"CarPlay-Tab5 project"); b.text(3,serial); b.text(4,"0.3"); b.text(5,"Tab5");
        // Wired control profile from the working DiPlay implementation. The phone
        // rejects parameters 6 and 7 when only authentication and power messages
        // are declared. Update messages are consumed below; no EA, location or
        // vehicle data is offered.
        constexpr uint16_t sent[]={0xaa01,0xaa03,0x5000,0x5002,0x5200,0x5203,0xae00,0xae02,0x4157,0x4159,0x4154,0x4156,0xae03,0x4301};
        constexpr uint16_t received[]={0xaa00,0xaa02,0xaa04,0xaa05,0x5001,0x5201,0x5202,0xae01,0x4158,0x4155,0x4300};
        b.list(6,sent,sizeof(sent)/sizeof(*sent)); b.list(7,received,sizeof(received)/sizeof(*received));
        b.u8(8,2); b.u16(9,20); b.text(12,"en"); b.text(13,"en");
        iap2::Body<128> usb;
        usb.u16(0,0); usb.text(1,"USBHostTransport"); usb.add(2,nullptr,0);
        usb.u8(3,ncm_interface); // Explicitly provisioned phone profile, not inferred from S3.
        usb.add(4,nullptr,0); b.group(16,usb);
        // Display component required by the route-guidance messages declared above.
        iap2::Body<96> route;
        route.u16(0,42); route.text(1,"RouteGuidance"); route.u16(2,64); route.u16(4,64); route.u16(6,8); b.group(30,route);
        if (send(0x1d01,b)) { identity_sent=true; ESP_LOGI(tag,"IDENTIFICATION_SENT bytes=%u usb_interface=%d",unsigned(b.size()),ncm_interface); }
    }
    void certificate() {
        // This wired iPhone requests authentication before StartIdentification.
        // Keep those two exchanges independent; never infer either one's success.
        if(!auth_port_is_configured()) { fail("authentication_identity_missing",CP_AUTH_MISSING); return; }
        uint8_t bytes[1536]; size_t length=sizeof(bytes);
        esp_err_t e=auth_port_read_certificate(bytes,&length);
        if (e!=ESP_OK || !length || length>sizeof(bytes)) { fail("authentication_identity_missing",CP_AUTH_MISSING); return; }
        iap2::Body<1540> b; b.add(0,bytes,length);
        if (send(0xaa01,b)) { if(!auth.certificate_sent()) { fail("duplicate_certificate_request"); return; } certificate_sent=true; ESP_LOGI(tag,"MFI_CERTIFICATE_SENT bytes=%u",unsigned(length)); }

    }
    void challenge(const uint8_t *body,size_t size) {
        iap2::Parameter p;
        if (!auth.can_sign() || !certificate_sent || signature_pending || !iap2::parameter_zero(body,size,p) || p.size!=32) { fail("invalid_authentication_challenge"); return; }
        uint8_t signature[64]; size_t length=sizeof(signature);
        esp_err_t e=auth_port_sign_challenge(p.data,p.size,signature,&length);
        if (e!=ESP_OK || length!=64) { mbedtls_platform_zeroize(signature,sizeof(signature)); fail("challenge_signing_failed"); return; }
        iap2::Body<68> b; b.add(0,signature,length);
        if(!auth.signature_sent()) { mbedtls_platform_zeroize(signature,sizeof(signature)); fail("invalid_signature_state"); return; }
        signature_pending=true;
        if (send(0xaa03,b,true)) ESP_LOGI(tag,"MFI_SIGNATURE_SENT digest_bytes=32 signature_bytes=64");
        mbedtls_platform_zeroize(signature,sizeof(signature));

    }
    void accepted() {
        if (!certificate_sent || !signature_pending || !signature_acked || authenticated || !auth.accept()) { fail("unexpected_authentication_success"); return; }
        authenticated=true; deadline=now()+30000;
        ESP_LOGI(tag,"IAP2_AUTHENTICATED phone accepted challenge response");
        if (identified) ready();
    }
    void ready() {
        tab5_session_post(epoch,CP_AUTH_OK);
        ESP_LOGI(tag,"IAP2_CONTROL_READY authentication and identification accepted");
        // Do not claim that the unknown board VBUS path can deliver a charging current.
        iap2::Body<16> power; power.u16(0,0); power.u8(1,0); send(0xae03,power);
        ESP_LOGI(tag,"Waiting for CarPlayAvailability before starting wired AirPlay");
    }
    void start_media() {
        if (!availability_pending || !identified || !authenticated) return;
        char address[64];
        if (!ncm_host_ipv6(address,sizeof(address))) return;
        availability_pending=false;
        if(!service_started) {
            if(tab5_airplay_start(epoch)!=ESP_OK) { fail("airplay_listener_failed"); return; }
            service_started=true;
        }
        airplay_identity_config_t config={};
        if(airplay_identity_port_get_config(&config)!=ESP_OK) { fail("airplay_config_failed"); return; }
        tab5_session_post(epoch,CP_NETWORK_OK);
        iap2::Body<> body; iap2::Body<96> wired;
        wired.text(0,address); body.group(0,wired);
        body.u32(2,tab5_airplay_port());
        body.text(3,config.device_identifier); body.text(4,config.public_key); body.text(5,config.source_version);
        deadline=0;
        if (send(0x4301,body)) ESP_LOGI(tag,"CARPLAY_START_SESSION IPv6=%s port=%u",address,tab5_airplay_port());
    }
    static void message(uint16_t id,const uint8_t *body,size_t size,void *ctx) {
        auto &c=*static_cast<Client *>(ctx);
        if (c.stopped) return;
        ESP_LOGI(tag,"CSM_RX id=%04x bytes=%u",id,unsigned(size));
        switch (id) {
        case 0x1d00: c.identity(); break;
        case 0x1d02:
            if (!c.identity_sent) { c.fail("unsolicited_identification_success"); break; }
            c.identified=true; c.deadline=now()+30000;
            ESP_LOGI(tag,"IAP2_IDENTIFIED");
            if (c.authenticated) c.ready();
            break;
        case 0x1d03: {
            iap2::Parameters cursor(body,size); iap2::Parameter p;
            while (cursor.next(p)) ESP_LOGW(tag,"IDENTIFICATION_REJECTED parameter=%u bytes=%u",p.id,unsigned(p.size));
            c.fail(cursor.complete()?"identification_rejected":"malformed_identification_rejection"); break;
        }
        case 0xaa00: c.certificate(); break;
        case 0xaa02: c.challenge(body,size); break;
        case 0xaa04: c.fail("phone_rejected_authentication",CP_AUTH_REJECTED); break;
        case 0xaa05: c.accepted(); break;
        case 0x4300: c.availability_pending=true; ESP_LOGI(tag,"CARPLAY_AVAILABILITY received"); break;
        case 0x5001: case 0x5201: case 0x5202: case 0xae01: case 0x4158: case 0x4155:
            // Consume notifications without inventing measurements or app data.
            break;
        default: ESP_LOGW(tag,"Unhandled CSM id=%04x",id); break;
        }
    }
};
}

void iap2_client_run(const Iap2Transport &transport,uint32_t epoch) {
    Client c{transport}; c.epoch=epoch;
    c.core=iap2_core_create(Client::tx,Client::message,Client::state,&c);
    if (!c.core) { ESP_LOGE(tag,"Link allocation failed"); return; }
    c.deadline=now()+30000;
    iap2_core_start(c.core,now());
    uint8_t bytes[2048];
    while (!c.stopped && mux_transport_alive(epoch)) {
        int n=transport.read(transport.context,bytes,sizeof(bytes),50);
        if (n<0) { c.fail("service_closed"); break; }
        if (n) iap2_core_feed(c.core,bytes,size_t(n),now());
        iap2_core_advance_time(c.core,now());
        c.start_media();
        if (c.deadline && now()>c.deadline) c.fail("control_stage_timeout");
        vTaskDelay(1);
    }
    ESP_LOGI(tag,"CONTROL_END identified=%u authenticated=%u rx_messages=%lu bad_frames=%lu",c.identified,c.authenticated,
        (unsigned long)iap2_core_rx_frames(c.core),(unsigned long)iap2_core_bad_frames(c.core));
    iap2_core_destroy(c.core);
    if(c.service_started) tab5_airplay_stop();
}
