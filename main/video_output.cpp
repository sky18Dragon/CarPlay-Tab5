#include "video_output.h"
#include "airplay_service.h"
#include "app_ui.h"
#include "core/video_queue.h"
#include "core/video_decoder.h"
#include "carplay_video_profile.h"
#include "esp32p4/rom/cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/soc.h"
#include <atomic>
#include <stdlib.h>
#include <utility>

namespace {
// mutex guards the queue and the frame slots and is held only briefly, because
// submit runs in the screen task. draw serializes display access so that
// tab5_video_start() cannot be followed by a frame of the stream it just ended.
// Lock order: draw, mutex.
SemaphoreHandle_t mutex,draw;
TaskHandle_t worker_task,presenter_task;
// Decoding and display run in parallel on the two cores. Three slots rotate
// between them, so the display may skip a frame without touching the references
// the decoder depends on: the reconstruction task writes one, the display reads
// one, and the third holds the newest finished frame.
struct FrameSlot { uint8_t *memory; unsigned width,height; uint64_t generation; };
constexpr size_t slot_bytes=1280*720*3/2;
// The panel stretches each row into several, so this restores some of the contrast
// between rows. 128 is half of the difference from the rows around a pixel.
constexpr unsigned sharpen_strength=128;
FrameSlot slots[3];
FrameSlot *filling=&slots[0],*showing=&slots[1],*ready=&slots[2];
bool frame_ready;
cp_video_queue queue;
uint8_t *storage,*input;
uint64_t generation,revision;
portMUX_TYPE loss_lock=portMUX_INITIALIZER_UNLOCKED;
uint64_t visible_generation;
bool lost;
// Frame-level counters: submit runs in the screen task, the rest in the worker.
struct Stats {
    std::atomic<uint32_t> submitted{0},idr{0},busy{0},rejected{0},decoded{0},pending{0},errors{0},ui_failed{0},
        starts{0},decoders{0},skipped{0},pack_total_ms{0},decode_max_ms{0},decode_total_ms{0},ui_max_ms{0},ui_total_ms{0},width{0},height{0};
} stats;
void log_stats() {
    static uint32_t seen,seen_starts; static int64_t last;
    const uint32_t total=stats.submitted+stats.pending+stats.decoded+stats.errors, starts=stats.starts;
    const int64_t now=esp_timer_get_time();
    if((total==seen && starts==seen_starts) || now-last<1000000) return;
    seen=total; seen_starts=starts; last=now;
    ESP_LOGI("VIDEO","STATS sub=%lu idr=%lu busy=%lu rej=%lu dec=%lu pend=%lu err=%lu ui_fail=%lu starts=%lu decoders=%lu skip=%lu pack_total=%lums decode_max=%lums decode_total=%lums ui_max=%lums ui_total=%lums size=%lux%lu",
        (unsigned long)stats.submitted,(unsigned long)stats.idr,(unsigned long)stats.busy,(unsigned long)stats.rejected,
        (unsigned long)stats.decoded,(unsigned long)stats.pending,(unsigned long)stats.errors,(unsigned long)stats.ui_failed,
        (unsigned long)starts,(unsigned long)stats.decoders,(unsigned long)stats.skipped,(unsigned long)stats.pack_total_ms,(unsigned long)stats.decode_max_ms,(unsigned long)stats.decode_total_ms,(unsigned long)stats.ui_max_ms,
        (unsigned long)stats.ui_total_ms,(unsigned long)stats.width,(unsigned long)stats.height);
}
void note_max(std::atomic<uint32_t> &slot,int64_t start_us) {
    uint32_t ms=uint32_t((esp_timer_get_time()-start_us)/1000);
    if(ms>slot) slot=ms;
}
// More than one rejected frame in five means the profile outruns this board.
void judge_profile() {
    constexpr uint32_t window=100,limit=5;
    static bool begun,trial;
    if(!begun) { begun=true; trial=carplay_video_profile_begin(); }
    if(!trial) return;
    const uint32_t submitted=stats.submitted;
    if(submitted<window) return;
    trial=false;
    if(uint64_t(stats.rejected)*limit>submitted) {
        ESP_LOGW("VIDEO","Profile too slow: rej=%lu of %lu; next boot uses the safe profile",(unsigned long)stats.rejected,(unsigned long)submitted);
        carplay_video_profile_fallback();
    } else carplay_video_profile_confirm();
}
bool take_loss() {
    portENTER_CRITICAL(&loss_lock); bool value=lost; lost=false; portEXIT_CRITICAL(&loss_lock);
    return value;
}
void mark_loss(uint64_t epoch) {
    portENTER_CRITICAL(&loss_lock);
    if(epoch && epoch==visible_generation) lost=true;
    portEXIT_CRITICAL(&loss_lock);
}
bool idr(const uint8_t *p,size_t size) {
    for(size_t i=0;i+3<size;++i) if(!p[i] && !p[i+1]) {
        size_t n=p[i+2]==1?3:(i+4<size && !p[i+2] && p[i+3]==1?4:0);
        if(n && (p[i+n]&31)==5) return true;
    }
    return false;
}
bool pack_frame(FrameSlot &slot,const cp_i420 &frame,uint64_t stream) {
    const int64_t start=esp_timer_get_time();
    if(!cp_i420_pack(&frame,slot.memory,slot_bytes,sharpen_strength)) return false;
    slot.width=frame.width; slot.height=frame.height; slot.generation=stream;
    stats.pack_total_ms+=uint32_t((esp_timer_get_time()-start)/1000);
    return true;
}
// The stream of the current decoder. Set while no decoder exists, so that no frame
// is on its way here.
uint64_t sink_stream;
// Runs on the reconstruction task, which alone owns the filling slot and decodes
// nothing until this returns, so the time spent here limits the frame rate.
void on_frame(const cp_i420 *frame,void *) {
    const bool copied=pack_frame(*filling,*frame,sink_stream);
    xSemaphoreTake(mutex,portMAX_DELAY);
    const bool same_stream=sink_stream==generation;
    if(same_stream && copied) {
        if(frame_ready) ++stats.skipped;
        std::swap(filling,ready); frame_ready=true;
    }
    xSemaphoreGive(mutex);
    if(same_stream && copied) xTaskNotifyGive(presenter_task);
}
void presenter(void *) {
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        for(;;) {
            xSemaphoreTake(draw,portMAX_DELAY);
            xSemaphoreTake(mutex,portMAX_DELAY);
            const bool have=frame_ready;
            if(have) { frame_ready=false; std::swap(showing,ready); }
            const bool current=have && showing->generation==generation;
            xSemaphoreGive(mutex);
            if(current) {
                const int64_t ui_start=esp_timer_get_time();
                if(tab5_ui_frame(showing->memory,showing->width,showing->height)) { tab5_airplay_media_ready(); judge_profile(); } else ++stats.ui_failed;
                note_max(stats.ui_max_ms,ui_start);
                stats.ui_total_ms+=uint32_t((esp_timer_get_time()-ui_start)/1000);
                stats.width=showing->width; stats.height=showing->height;
            }
            xSemaphoreGive(draw);
            if(!have) break;
        }
    }
}
void worker(void *) {
    cp_decoder *decoder=nullptr; uint64_t decoder_revision=0;
    for(;;) {
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(200));
        log_stats();
        for(;;) {
            cp_video_unit unit; uint64_t current,version;
            xSemaphoreTake(mutex,portMAX_DELAY);
            if(take_loss()) { cp_video_reset(&queue,generation); ++revision; }
            current=generation; version=revision;
            bool have=cp_video_pop(&queue,current,input,512*1024,&unit);
            xSemaphoreGive(mutex);
            if(decoder_revision!=version) {
                cp_decoder_destroy(decoder); decoder=nullptr; decoder_revision=version;
            }
            if(!have) break;
            if(!decoder) {
                sink_stream=current;
                decoder=cp_decoder_create(1280,720);
                cp_decoder_set_sink(decoder,on_frame,nullptr);
                ++stats.decoders;
            }
            cp_i420 frame;
            const int64_t decode_start=esp_timer_get_time();
            cp_decode_result result=decoder?cp_decoder_decode(decoder,input,unit.size,unit.pts,&frame):CP_DECODE_ERROR;
            note_max(stats.decode_max_ms,decode_start);
            stats.decode_total_ms+=uint32_t((esp_timer_get_time()-decode_start)/1000);
            if(result==CP_DECODE_FRAME) ++stats.decoded;
            else if(result==CP_DECODE_ERROR) ++stats.errors;
            else ++stats.pending;
            xSemaphoreTake(mutex,portMAX_DELAY);
            if(current==generation && version==revision && result==CP_DECODE_ERROR) {
                ESP_LOGW("VIDEO","Decode failed; waiting for IDR");
                cp_video_reset(&queue,generation); ++revision;
            }
            xSemaphoreGive(mutex);
            taskYIELD();
        }
    }
}
// Frames, macroblock arrays and code are all read in long runs from PSRAM, so the
// L2 cache fetches the next line on a miss. 7% off each decoded frame. The same on
// the L1 data cache made it slower.
void enable_cache_preload() {
    const l1_dcache_l2_autoload_config config={.gid=0,.order=CACHE_AUTOLOAD_POSITIVE,.trigger=CACHE_AUTOLOAD_MISS_TRIGGER,
        .ena0=1,.ena1=0,.ena2=0,.ena3=0,.addr0=SOC_EXTRAM_LOW,.size0=SOC_EXTRAM_HIGH-SOC_EXTRAM_LOW,
        .addr1=0,.size1=0,.addr2=0,.size2=0,.addr3=0,.size3=0};
    Cache_Config_L2_Cache_Autoload(&config);
    Cache_Enable_L2_Cache_Autoload();
}
}
esp_err_t tab5_video_init(void) {
    if(worker_task) return ESP_OK;
    enable_cache_preload();
    storage=(uint8_t *)heap_caps_malloc(512*1024,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    input=(uint8_t *)heap_caps_malloc(512*1024,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    mutex=xSemaphoreCreateMutex();
    draw=xSemaphoreCreateMutex();
    bool slots_ok=true;
    for(FrameSlot &slot:slots) {
        slot.memory=(uint8_t *)heap_caps_aligned_alloc(16,slot_bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        slots_ok=slots_ok && slot.memory;
    }
    if(storage && input && mutex && draw && slots_ok && cp_video_init(&queue,storage,512*1024) &&
       xTaskCreatePinnedToCore(presenter,"video_show",8192,nullptr,4,&presenter_task,0)==pdPASS) {
        if(xTaskCreatePinnedToCore(worker,"video_decode",12288,nullptr,4,&worker_task,1)==pdPASS) return ESP_OK;
        vTaskDelete(presenter_task); presenter_task=nullptr;
    }
    if(mutex) vSemaphoreDelete(mutex);
    if(draw) vSemaphoreDelete(draw);
    for(FrameSlot &slot:slots) { free(slot.memory); slot.memory=nullptr; }
    free(storage); free(input); storage=input=nullptr; mutex=draw=nullptr; worker_task=nullptr;
    return ESP_ERR_NO_MEM;
}
void tab5_video_start(uint64_t epoch) {
    if(!worker_task) return;
    ++stats.starts;
    ESP_LOGI("VIDEO","START epoch=%llu clears canvas",(unsigned long long)epoch);
    xSemaphoreTake(draw,portMAX_DELAY);
    xSemaphoreTake(mutex,portMAX_DELAY); generation=epoch; ++revision;
    portENTER_CRITICAL(&loss_lock); visible_generation=epoch; lost=false; portEXIT_CRITICAL(&loss_lock);
    cp_video_reset(&queue,epoch); frame_ready=false; xSemaphoreGive(mutex);
    tab5_ui_clear(); xSemaphoreGive(draw);
    xTaskNotifyGive(worker_task);
}
void tab5_video_stop(void) { tab5_video_start(0); }
bool tab5_video_submit(uint64_t epoch,const uint8_t *data,size_t size,uint64_t pts) {
    if(!worker_task || !data || !size || size>512*1024) return false;
    bool key=idr(data,size);
    ++stats.submitted; if(key) ++stats.idr;
    if(xSemaphoreTake(mutex,pdMS_TO_TICKS(5))!=pdTRUE) { ++stats.busy; mark_loss(epoch); xTaskNotifyGive(worker_task); return false; }
    if(take_loss()) { cp_video_reset(&queue,generation); ++revision; }
    uint64_t dropped=queue.dropped;
    bool ok=cp_video_push(&queue,epoch,data,size,pts,key);
    if(queue.dropped!=dropped) ++revision;
    xSemaphoreGive(mutex);
    if(ok) xTaskNotifyGive(worker_task); else ++stats.rejected;
    return ok;
}
bool tab5_video_needs_idr(void) {
    if(!worker_task) return false;
    xSemaphoreTake(mutex,portMAX_DELAY); bool need=cp_video_take_idr_request(&queue);
    xSemaphoreGive(mutex); return need;
}
