/* SPDX-License-Identifier: GPL-3.0-or-later; RTP framing and formats follow DiPlay AudioStream.kt. */
#include "airplay_audio.h"
#include "airplay_audio_sink.h"
#include "airplay_pair_verify.h"
#include "net_port.h"
#include "mbedtls/chachapoly.h"
#include "mbedtls/platform_util.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define AUDIO_SLOTS 4
typedef struct {
    int data,control;
    unsigned type,rate,channels;
    uint64_t id,format,sink_generation;
    char usage[32];
    uint8_t key[32];
    uint16_t data_port,control_port,last_seq;
    bool have_seq;
    uint32_t packets,bytes,bad,lost,reordered,crc;
    int peak;
} audio_slot_t;
static audio_slot_t slots[AUDIO_SLOTS];
static airplay_audio_sink sink;
static bool sink_installed;
bool airplay_audio_install_sink(const airplay_audio_sink *p) {
    if(sink_installed || !p || !p->open || !p->write || !p->close) return false;
    sink=*p; sink_installed=true; return true;
}
static SemaphoreHandle_t lock;
static uint8_t *packet;

static bool number(const airplay_bplist_doc_t *doc,airplay_bplist_object_t stream,const char *key,uint64_t *out) {
    airplay_bplist_object_t value;
    return airplay_bplist_dict_get(doc,stream,key,&value)==AIRPLAY_BPLIST_OK &&
        airplay_bplist_get_uint(doc,value,out)==AIRPLAY_BPLIST_OK;
}
static int udp(uint16_t *port) {
    int fd=socket(AF_INET6,SOCK_DGRAM,IPPROTO_UDP);
    if (fd<0) return -1;
    struct sockaddr_in6 addr;
    if (!net_port_local_address(&addr,0) || bind(fd,(struct sockaddr *)&addr,sizeof(addr))) { close(fd); return -1; }
    socklen_t len=sizeof(addr);
    if (getsockname(fd,(struct sockaddr *)&addr,&len)) { close(fd); return -1; }
    fcntl(fd,F_SETFL,O_NONBLOCK);
    *port=ntohs(addr.sin6_port); return fd;
}
static void clear(audio_slot_t *s) {
    if (s->type) {
        close(s->data); close(s->control);
        if(s->sink_generation) sink.close(s->sink_generation);
        ESP_LOGI("AUDIO","stop type=%u usage=%s packets=%lu pcm_bytes=%lu bad=%lu",s->type,s->usage,
            (unsigned long)s->packets,(unsigned long)s->bytes,(unsigned long)s->bad);
    }
    mbedtls_platform_zeroize(s,sizeof(*s));
}
static void consume(audio_slot_t *s,int n) {
    if (n<36 || (packet[0]>>6)!=2 || (packet[0]&0x3f)) { ++s->bad; return; }
    size_t size=(size_t)n-36;
    if (!size || size%(s->channels*2)) { ++s->bad; return; }
    uint8_t nonce[12]={0}; memcpy(nonce+4,packet+n-8,8);
    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    int result=mbedtls_chachapoly_setkey(&ctx,s->key);
    if (!result) result=mbedtls_chachapoly_auth_decrypt(&ctx,size,nonce,packet+4,8,
        packet+n-24,packet+12,packet+12);
    mbedtls_chachapoly_free(&ctx);
    if (result) { ++s->bad; return; }
    uint16_t seq=(uint16_t)((packet[2]<<8)|packet[3]);
    if (s->have_seq) {
        int16_t delta=(int16_t)(seq-s->last_seq);
        if (delta<=0) { ++s->reordered; }
        if (delta>1) s->lost+=(unsigned)delta-1;
    }
    if(!s->have_seq || (uint16_t)(seq-s->last_seq)<0x8000) s->last_seq=seq;
    s->have_seq=true;
    uint32_t timestamp=((uint32_t)packet[4]<<24)|((uint32_t)packet[5]<<16)|((uint32_t)packet[6]<<8)|packet[7];
    if(!sink.write(s->sink_generation,timestamp,packet+12,size)) { ++s->bad; return; }
    ++s->packets; s->bytes+=size;
    s->crc=esp_rom_crc32_le(s->crc,packet+12,size);
    // DiPlay byteSwapS16 converts this network-order LPCM to native little endian.
    for (size_t i=12;i<12+size;i+=2) {
        int value=(int16_t)((packet[i]<<8)|packet[i+1]);
        if (value<0) value=-value;
        if (value>s->peak) s->peak=value;
    }
    if (s->packets==1) ESP_LOGI("AUDIO","PCM_FIRST type=%u usage=%s rate=%u channels=%u bytes=%u sink=pcm_playback",
        s->type,s->usage,s->rate,s->channels,(unsigned)size);
}
static void task(void *arg) {
    (void)arg;
    TickType_t last=0;
    while (true) {
        xSemaphoreTake(lock,portMAX_DELAY);
        for (unsigned i=0;i<AUDIO_SLOTS;++i) {
            audio_slot_t *s=&slots[i];
            if (!s->type) continue;
            // Bound per-stream work so music cannot starve guidance or control.
            for (unsigned burst=0;burst<8;++burst) {
                int n=recv(s->data,packet,4096,MSG_DONTWAIT);
                if (n<=0) break;
                consume(s,n);
            }
            for (unsigned burst=0;burst<4;++burst) if (recv(s->control,packet,4096,MSG_DONTWAIT)<=0) break;
            if (xTaskGetTickCount()-last>=pdMS_TO_TICKS(3000)) {
                ESP_LOGI("AUDIO","PCM type=%u usage=%s packets=%lu bytes=%lu auth_or_format_errors=%lu lost=%lu reordered=%lu peak=%d crc=%08lx sink=pcm_playback",
                    s->type,s->usage,(unsigned long)s->packets,(unsigned long)s->bytes,(unsigned long)s->bad,
                    (unsigned long)s->lost,(unsigned long)s->reordered,s->peak,(unsigned long)s->crc);
            }
        }
        if (xTaskGetTickCount()-last>=pdMS_TO_TICKS(3000)) last=xTaskGetTickCount();
        xSemaphoreGive(lock);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
bool airplay_audio_start(const airplay_rtsp_pairing_t *pairing,
    const airplay_bplist_doc_t *doc,airplay_bplist_object_t stream,airplay_stream_ports_t *ports) {
    uint64_t type,id,format;
    if (!number(doc,stream,"type",&type) || type!=100 ||
        !number(doc,stream,"streamConnectionID",&id) || !number(doc,stream,"audioFormat",&format)) return false;
    const unsigned rates[]={8000,16000,24000,32000,44100,48000};
    const uint64_t mono[]={0x4,0x10,0x40,0x100,0x400,0x4000};
    unsigned rate=0,channels=0;
    for (unsigned i=0;i<6;++i) {
        if (format==mono[i]) { rate=rates[i]; channels=1; }
        if (format==mono[i]*2) { rate=rates[i]; channels=2; }
    }
    if (!sink_installed || (rate!=44100 && rate!=48000)) return false; // Only formats advertised in /info; never interpret compressed bytes as PCM.
    char usage[32]="default";
    airplay_bplist_object_t value; size_t length;
    if (airplay_bplist_dict_get(doc,stream,"audioType",&value)==AIRPLAY_BPLIST_OK &&
        airplay_bplist_get_string(doc,value,usage,sizeof(usage),&length)!=AIRPLAY_BPLIST_OK) return false;
    if (!lock) {
        lock=xSemaphoreCreateMutex();
        packet=heap_caps_malloc(4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (!lock || !packet) {
            if (lock) vSemaphoreDelete(lock);
            heap_caps_free(packet); packet=NULL; lock=NULL; return false;
        }
        if (xTaskCreate(task,"audio_pcm",4096,NULL,4,NULL)!=pdPASS) {
            heap_caps_free(packet); packet=NULL; vSemaphoreDelete(lock); lock=NULL; return false;
        }
    }
    xSemaphoreTake(lock,portMAX_DELAY);
    audio_slot_t *s=NULL;
    for (unsigned i=0;i<AUDIO_SLOTS;++i) if (slots[i].type && slots[i].id==id &&
        (slots[i].type!=type || slots[i].format!=format || strcmp(slots[i].usage,usage))) goto failed;
    for (unsigned i=0;i<AUDIO_SLOTS;++i) if (slots[i].type==type && !strcmp(slots[i].usage,usage)) { s=&slots[i]; break; }
    if (s && s->id==id && s->format==format) goto ready;
    if (!s) for (unsigned i=0;i<AUDIO_SLOTS;++i) if (!slots[i].type) { s=&slots[i]; break; }
    if (!s) goto failed;
    clear(s);
    if (airplay_rtsp_pairing_derive_screen_output_key(pairing,id,s->key)!=AIRPLAY_PAIR_VERIFY_OK) goto failed;
    s->data=udp(&s->data_port);
    s->control=udp(&s->control_port);
    if (s->data<0 || s->control<0) {
        if (s->data>=0) close(s->data);
        if (s->control>=0) close(s->control);
        mbedtls_platform_zeroize(s,sizeof(*s)); goto failed;
    }
    s->type=type; s->id=id; s->format=format; s->rate=rate; s->channels=channels;
    memcpy(s->usage,usage,sizeof(usage));
    s->sink_generation=sink.open(rate,channels);
    if(!s->sink_generation) { clear(s); goto failed; }
    ESP_LOGI("AUDIO","ready type=%u usage=%s rate=%u channels=%u data=%u control=%u sink=pcm_playback",
        s->type,s->usage,rate,channels,s->data_port,s->control_port);
ready:
    *ports=(airplay_stream_ports_t){.type=type,.connection_id=id,.data_port=s->data_port,.control_port=s->control_port};
    xSemaphoreGive(lock); return true;
failed:
    xSemaphoreGive(lock); return false;
}
void airplay_audio_stop(unsigned type) {
    if (!lock) return;
    xSemaphoreTake(lock,portMAX_DELAY);
    for (unsigned i=0;i<AUDIO_SLOTS;++i) if (!type || slots[i].type==type) clear(&slots[i]);
    xSemaphoreGive(lock);
}
