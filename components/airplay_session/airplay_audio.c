/* SPDX-License-Identifier: GPL-3.0-or-later; RTP framing and formats follow DiPlay AudioStream.kt,
 * MicrophonePacketizer.kt and MicrophoneStart.kt. */
#include "airplay_audio.h"
#include "airplay_audio_sink.h"
#include "airplay_pair_verify.h"
#include "net_port.h"
#include "mbedtls/chachapoly.h"
#include "mbedtls/platform_util.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define AUDIO_SLOTS 4
#define RTP_HEADER_BYTES 12
#define SEAL_OVERHEAD (RTP_HEADER_BYTES + 16 + 8)
/* A 1500-byte link MTU less the IPv6 and UDP headers. */
#define MAX_UDP_PAYLOAD (1500 - 40 - 8)
/* Start the uplink this long after SETUP when the phone plays nothing (Siri, voice notes). */
#define SILENT_START_US 500000
typedef struct {
    bool wanted, running;
    struct sockaddr_in6 address;
    uint8_t key[32];
    unsigned frames;
    uint16_t sequence;
    uint32_t timestamp;
    uint64_t nonce;
    int64_t start_at, next_at;
    uint32_t sent, failed, starved;
    bool live;      /* true: real microphone; false: sealed silence */
    int peak;
} uplink_t;
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
    uplink_t mic;
} audio_slot_t;
static audio_slot_t slots[AUDIO_SLOTS];
static airplay_audio_sink sink;
static bool sink_installed;
bool airplay_audio_install_sink(const airplay_audio_sink *p) {
    if(sink_installed || !p || !p->open || !p->write || !p->close) return false;
    sink=*p; sink_installed=true; return true;
}
static SemaphoreHandle_t lock;
static uint8_t *packet, *uplink_packet;
static int16_t *mic_samples;

static bool number(const airplay_bplist_doc_t *doc,airplay_bplist_object_t stream,const char *key,uint64_t *out) {
    airplay_bplist_object_t value;
    return airplay_bplist_dict_get(doc,stream,key,&value)==AIRPLAY_BPLIST_OK &&
        airplay_bplist_get_uint(doc,value,out)==AIRPLAY_BPLIST_OK;
}
/* -1 when absent; the phone sends `input` as a boolean or an integer. */
static int flag(const airplay_bplist_doc_t *doc,airplay_bplist_object_t stream,const char *key) {
    airplay_bplist_object_t value; bool b; uint64_t n;
    if (airplay_bplist_dict_get(doc,stream,key,&value)!=AIRPLAY_BPLIST_OK) return -1;
    if (airplay_bplist_get_bool(doc,value,&b)==AIRPLAY_BPLIST_OK) return b;
    if (airplay_bplist_get_uint(doc,value,&n)==AIRPLAY_BPLIST_OK) return n!=0;
    return -1;
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
        if(s->mic.live && sink.mic_stop) sink.mic_stop();
        ESP_LOGI("AUDIO","stop type=%u usage=%s packets=%lu pcm_bytes=%lu bad=%lu uplink=%s sent=%lu failed=%lu starved=%lu",
            s->type,s->usage,(unsigned long)s->packets,(unsigned long)s->bytes,(unsigned long)s->bad,
            s->mic.live?"mic":(s->mic.wanted?"silence":"off"),
            (unsigned long)s->mic.sent,(unsigned long)s->mic.failed,(unsigned long)s->mic.starved);
    }
    mbedtls_platform_zeroize(s,sizeof(*s));
}
/* DiPlay's iPhone rule: calls and Siri always record; app audio only when the phone asks for input. */
static bool wants_microphone(const char *usage,int input,uint64_t phone_port) {
    if (!strcmp(usage,"telephony") || !strcmp(usage,"speechRecognition")) return true;
    if (!strcmp(usage,"default") || !strcmp(usage,"compatibility"))
        return input>=0 ? input==1 : (phone_port>0 && phone_port<65536);
    return false;
}
static unsigned uplink_frames(unsigned rate,unsigned channels,uint64_t frames_per_packet) {
    const unsigned max_frames=(MAX_UDP_PAYLOAD-SEAL_OVERHEAD)/(channels*2);
    if (frames_per_packet>0 && frames_per_packet<=max_frames) return (unsigned)frames_per_packet;
    static const unsigned millis[]={20,10,5};
    for (unsigned i=0;i<3;++i) if (rate*millis[i]/1000<=max_frames) return rate*millis[i]/1000;
    return max_frames;
}
/* Seals one uplink packet. samples: m->frames mono samples, or NULL for silence
 * (used only when the board has no microphone). Wire PCM is big-endian. */
