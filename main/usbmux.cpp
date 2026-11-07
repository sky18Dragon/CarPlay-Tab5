// SPDX-License-Identifier: GPL-3.0-or-later
// USBMUX v2 framing follows the local DiPlay Iap2UsbMuxHost/UsbMuxFrameBuffer.
#include "usbmux.h"
#include "lockdown_reply.h"
#include "mux_channels.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {
constexpr const char *tag="USBMUX";
constexpr size_t max_frame=65536, tx_capacity=4096;
usb_host_client_handle_t client;
usb_device_handle_t device;
usb_transfer_t *rx,*tx;
uint8_t ep_in,ep_out,interface_number;
bool rx_busy,tx_busy,connected,rx_done,tx_done,failed,version_ok,tcp_connected,optional_padding;
uint16_t mux_seq,mux_ack;
uint32_t tcp_seq,tcp_ack;
uint8_t *buffer;
size_t buffered;
struct Packet { size_t size; uint8_t bytes[tx_capacity]; } queue[8];
unsigned head,tail,count;
int64_t deadline;
lockdown::QueryTypeReply lockdown_reply;
bool query_complete;
uint16_t be16(const uint8_t *p) { return uint16_t(p[0])<<8|p[1]; }
uint32_t be32(const uint8_t *p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
void put16(uint8_t *p,uint16_t x) { p[0]=x>>8; p[1]=x; }
void put32(uint8_t *p,uint32_t x) { p[0]=x>>24; p[1]=x>>16; p[2]=x>>8; p[3]=x; }
bool enqueue(const uint8_t *p,size_t n) {
    if (!connected || failed || n>tx_capacity || count==8) { failed=true; return false; }
    memcpy(queue[tail].bytes,p,n); queue[tail].size=n; tail=(tail+1)%8; ++count; return true;
}
bool frame(uint32_t protocol,const uint8_t *payload,size_t n) {
    if (!connected || failed || n>tx_capacity-16 || count==8) { failed=true; return false; }
    uint8_t *packet=queue[tail].bytes;
    put32(packet,protocol); put32(packet+4,n+16); put32(packet+8,0xfeedface);
    put16(packet+12,mux_seq++); put16(packet+14,mux_ack);
    memcpy(packet+16,payload,n);
    queue[tail].size=n+16; tail=(tail+1)%8; ++count; return true;
}
bool tcp(uint8_t flags,const uint8_t *payload=nullptr,size_t n=0) {
    if (n>tx_capacity-36) return false;
    static uint8_t packet[tx_capacity-16]; // Only touched by the USB client task.
    memset(packet,0,20);
    put16(packet,1); put16(packet+2,62078); put32(packet+4,tcp_seq); put32(packet+8,tcp_ack);
    packet[12]=0x50; packet[13]=flags; put16(packet+14,512);
    if (n) memcpy(packet+20,payload,n);
    ESP_LOGI(tag,"TCP_TX flags=%02x seq=%lu ack=%lu bytes=%u",flags,
             (unsigned long)tcp_seq,(unsigned long)tcp_ack,unsigned(n));
    if (!frame(6,packet,n+20)) return false;
    tcp_seq+=n+((flags&2)?1:0)+((flags&1)?1:0);
    return true;
}
void query_type() {
    // Read-only Lockdown request. Never dumps phone identifiers or pairing material.
    constexpr char xml[]="<?xml version=\"1.0\" encoding=\"UTF-8\"?><!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"><plist version=\"1.0\"><dict><key>Label</key><string>CarPlay_Tab5</string><key>Request</key><string>QueryType</string></dict></plist>";
    uint8_t payload[sizeof(xml)+4]; put32(payload,sizeof(xml)-1); memcpy(payload+4,xml,sizeof(xml)-1);
    // iOS USBMUX carries a constrained TCP dialect. Match DiPlay's ACK-only
    // data packets; PSH is not used by its working Lockdown transport.
    if (!tcp(0x10,payload,sizeof(xml)+3)) {
        failed=true;
        ESP_LOGE(tag,"Could not queue Lockdown QueryType");
        return;
    }
    ESP_LOGI(tag,"Lockdown QueryType sent; pairing/iAP2 not started");
}
void received_frame(const uint8_t *p,size_t n) {
    uint32_t protocol=be32(p);
    if (!version_ok) {
        if (protocol!=0 || n!=20 || be32(p+8)!=2) { failed=true; ESP_LOGE(tag,"Unexpected version reply"); return; }
        version_ok=true; optional_padding=true;
        uint8_t setup=7; frame(2,&setup,1); tcp(2);
        ESP_LOGI(tag,"VERSION 2 accepted; connecting to Lockdown 62078");
        return;
    }
    mux_ack=be16(p+12);
    if (protocol!=6) return;
    if (n<36) { failed=true; return; }
    const auto *t=p+16; size_t th=(t[12]>>4)*4;
    if (th<20 || th>n-16) { failed=true; return; }
    uint8_t flags=t[13]; uint32_t seq=be32(t+4); size_t bytes=n-16-th;
    optional_padding=n==36 && flags==0x12 && be32(p+8)==0xfaceface;
    if (be16(t+2)!=1) { mux_channels_receive(t,n-16); return; }
    if (be16(t)!=62078) { ESP_LOGW(tag,"Ignored other TCP port"); return; }
    ESP_LOGI(tag,"TCP_RX flags=%02x seq=%lu ack=%lu bytes=%u mux_seq=%u",flags,
             (unsigned long)seq,(unsigned long)be32(t+8),unsigned(bytes),mux_ack);
    if (flags&4) {
        // Port 1 only carries the QueryType probe. Once it has answered, a reset
        // says nothing about the CarPlay streams, so it must not tear them down.
        if (query_complete) { tcp_connected=false; ESP_LOGW(tag,"PROBE_RESET ignored; CarPlay streams unaffected"); return; }
        failed=true; ESP_LOGE(tag,"TCP RESET"); return;
    }
    if (flags&2) {
        if (tcp_connected || !(flags&16) || be32(t+8)!=tcp_seq) { failed=true; return; }
        tcp_ack=seq+1; tcp_connected=true; tcp(16); query_type();
    } else if (bytes) {
        if (!tcp_connected || seq!=tcp_ack) { tcp(16); return; }
        tcp_ack+=bytes; tcp(16);
        if (query_complete) {
            failed=true; ESP_LOGE(tag,"Unexpected data after QueryType response"); return;
        }
        auto result=lockdown_reply.append(t+th,bytes);
        if (result==lockdown::QueryTypeReply::Result::invalid) {
            failed=true; ESP_LOGE(tag,"Invalid Lockdown response length"); return;
        }
        if (result==lockdown::QueryTypeReply::Result::complete) {
            if (!lockdown_reply.is_lockdown_service()) {
                failed=true; ESP_LOGE(tag,"QueryType response did not identify the Lockdown service"); return;
            }
            query_complete=true;
            // The probe has served its purpose. Left open, the phone resets it
            // about 60 s later, in the middle of the CarPlay session.
            if (!tcp(0x11)) return;
            ESP_LOGI(tag,"PROBE_CLOSED FIN sent on QueryType connection");
            mux_channels_ready();
            deadline=0;

            ESP_LOGI(tag,"LOCKDOWN_QUERY_TYPE_OK body=%u bytes; response reassembled and service type verified",
                     unsigned(lockdown_reply.body_size()));
        }
    }
    if (flags&1) {
        if (seq+bytes!=tcp_ack) { tcp(16); return; }
        ++tcp_ack; tcp(16);
        if (!query_complete) { failed=true; ESP_LOGE(tag,"Lockdown peer closed before complete response"); }
        else ESP_LOGI(tag,"Lockdown peer closed");
    }
}
void consume(const uint8_t *p,size_t n) {
    if (buffered+n>max_frame+4096) { failed=true; return; }
    memcpy(buffer+buffered,p,n); buffered+=n;
    while (buffered>=16 && !failed) {
        uint32_t len=be32(buffer+4);
        if (len<16 || len>max_frame) {
            if (optional_padding && be32(buffer)!=6) {
                if (buffered<40) return;
                uint32_t next=be32(buffer+8);
                size_t th=(buffer[32]>>4)*4;
                if (be32(buffer+4)==6 && next>=36 && next<=max_frame && be32(buffer+12)==0xfaceface && th>=20 && th<=next-16 && be16(buffer+20) && be16(buffer+22)) {
                    memmove(buffer,buffer+4,buffered-4); buffered-=4; optional_padding=false;
                    ESP_LOGI(tag,"Accepted bounded iOS reply padding (4 bytes)"); continue;
                }
            }
            failed=true; ESP_LOGE(tag,"Invalid frame length=%lu",(unsigned long)len); return;
        }
        if (buffered<len) return;
        optional_padding=false;
        received_frame(buffer,len);
        memmove(buffer,buffer+len,buffered-len); buffered-=len;
    }
}
void rx_callback(usb_transfer_t *) { rx_busy=false; rx_done=true; }
void tx_callback(usb_transfer_t *) { tx_busy=false; tx_done=true; }
}

