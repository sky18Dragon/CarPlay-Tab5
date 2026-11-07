// SPDX-License-Identifier: GPL-3.0-or-later
#include "mux_channels.h"
#include "lockdown_client.h"
#include "app_session.h"
#include <atomic>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

namespace {
constexpr char tag[]="MUX_STREAM";
constexpr unsigned slots=3, receive_capacity=32768;
struct Channel {
    std::atomic<uint32_t> token{0};
    std::atomic<bool> established{false}, closed{true};
    uint16_t local=0, remote=0;
    uint32_t seq=0, ack=0;
    StreamBufferHandle_t input=nullptr;
    StaticStreamBuffer_t control;
};
Channel channels[slots];
enum class Op : uint8_t { open, write, close };
struct Command { uint32_t token; Op op; uint16_t port, size; uint8_t data[1024]; };
QueueHandle_t commands;
std::atomic<bool> attached{false}, worker_running{false};
std::atomic<uint32_t> generation{0};
bool ready;
uint16_t next_port=2;

uint16_t be16(const uint8_t *p) { return uint16_t(p[0])<<8|p[1]; }
uint32_t be32(const uint8_t *p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
Channel *find(uint32_t token) {
    for (auto &c:channels) if (token && c.token.load()==token) return &c;
    return nullptr;
}
bool send(Channel &c,uint8_t flags,const uint8_t *p=nullptr,size_t size=0) {
    if (!usbmux_send_tcp_packet(c.local,c.remote,c.seq,c.ack,flags,p,size)) { c.closed=true; return false; }
    c.seq+=size+((flags&2)?1:0)+((flags&1)?1:0);
    return true;
}
bool init() {
    if (!commands) commands=xQueueCreate(8,sizeof(Command));
    if (!commands) return false;
    for (auto &c:channels) if (!c.input) {
        auto *data=static_cast<uint8_t *>(heap_caps_malloc(receive_capacity+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if (!data) return false;
        c.input=xStreamBufferCreateStatic(receive_capacity+1,1,data,&c.control);
        if (!c.input) { free(data); return false; }
    }
    return true;
}
void worker(void *) {
    uint32_t epoch=generation.load();
    tab5_session_post(epoch,CP_MUX_OK);
    lockdown_client_run(epoch);
    tab5_session_post(epoch,CP_IO_ERROR);
    worker_running=false;
    vTaskDelete(nullptr);
}
}

void mux_channels_transport(bool value) {
    if (attached.exchange(value)==value && !value) return;
    ready=false;
    ++generation;
    for (auto &c:channels) c.closed=true;
}
void mux_channels_ready() { ready=true; }
uint32_t mux_transport_generation() { return generation.load(); }
bool mux_transport_alive(uint32_t expected) { return attached.load() && generation.load()==expected; }

void mux_channels_poll() {
    if (!attached) return;
    static bool launched=false;
    static uint32_t launched_generation=0;
    if (launched_generation!=generation.load()) { launched_generation=generation.load(); launched=false; }
    if (ready && !launched && !worker_running) {
        if (!init()) { ESP_LOGE(tag,"Could not allocate stream queues"); launched=true; return; }
        xQueueReset(commands);
        for (auto &c:channels) { c.token=0; c.established=false; c.closed=true; xStreamBufferReset(c.input); }
        worker_running=true;
        if (xTaskCreatePinnedToCore(worker,"lockdown",16384,nullptr,4,nullptr,0)!=pdPASS) {
            worker_running=false; ESP_LOGE(tag,"Could not start worker");
        }
        launched=true;
    }
    if (!commands) return;
    static Command cmd;
    if (xQueueReceive(commands,&cmd,0)!=pdTRUE) return;
    Channel *c=find(cmd.token);
    if (!c || uint16_t(cmd.token>>16)!=uint16_t(generation.load())) return;
    if (cmd.op==Op::open) {
        c->local=uint16_t(cmd.token); c->remote=cmd.port; c->seq=c->ack=0;
        c->closed=false; c->established=false;
        send(*c,2);
        ESP_LOGI(tag,"Connecting local=%u remote=%u",c->local,c->remote);
    } else if (cmd.op==Op::write) {
        if (!c->closed && c->established) send(*c,0x10,cmd.data,cmd.size);
    } else {
        if (c->established && !c->closed) send(*c,0x11);
        c->closed=true;
    }
}

void mux_channels_receive(const uint8_t *p,size_t size) {
    if (size<20) return;
    size_t header=(p[12]>>4)*4;
    if (header<20 || header>size) return;
    for (auto &c:channels) {
        if (!c.token || c.local!=be16(p+2) || c.remote!=be16(p)) continue;
        uint8_t flags=p[13]; uint32_t seq=be32(p+4); size_t n=size-header;
        if (flags&4) {
            if (!c.closed) ESP_LOGE(tag,"Peer reset active connection local=%u",c.local);
            c.closed=true; return;
        }
        if ((flags&0x12)==0x12) {
            if (c.closed || c.established || n || be32(p+8)!=c.seq) return;
            c.ack=seq+1;
            if (send(c,0x10)) c.established=true;
            return;
        }
        if (c.closed || !c.established) return;
        if (n) {
            if (seq!=c.ack) { send(c,0x10); return; }
            if (xStreamBufferSpacesAvailable(c.input)<n || xStreamBufferSend(c.input,p+header,n,0)!=n) {
                c.closed=true; ESP_LOGE(tag,"Receive buffer overflow local=%u",c.local); return;
            }
            c.ack+=n; send(c,0x10);
        }
        if (flags&1) {
            if (seq+n!=c.ack) { send(c,0x10); return; }
            ++c.ack; send(c,0x10); c.closed=true;
        }
        return;
    }
}

uint32_t mux_stream_open(uint16_t port,uint32_t epoch) {
    if (!commands || !port || !mux_transport_alive(epoch) || next_port==0) return 0;
    Channel *channel=nullptr;
    for (auto &c:channels) if (!c.token.load()) { channel=&c; break; }
    if (!channel) return 0;
    xStreamBufferReset(channel->input);
    channel->established=false; channel->closed=false;
    uint32_t token=(uint32_t(uint16_t(epoch))<<16)|next_port++;
    channel->token=token;
    Command cmd={}; cmd.op=Op::open; cmd.token=token; cmd.port=port;
    if (xQueueSend(commands,&cmd,pdMS_TO_TICKS(500))!=pdTRUE) { channel->token=0; return 0; }
    int64_t end=esp_timer_get_time()+5000000;
    while (mux_transport_alive(epoch) && !channel->closed && !channel->established && esp_timer_get_time()<end) vTaskDelay(pdMS_TO_TICKS(10));
    if (!channel->established || channel->closed || !mux_transport_alive(epoch)) { mux_stream_close(token); return 0; }
    return token;
}

int mux_stream_read(uint32_t token,uint8_t *out,size_t size,unsigned timeout_ms) {
    Channel *c=find(token);
    if (!c || !out || !size) return -1;
    int64_t end=esp_timer_get_time()+int64_t(timeout_ms)*1000;
    do {
        if (!attached || uint16_t(token>>16)!=uint16_t(generation.load()) || c->token!=token) return -1;
        size_t n=xStreamBufferReceive(c->input,out,size,pdMS_TO_TICKS(20));
        if (n) return int(n);
        if (c->closed) return -1;
    } while (esp_timer_get_time()<end);
    return 0;
}

bool mux_stream_write(uint32_t token,const uint8_t *data,size_t size) {
    Channel *c=find(token);
    if (!c || (!data && size)) return false;
    Command cmd={}; cmd.op=Op::write; cmd.token=token;
    for (size_t pos=0;pos<size;) {
        if (!attached || c->closed || c->token!=token || uint16_t(token>>16)!=uint16_t(generation.load())) return false;
        cmd.size=size-pos>sizeof(cmd.data)?sizeof(cmd.data):size-pos;
        memcpy(cmd.data,data+pos,cmd.size);
        if (xQueueSend(commands,&cmd,pdMS_TO_TICKS(1000))!=pdTRUE) return false;
        pos+=cmd.size;
    }
    return true;
}

void mux_stream_close(uint32_t token) {
    Channel *c=find(token); if (!c) return;
    if (attached && uint16_t(token>>16)==uint16_t(generation.load()) && !c->closed) {
        Command cmd={}; cmd.op=Op::close; cmd.token=token;
        if (xQueueSend(commands,&cmd,pdMS_TO_TICKS(500))==pdTRUE) {
            int64_t end=esp_timer_get_time()+1000000;
            while (!c->closed && esp_timer_get_time()<end && attached) vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    c->token=0;
}

bool mux_worker_drained() { return !worker_running.load(); }