static void send_uplink(audio_slot_t *s,const int16_t *samples) {
    uplink_t *m=&s->mic;
    const size_t body=(size_t)m->frames*s->channels*2;
    uint8_t *p=uplink_packet;
    memset(p,0,RTP_HEADER_BYTES+body);
    if (samples) {
        uint8_t *q=p+RTP_HEADER_BYTES;
        for (unsigned i=0;i<m->frames;++i) {
            const int v=samples[i];
            if ((v<0?-v:v)>m->peak) m->peak=v<0?-v:v;
            for (unsigned c=0;c<s->channels;++c) { *q++=(uint8_t)((uint16_t)v>>8); *q++=(uint8_t)v; }
        }
    }
    p[0]=0x80; p[1]=(uint8_t)(s->type&0x7f);
    p[2]=(uint8_t)(m->sequence>>8); p[3]=(uint8_t)m->sequence;
    p[4]=(uint8_t)(m->timestamp>>24); p[5]=(uint8_t)(m->timestamp>>16);
    p[6]=(uint8_t)(m->timestamp>>8); p[7]=(uint8_t)m->timestamp;
    uint8_t nonce[12]={0};
    for (unsigned i=0;i<8;++i) nonce[4+i]=(uint8_t)(m->nonce>>(8*i));
    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    int result=mbedtls_chachapoly_setkey(&ctx,m->key);
    if (!result) result=mbedtls_chachapoly_encrypt_and_tag(&ctx,body,nonce,p+4,8,
        p+RTP_HEADER_BYTES,p+RTP_HEADER_BYTES,p+RTP_HEADER_BYTES+body);
    mbedtls_chachapoly_free(&ctx);
    memcpy(p+RTP_HEADER_BYTES+body+16,nonce+4,8);
    const size_t length=RTP_HEADER_BYTES+body+24;
    if (!result && sendto(s->data,p,length,0,(struct sockaddr *)&m->address,sizeof(m->address))==(ssize_t)length)
        ++m->sent;
    else ++m->failed;
    m->sequence++; m->timestamp+=m->frames; m->nonce++;
}
static void start_uplink(audio_slot_t *s,const char *reason) {
    uplink_t *m=&s->mic;
    if (!m->wanted || m->running) return;
    m->running=true; m->next_at=esp_timer_get_time();
    m->live=sink.mic_start && sink.mic_read && mic_samples && sink.mic_start();
    if (m->live) ESP_LOGI("AUDIO","uplink=mic start type=%u usage=%s reason=%s rate=%u channels=%u frames=%u",
        s->type,s->usage,reason,s->rate,s->channels,m->frames);
    else ESP_LOGW("AUDIO","uplink=silence start type=%u usage=%s reason=%s rate=%u channels=%u frames=%u (no microphone)",
        s->type,s->usage,reason,s->rate,s->channels,m->frames);
}
static void service_uplink(audio_slot_t *s,int64_t now) {
    uplink_t *m=&s->mic;
    if (!m->wanted) return;
    if (!m->running) {
        if (m->start_at && now>=m->start_at) start_uplink(s,"no_downlink_500ms");
        if (!m->running) return;
    }
    if (m->live) {
        /* Paced by the microphone clock: send whatever full packets are captured. */
        unsigned burst=0;
        for (;burst<4 && sink.mic_read(s->rate,mic_samples,m->frames)==m->frames;++burst) send_uplink(s,mic_samples);
        if (!burst && now-m->next_at>200000) { ++m->starved; m->next_at=now; }
        if (burst) m->next_at=now;
        return;
    }
    const int64_t period=(int64_t)m->frames*1000000/s->rate;
    if (now-m->next_at>8*period) m->next_at=now-period; /* after a long stall, do not burst */
    for (unsigned burst=0;burst<4 && now>=m->next_at;++burst) { send_uplink(s,NULL); m->next_at+=period; }
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
    if(s->sink_generation && !sink.write(s->sink_generation,timestamp,packet+12,size)) { ++s->bad; return; }
    ++s->packets; s->bytes+=size;
    s->crc=esp_rom_crc32_le(s->crc,packet+12,size);
    // DiPlay byteSwapS16 converts this network-order LPCM to native little endian.
    for (size_t i=12;i<12+size;i+=2) {
        int value=(int16_t)((packet[i]<<8)|packet[i+1]);
        if (value<0) value=-value;
        if (value>s->peak) s->peak=value;
    }
    if (s->packets==1) {
        ESP_LOGI("AUDIO","PCM_FIRST type=%u usage=%s rate=%u channels=%u bytes=%u sink=%s",
            s->type,s->usage,s->rate,s->channels,(unsigned)size,s->sink_generation?"pcm_playback":"none");
        start_uplink(s,"first_downlink");
    }
}
static void task(void *arg) {
    (void)arg;
    TickType_t last=0;
    while (true) {
        xSemaphoreTake(lock,portMAX_DELAY);
        const int64_t now=esp_timer_get_time();
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
            service_uplink(s,now);
            if (xTaskGetTickCount()-last>=pdMS_TO_TICKS(3000)) {
                ESP_LOGI("AUDIO","PCM type=%u usage=%s packets=%lu bytes=%lu auth_or_format_errors=%lu lost=%lu reordered=%lu peak=%d crc=%08lx sink=%s uplink=%s sent=%lu failed=%lu starved=%lu mic_peak=%d",
                    s->type,s->usage,(unsigned long)s->packets,(unsigned long)s->bytes,(unsigned long)s->bad,
                    (unsigned long)s->lost,(unsigned long)s->reordered,s->peak,(unsigned long)s->crc,
                    s->sink_generation?"pcm_playback":"none",
                    s->mic.running?(s->mic.live?"mic":"silence"):(s->mic.wanted?"pending":"off"),
                    (unsigned long)s->mic.sent,(unsigned long)s->mic.failed,(unsigned long)s->mic.starved,s->mic.peak);
                s->mic.peak=0;
            }
        }
        if (xTaskGetTickCount()-last>=pdMS_TO_TICKS(3000)) last=xTaskGetTickCount();
        xSemaphoreGive(lock);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
bool airplay_audio_start(const airplay_rtsp_pairing_t *pairing,
    const airplay_bplist_doc_t *doc,airplay_bplist_object_t stream,
    const struct sockaddr_in6 *peer,airplay_stream_ports_t *ports) {
    uint64_t type,id,format,phone_port=0,frames_per_packet=0;
    char keys[200]="";
    (void)airplay_bplist_dict_keys(doc,stream,keys,sizeof(keys));
    if (!number(doc,stream,"type",&type) || (type!=100 && type!=101) ||
        !number(doc,stream,"streamConnectionID",&id) || !number(doc,stream,"audioFormat",&format)) {
        ESP_LOGW("AUDIO","setup rejected: missing fields keys=[%s]",keys);
        return false;
    }
    (void)number(doc,stream,"dataPort",&phone_port);
    (void)number(doc,stream,"framesPerPacket",&frames_per_packet);
    const int input=flag(doc,stream,"input");
    const unsigned rates[]={8000,16000,24000,32000,44100,48000};
    const uint64_t mono[]={0x4,0x10,0x40,0x100,0x400,0x4000};
    unsigned rate=0,channels=0;
    for (unsigned i=0;i<6;++i) {
        if (format==mono[i]) { rate=rates[i]; channels=1; }
        if (format==mono[i]*2) { rate=rates[i]; channels=2; }
    }
    char usage[32]="default";
    airplay_bplist_object_t value; size_t length;
    if (airplay_bplist_dict_get(doc,stream,"audioType",&value)==AIRPLAY_BPLIST_OK &&
        airplay_bplist_get_string(doc,value,usage,sizeof(usage),&length)!=AIRPLAY_BPLIST_OK) return false;
    const bool microphone=type==100 && peer && wants_microphone(usage,input,phone_port);
    ESP_LOGI("AUDIO","setup type=%u usage=%s format=0x%llx rate=%u channels=%u input=%d phone_port=%u frames_per_packet=%u microphone=%s keys=[%s]",
        (unsigned)type,usage,(unsigned long long)format,rate,channels,input,(unsigned)phone_port,
        (unsigned)frames_per_packet,microphone?"on":"off",keys);
    if (!rate) return false; // Only PCM is advertised in /info; never interpret compressed bytes as PCM.
    if (microphone && (phone_port==0 || phone_port>65535)) {
        ESP_LOGW("AUDIO","setup rejected: microphone wanted but no phone port");
        return false;
    }
    if (!lock) {
        lock=xSemaphoreCreateMutex();
        packet=heap_caps_malloc(4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        uplink_packet=heap_caps_malloc(MAX_UDP_PAYLOAD,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        mic_samples=heap_caps_malloc(MAX_UDP_PAYLOAD,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (!lock || !packet || !uplink_packet || !mic_samples) {
            if (lock) vSemaphoreDelete(lock);
            heap_caps_free(packet); heap_caps_free(uplink_packet); packet=uplink_packet=NULL; lock=NULL; return false;
        }
        if (xTaskCreate(task,"audio_pcm",4096,NULL,5,NULL)!=pdPASS) {
            heap_caps_free(packet); heap_caps_free(uplink_packet); packet=uplink_packet=NULL;
            vSemaphoreDelete(lock); lock=NULL; return false;
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
    if (microphone) {
        if (airplay_rtsp_pairing_derive_stream_input_key(pairing,id,s->mic.key)!=AIRPLAY_PAIR_VERIFY_OK) goto failed;
        s->mic.wanted=true;
        s->mic.address=*peer;
        s->mic.address.sin6_port=htons((uint16_t)phone_port);
        s->mic.frames=uplink_frames(rate,channels,frames_per_packet);
        // Calls wait for their downlink (it is the echo reference); everything else may start silent.
        if (strcmp(usage,"telephony")) s->mic.start_at=esp_timer_get_time()+SILENT_START_US;
    }
    s->data=udp(&s->data_port);
    s->control=udp(&s->control_port);
    if (s->data<0 || s->control<0) {
        if (s->data>=0) close(s->data);
        if (s->control>=0) close(s->control);
        mbedtls_platform_zeroize(s,sizeof(*s)); goto failed;
    }
    s->type=type; s->id=id; s->format=format; s->rate=rate; s->channels=channels;
    memcpy(s->usage,usage,sizeof(usage));
    // The speaker takes one stream at a time; others are still received and counted.
    s->sink_generation=sink_installed?sink.open(type,rate,channels):0;
    ESP_LOGI("AUDIO","ready type=%u usage=%s rate=%u channels=%u data=%u control=%u sink=%s",
        s->type,s->usage,rate,channels,s->data_port,s->control_port,s->sink_generation?"pcm_playback":"none");
ready:
    *ports=(airplay_stream_ports_t){.type=type,.connection_id=id,.data_port=s->data_port,.control_port=s->control_port};
    xSemaphoreGive(lock); return true;
failed:
    xSemaphoreGive(lock); return false;
}
void airplay_audio_duck(bool ducked) {
    if (sink_installed && sink.duck) sink.duck(ducked);
}
void airplay_audio_stop(unsigned type) {
    if (!lock) return;
    xSemaphoreTake(lock,portMAX_DELAY);
    for (unsigned i=0;i<AUDIO_SLOTS;++i) if (!type || slots[i].type==type) clear(&slots[i]);
    xSemaphoreGive(lock);
}
