// SPDX-License-Identifier: GPL-3.0-or-later
#include "ncm_host.h"
#include "core/ntb16.h"
#include "core/ncm_descriptor.h"
#include <atomic>
#include <new>
#include <stdio.h>
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_defaults.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace {
constexpr char tag[]="NCM";
usb_host_client_handle_t client;
usb_device_handle_t device;
esp_netif_t *netif;
usb_transfer_t *rx,*tx,*status,*control;
bool rx_busy,tx_busy,status_busy,control_busy,rx_done,tx_done,status_done,control_done;
bool control_claimed,data_claimed,failed;
static std::atomic<bool> fault; /* a running link failed; the probe re-enumerates */
std::atomic<bool> attached{false},started{false};
std::atomic<bool> quiescing{false};
uint8_t control_interface,data_interface,data_alt,ep_in,ep_out,ep_status,mac_index;
uint16_t in_mps,out_mps,status_mps,sequence;
uint8_t interface_mac[6];
enum class ControlStage { mac,alternate } control_stage;
int64_t report_deadline,control_deadline;
unsigned rx_frames,tx_frames,rx_blocks;
/* While CarPlay streams, the phone sends NCM data many times a second (at least
 * about 18 frames/s in field logs). A receive transfer that never completes
 * after a recovered USB channel error froze video for 11 minutes, so a link
 * that received steadily and then goes silent counts as failed. */
constexpr int64_t RX_STALL_US=5000000;
constexpr int64_t TX_STALL_US=3000000;
constexpr unsigned RX_STALL_ARM_BLOCKS=100;
static int64_t rx_last_at;
static int64_t tx_submitted_at;
static unsigned rx_blocks_at_last;
struct Packet { uint16_t size; uint8_t bytes[1514]; };
QueueHandle_t output;
StaticQueue_t output_control;
uint8_t *output_storage,*aggregate;
ncm::Stream *stream;

esp_err_t transmit(void *,void *data,size_t size) {
    if (!attached || !started || quiescing || size<14 || size>1514) return ESP_ERR_INVALID_STATE;
    Packet p; p.size=uint16_t(size); memcpy(p.bytes,data,size);
    if (xQueueSend(output,&p,0)!=pdTRUE) return ESP_ERR_NO_MEM;
    // The USB task sleeps until a USB event. Without this wake-up, a TCP ACK or touch
    // report waits for the next received block, or up to 100 ms when the phone is
    // itself waiting for that ACK.
    usb_host_client_unblock(client);
    return ESP_OK;
}
void free_rx(void *,void *p) { free(p); }
bool received(const uint8_t *p,size_t size,void *) {
    ++rx_frames;
    if (rx_frames<=4) ESP_LOGI(tag,"ETHERNET_RX bytes=%u type=%04x",unsigned(size),unsigned(p[12])<<8|p[13]);
    auto *copy=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!copy) return false;
    memcpy(copy,p,size);
    // Ownership transfers to esp-netif, which calls free_rx even on input failure.
    esp_netif_receive(netif,copy,size,nullptr);
    return true;
}
void rx_callback(usb_transfer_t *) { rx_busy=false; rx_done=true; }
void tx_callback(usb_transfer_t *) { tx_busy=false; tx_done=true; }
void status_callback(usb_transfer_t *) { status_busy=false; status_done=true; }
void control_callback(usb_transfer_t *) { control_busy=false; control_done=true; }
void link_up(const uint8_t mac[6]) {
    esp_err_t e=esp_netif_set_mac(netif,const_cast<uint8_t *>(mac));
    if (e!=ESP_OK) { failed=true; ESP_LOGE(tag,"set_mac: %s",esp_err_to_name(e)); return; }
    started=true;
    esp_netif_action_start(netif,nullptr,0,nullptr);
    esp_netif_action_connected(netif,nullptr,0,nullptr);
    e=esp_netif_create_ip6_linklocal(netif);
    ESP_LOGI(tag,"INTERFACE_READY ctrl=%u data=%u/%u IN=%02x OUT=%02x MPS=%u/%u IPv6=%s",
        control_interface,data_interface,data_alt,ep_in,ep_out,in_mps,out_mps,esp_err_to_name(e));
}
bool discover(const uint8_t *p,size_t size) {
    cp_ncm_descriptor d={};
    if(!cp_ncm_discover(p,size,&d)) return false;
    control_interface=d.control; data_interface=d.data; data_alt=d.alternate;
    ep_in=d.in; ep_out=d.out; ep_status=d.notification; mac_index=d.mac_string;
    in_mps=d.in_mps; out_mps=d.out_mps; status_mps=d.notification_mps;
    return true;
}
}