esp_err_t usbmux_start(usb_host_client_handle_t c,usb_device_handle_t d) {
    if (connected || rx || tx) return ESP_ERR_INVALID_STATE;
    const usb_config_desc_t *config=nullptr;
    esp_err_t err=usb_host_get_active_config_descriptor(d,&config); if (err!=ESP_OK) return err;
    bool in_mux=false; ep_in=ep_out=0; unsigned in_count=0,out_count=0;
    auto *p=reinterpret_cast<const uint8_t *>(config);
    for (size_t pos=0;pos<config->wTotalLength;) {
        if (config->wTotalLength-pos<2 || p[pos]<2 || p[pos]>config->wTotalLength-pos) return ESP_ERR_INVALID_RESPONSE;
        const auto *it=p+pos;
        if (it[1]==4 && it[0]>=9) {
            in_mux=it[5]==0xff && it[6]==0xfe && it[7]==2 && it[3]==0;
            if (in_mux) interface_number=it[2];
        } else if (in_mux && it[1]==5 && it[0]>=7 && (it[3]&3)==2) {
            unsigned mps=it[4]|(unsigned(it[5])<<8);
            if (mps!=64 && mps!=512) return ESP_ERR_NOT_SUPPORTED;
            if (it[2]&0x80) { ep_in=it[2]; ++in_count; } else { ep_out=it[2]; ++out_count; }
        }
        pos+=it[0];
    }
    if (in_count!=1 || out_count!=1) return ESP_ERR_NOT_FOUND;
    err=usb_host_interface_claim(c,d,interface_number,0); if (err!=ESP_OK) return err;
    client=c; device=d;
    err=usb_host_transfer_alloc(4096,0,&rx);
    if (err==ESP_OK) err=usb_host_transfer_alloc(tx_capacity,0,&tx);
    buffer=static_cast<uint8_t *>(heap_caps_malloc(max_frame+4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (err!=ESP_OK || !buffer) { usbmux_release(); return ESP_ERR_NO_MEM; }
    mux_channels_transport(true);
    connected=true; failed=false; version_ok=tcp_connected=optional_padding=false;

    rx_busy=tx_busy=rx_done=tx_done=false; head=tail=count=0; buffered=0;
    mux_seq=mux_ack=0; tcp_seq=tcp_ack=0;
    lockdown_reply.reset(); query_complete=false;
    rx->device_handle=d; rx->bEndpointAddress=ep_in; rx->num_bytes=4096; rx->callback=rx_callback;
    tx->device_handle=d; tx->bEndpointAddress=ep_out; tx->callback=tx_callback;
    tx->flags=USB_TRANSFER_FLAG_ZERO_PACK;
    uint8_t version[20]={}; put32(version+4,20); put32(version+8,2); enqueue(version,20);
    deadline=esp_timer_get_time()+15000000;
    ESP_LOGI(tag,"Claimed interface=%u IN=%02x OUT=%02x; starting version handshake",interface_number,ep_in,ep_out);
    return ESP_OK;
}
void usbmux_poll() {
    if (!connected) return;
    if (rx_done) {
        rx_done=false;
        if (rx->status==USB_TRANSFER_STATUS_COMPLETED) consume(rx->data_buffer,rx->actual_num_bytes);
        else { failed=true; ESP_LOGE(tag,"RX status=%d",int(rx->status)); }
    }
    if (tx_done) {
        tx_done=false;
        if (tx->status!=USB_TRANSFER_STATUS_COMPLETED || tx->actual_num_bytes!=tx->num_bytes) { failed=true; ESP_LOGE(tag,"TX incomplete"); }
    }
    if (deadline && esp_timer_get_time()>deadline) { deadline=0; failed=true; ESP_LOGE(tag,"Handshake/Lockdown timeout"); }
    if (failed) { deadline=0; mux_channels_transport(false); return; }

    if (count<4) mux_channels_poll();
    if (!rx_busy) { esp_err_t e=usb_host_transfer_submit(rx); if (e==ESP_OK) rx_busy=true; else failed=true; }
    if (!tx_busy && count) {
        tx->num_bytes=queue[head].size; memcpy(tx->data_buffer,queue[head].bytes,tx->num_bytes);
        head=(head+1)%8; --count;
        esp_err_t e=usb_host_transfer_submit(tx); if (e==ESP_OK) tx_busy=true; else failed=true;
    }
}
void usbmux_disconnect() {
    bool was_connected=connected;
    mux_channels_transport(false); connected=false;
    if(was_connected && device) {
        const uint8_t endpoints[]={ep_in,ep_out};
        for(uint8_t endpoint:endpoints) if(endpoint && usb_host_endpoint_halt(device,endpoint)==ESP_OK)
            usb_host_endpoint_flush(device,endpoint);
    }
}
bool usbmux_drained() { return !rx_busy && !tx_busy; }
void usbmux_release() {
    if (!usbmux_drained()) return;
    if (rx) usb_host_transfer_free(rx);
    if (tx) usb_host_transfer_free(tx);
    rx=tx=nullptr;
    free(buffer); buffer=nullptr;
    if (device) usb_host_interface_release(client,device,interface_number);
    device=nullptr; connected=false;
}

const char *usbmux_status(void) { return !connected ? "IDLE" : failed ? "FAILED" : query_complete ? "QUERY_TYPE_OK" : version_ok ? "LOCKDOWN_PENDING" : "VERSION_PENDING"; }

bool usbmux_send_tcp_packet(uint16_t local,uint16_t remote,uint32_t seq,uint32_t ack,
                           uint8_t flags,const uint8_t *data,size_t size) {
    if (size>tx_capacity-36 || !connected || failed) return false;
    static uint8_t packet[tx_capacity-16];
    memset(packet,0,20);
    put16(packet,local); put16(packet+2,remote); put32(packet+4,seq); put32(packet+8,ack);
    packet[12]=0x50; packet[13]=flags; put16(packet+14,512);
    if (size) memcpy(packet+20,data,size);
    return frame(6,packet,size+20);
}

uint32_t usbmux_epoch(void) { return mux_transport_generation(); }
bool usbmux_worker_drained(void) { return mux_worker_drained(); }