esp_err_t ncm_host_init() {
    if(netif) return ESP_OK;
    esp_err_t e=esp_netif_init(); if (e!=ESP_OK) return e;
    e=esp_event_loop_create_default(); if (e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) return e;
    output_storage=static_cast<uint8_t *>(heap_caps_malloc(8*sizeof(Packet),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    aggregate=static_cast<uint8_t *>(heap_caps_malloc(65535,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!output_storage || !aggregate) { free(output_storage); free(aggregate); output_storage=aggregate=nullptr; return ESP_ERR_NO_MEM; }
    output=xQueueCreateStatic(8,sizeof(Packet),output_storage,&output_control);
    if (!output) { free(output_storage); free(aggregate); output_storage=aggregate=nullptr; return ESP_ERR_NO_MEM; }
    esp_netif_inherent_config_t base=ESP_NETIF_INHERENT_DEFAULT_ETH();
    base.flags=esp_netif_flags_t(ESP_NETIF_FLAG_AUTOUP|ESP_NETIF_FLAG_MLDV6_REPORT);
    base.if_key="CARPLAY_NCM"; base.if_desc="iPhone USB"; base.route_prio=10;
    esp_netif_driver_ifconfig_t driver={}; driver.handle=output; driver.transmit=transmit; driver.driver_free_rx_buffer=free_rx;
    esp_netif_config_t config={&base,&driver,ESP_NETIF_NETSTACK_DEFAULT_ETH};
    netif=esp_netif_new(&config);
    if(!netif) { vQueueDelete(output); output=nullptr; free(output_storage); free(aggregate); output_storage=aggregate=nullptr; return ESP_ERR_NO_MEM; }
    return ESP_OK;
}
esp_err_t ncm_host_start(usb_host_client_handle_t c,usb_device_handle_t d) {
    const usb_config_desc_t *cfg=nullptr;
    if (device || !netif || usb_host_get_active_config_descriptor(d,&cfg)!=ESP_OK) return ESP_ERR_INVALID_STATE;
    if (!discover(reinterpret_cast<const uint8_t *>(cfg),cfg->wTotalLength)) {
        ESP_LOGW(tag,"Unsupported descriptors ctrl=%u data=%u IN=%02x OUT=%02x MAC_string=%u",control_interface,data_interface,ep_in,ep_out,mac_index);
        return ESP_ERR_NOT_FOUND;
    }
    client=c; device=d; failed=started=false; sequence=0; rx_frames=tx_frames=rx_blocks=0; rx_blocks_at_last=0; rx_last_at=esp_timer_get_time();
    quiescing=false; fault=false; tx_submitted_at=0;
    report_deadline=esp_timer_get_time()+10000000;
    xQueueReset(output);
    delete stream; stream=new(std::nothrow) ncm::Stream(aggregate,65535,in_mps);
    if (!stream) { ncm_host_release(); return ESP_ERR_NO_MEM; }
    esp_err_t e=usb_host_interface_claim(c,d,control_interface,0); control_claimed=e==ESP_OK;
    if (e==ESP_OK) { e=usb_host_interface_claim(c,d,data_interface,data_alt); data_claimed=e==ESP_OK; }
    if (e==ESP_OK) e=usb_host_transfer_alloc(4096,0,&rx);
    if (e==ESP_OK) e=usb_host_transfer_alloc(1600,0,&tx);
    if (e==ESP_OK && ep_status) e=usb_host_transfer_alloc(status_mps,0,&status);
    if (e==ESP_OK) e=usb_host_transfer_alloc(72,0,&control);
    if (e!=ESP_OK) { ncm_host_release(); return e; }
    rx->device_handle=tx->device_handle=control->device_handle=d;
    rx->bEndpointAddress=ep_in; rx->num_bytes=4096; rx->callback=rx_callback;
    tx->bEndpointAddress=ep_out; tx->callback=tx_callback;
    if (status) { status->device_handle=d; status->bEndpointAddress=ep_status; status->num_bytes=status_mps; status->callback=status_callback; }
    memset(control->data_buffer,0,72);
    auto *setup=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);
    setup->bmRequestType=0x80; setup->bRequest=6; setup->wValue=uint16_t(0x300)|mac_index; setup->wIndex=0x409; setup->wLength=64;
    control->num_bytes=72; control->bEndpointAddress=0; control->callback=control_callback;
    attached=true;
    control_stage=ControlStage::mac;
    e=usb_host_transfer_submit_control(c,control); control_busy=e==ESP_OK;
    control_deadline=esp_timer_get_time()+3000000;
    if (e!=ESP_OK) { ncm_host_disconnect(); ncm_host_release(); }
    return e;
}
void ncm_host_poll() {
    if (!attached || quiescing) return;
    if (control_busy && esp_timer_get_time()>control_deadline && !failed) { failed=true; ESP_LOGE(tag,"MAC descriptor timeout; unplug to cancel safely"); }
    if (control_done) {
        control_done=false;
        if (control_stage==ControlStage::alternate) {
            if (control->status==USB_TRANSFER_STATUS_COMPLETED) {
                ESP_LOGI(tag,"SET_INTERFACE accepted interface=%u alternate=%u",data_interface,data_alt);
                link_up(interface_mac);
            } else { failed=true; ESP_LOGE(tag,"SET_INTERFACE failed status=%d",control->status); }
        } else {
        const auto *p=control->data_buffer+8; int size=control->actual_num_bytes-8;
        uint8_t mac[6]={}; bool valid=control->status==USB_TRANSFER_STATUS_COMPLETED && size>=26 && p[1]==3 && p[0]==26;
        for (unsigned i=0;valid && i<12;++i) {
            uint8_t c=p[2+i*2],value;
            if (p[3+i*2]) { valid=false; break; }
            if (c>='0' && c<='9') value=c-'0'; else if (c>='A' && c<='F') value=c-'A'+10; else if (c>='a' && c<='f') value=c-'a'+10; else { valid=false; break; }
            mac[i/2]|=value<<(i%2?0:4);
        }
        uint8_t any=0; for (uint8_t b:mac) any|=b;
        if (valid && any && !(mac[0]&1)) {
            memcpy(interface_mac,mac,sizeof(interface_mac));
            // interface_claim() allocates host pipes only. Select the data
            // alternate on the device explicitly, as Android setInterface does.
            memset(control->data_buffer,0,72);
            auto *setup=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);
            setup->bmRequestType=0x01; setup->bRequest=0x0b; setup->wValue=data_alt; setup->wIndex=data_interface;
            control->num_bytes=8; control_stage=ControlStage::alternate;
            esp_err_t e=usb_host_transfer_submit_control(client,control);
            control_busy=e==ESP_OK; control_deadline=esp_timer_get_time()+3000000;
            if (e!=ESP_OK) { failed=true; ESP_LOGE(tag,"SET_INTERFACE submit: %s",esp_err_to_name(e)); }
        }
        else { failed=true; ESP_LOGE(tag,"Invalid NCM MAC descriptor"); }
        }
    }
    if (rx_done) {
        rx_done=false;
        if (rx->status==USB_TRANSFER_STATUS_COMPLETED && rx->actual_num_bytes>0) {
            ++rx_blocks;
            if (!stream->feed(rx->data_buffer,rx->actual_num_bytes,received,nullptr)) { failed=true; ESP_LOGE(tag,"Invalid/unsupported NTB16 stream"); }
        } else if (rx->status!=USB_TRANSFER_STATUS_COMPLETED) { failed=true; ESP_LOGW(tag,"RX status=%d",rx->status); }
    }
    if (tx_done) { tx_done=false; if (tx->status==USB_TRANSFER_STATUS_COMPLETED) ++tx_frames; else { failed=true; ESP_LOGW(tag,"TX status=%d",tx->status); } }
    if (status_done) {
        status_done=false;
        if (status->status==USB_TRANSFER_STATUS_COMPLETED && status->actual_num_bytes>=8)
            ESP_LOGI(tag,"NOTIFICATION type=%02x value=%u",status->data_buffer[1],ncm::le16(status->data_buffer+2));
    }
    const int64_t now=esp_timer_get_time();
    if (tx_busy && now-tx_submitted_at>TX_STALL_US && !failed) {
        failed=true;
        ESP_LOGW(tag,"TX stalled: transfer pending for %lld ms (tx_frames=%u tx_pending=%u)",
                 (long long)((now-tx_submitted_at)/1000),tx_frames,unsigned(uxQueueMessagesWaiting(output)));
    }
    if (rx_blocks!=rx_blocks_at_last) { rx_blocks_at_last=rx_blocks; rx_last_at=now; }
    else if (started && rx_blocks>=RX_STALL_ARM_BLOCKS && now-rx_last_at>RX_STALL_US && !failed) {
        failed=true;
        ESP_LOGW(tag,"RX stalled: nothing received for %lld ms (rx_usb=%u rx_busy=%d)",
                 (long long)((now-rx_last_at)/1000),rx_blocks,int(rx_busy));
    }
    if (failed) {
        /* Do not halt endpoints here: that may wait forever for a lost channel
         * interrupt. The probe cuts VBUS and waits for DEV_GONE before cleanup. */
        quiescing=true; fault=true; return;
    }
    if (!started) return;
    if (!rx_busy) { esp_err_t e=usb_host_transfer_submit(rx); rx_busy=e==ESP_OK; if (e!=ESP_OK) failed=true; }
    if (status && !status_busy) { esp_err_t e=usb_host_transfer_submit(status); status_busy=e==ESP_OK; }
    if (!tx_busy) {
        static Packet packet;
        if (xQueueReceive(output,&packet,0)==pdTRUE) {
            size_t size=ncm::build(tx->data_buffer,1600,packet.bytes,packet.size,sequence++,out_mps);
            tx->num_bytes=size;
            esp_err_t e=size?usb_host_transfer_submit(tx):ESP_ERR_INVALID_SIZE;
            tx_busy=e==ESP_OK; if (tx_busy) tx_submitted_at=esp_timer_get_time(); else failed=true;
        }
    }
    if (esp_timer_get_time()>report_deadline) {
        report_deadline=esp_timer_get_time()+10000000;
        char address[64]={}; bool ready=ncm_host_ipv6(address,sizeof(address));
        ESP_LOGI(tag,"STATUS IPv6=%s rx_usb=%u rx_frames=%u tx_frames=%u tx_pending=%u",ready?address:"tentative",rx_blocks,rx_frames,tx_frames,unsigned(uxQueueMessagesWaiting(output)));
    }
}
void ncm_host_disconnect() {
    if (!attached.exchange(false)) return;
    if (started) { esp_netif_action_disconnected(netif,nullptr,0,nullptr); esp_netif_action_stop(netif,nullptr,0,nullptr); }
    started=false;
    // Halt/flush only our data/status pipes. Endpoint zero may be shared with
    // configuration scans, so its outstanding callback must drain naturally.
    if(device) {
        const uint8_t endpoints[]={ep_in,ep_out,ep_status};
        for(uint8_t endpoint:endpoints) if(endpoint && usb_host_endpoint_halt(device,endpoint)==ESP_OK)
            usb_host_endpoint_flush(device,endpoint);
    }
    if(output) xQueueReset(output);
}
bool ncm_host_take_fault() { return fault.exchange(false); }
bool ncm_host_drained() { return !rx_busy && !tx_busy && !status_busy && !control_busy; }
void ncm_host_release() {
    if (!ncm_host_drained()) return;
    ncm_host_disconnect();
    usb_transfer_t **transfers[]={&rx,&tx,&status,&control};
    for (auto t:transfers) if (*t) { usb_host_transfer_free(*t); *t=nullptr; }
    if (device && data_claimed) usb_host_interface_release(client,device,data_interface);
    if (device && control_claimed) usb_host_interface_release(client,device,control_interface);
    delete stream; stream=nullptr;
    device=nullptr; control_claimed=data_claimed=false;
    rx_done=tx_done=status_done=control_done=false;
}
esp_netif_t *ncm_host_netif() { return netif; }
extern "C" esp_netif_t *carplay_netif_handle() { return netif; }
bool ncm_host_ipv6(char *out,size_t capacity) {
    if (!netif || !out || capacity<40 || !attached || !started) return false;
    esp_ip6_addr_t address;
    if (esp_netif_get_ip6_linklocal(netif,&address)!=ESP_OK) return false;
    return snprintf(out,capacity,IPV6STR,IPV62STR(address))>0;
}
