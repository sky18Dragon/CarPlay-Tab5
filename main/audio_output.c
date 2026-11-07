#include "audio_output.h"
#include "core/pcm_buffer.h"
#include "bsp/m5stack_tab5.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>
#define AUDIO_CAPACITY 4800
/* Only the worker touches the codec. Control callers serialize via control_lock
 * and wait for ack, so stop returns after old samples and codec are closed. */
typedef struct { uint64_t generation; uint32_t rate; unsigned channels; } audio_command;
static SemaphoreHandle_t buffer_lock,control_lock,ack;
static QueueHandle_t commands;
static cp_pcm pcm;
static uint8_t *storage,*valid;
static esp_err_t command_result;
static bool active;
static void audio_worker(void *arg)
{
    (void)arg;
    esp_codec_dev_handle_t codec=NULL;
    uint32_t rate=48000; unsigned channels=2;
    uint8_t samples[480*4];
    for(;;) {
        audio_command command;
        if(xQueueReceive(commands,&command,active?0:portMAX_DELAY)==pdTRUE) {
            xSemaphoreTake(buffer_lock,portMAX_DELAY); active=false; cp_pcm_reset(&pcm,0); xSemaphoreGive(buffer_lock);
            if(codec) esp_codec_dev_close(codec);
            command_result=ESP_OK;
            if(command.generation) {
                if(!codec) codec=bsp_audio_codec_speaker_init();
                esp_codec_dev_sample_info_t fmt={.sample_rate=command.rate,.channel=command.channels,.bits_per_sample=16};
                if(!codec || esp_codec_dev_open(codec,&fmt)!=ESP_CODEC_DEV_OK) command_result=ESP_FAIL;
                else if(esp_codec_dev_set_out_vol(codec,30)!=ESP_CODEC_DEV_OK) {
                    esp_codec_dev_close(codec); command_result=ESP_FAIL;
                } else {
                    rate=command.rate; channels=command.channels;
                    xSemaphoreTake(buffer_lock,portMAX_DELAY);
                    cp_pcm_init(&pcm,storage,valid,AUDIO_CAPACITY,rate,channels,rate*60/1000);
                    cp_pcm_reset(&pcm,command.generation); active=true;
                    xSemaphoreGive(buffer_lock);
                }
            }
            xSemaphoreGive(ack);
        }
        if(!active) continue;
        size_t frames=rate/100; /* 10 ms per codec write; USB callback never waits here. */
        xSemaphoreTake(buffer_lock,portMAX_DELAY);
        cp_pcm_read(&pcm,pcm.generation,samples,frames);
        xSemaphoreGive(buffer_lock);
        if(esp_codec_dev_write(codec,samples,frames*channels*2)!=ESP_CODEC_DEV_OK) {
            ESP_LOGE("AUDIO","Playback write failed; stream stopped");
            xSemaphoreTake(buffer_lock,portMAX_DELAY); active=false; cp_pcm_reset(&pcm,0); xSemaphoreGive(buffer_lock);
            esp_codec_dev_close(codec);
        }
    }
}
esp_err_t tab5_audio_init(void)
{
    if(commands) return ESP_OK;
    storage=heap_caps_calloc(AUDIO_CAPACITY,4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    valid=calloc(AUDIO_CAPACITY,1);
    buffer_lock=xSemaphoreCreateMutex(); control_lock=xSemaphoreCreateMutex(); ack=xSemaphoreCreateBinary();
    commands=xQueueCreate(1,sizeof(audio_command));
    if(!storage || !valid || !buffer_lock || !control_lock || !ack || !commands) goto fail;
    cp_pcm_init(&pcm,storage,valid,AUDIO_CAPACITY,48000,2,2880);
    if(xTaskCreate(audio_worker,"pcm_play",6144,NULL,6,NULL)!=pdPASS) goto fail;
    return ESP_OK;
fail:
    if(commands) vQueueDelete(commands);
    if(buffer_lock) vSemaphoreDelete(buffer_lock);
    if(control_lock) vSemaphoreDelete(control_lock);
    if(ack) vSemaphoreDelete(ack);
    free(storage); free(valid); storage=valid=NULL; commands=NULL;
    buffer_lock=control_lock=ack=NULL;
    return ESP_ERR_NO_MEM;
}
static esp_err_t configure(audio_command command)
{
    if(!commands) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(control_lock,portMAX_DELAY);
    xQueueSend(commands,&command,portMAX_DELAY);
    xSemaphoreTake(ack,portMAX_DELAY);
    esp_err_t result=command_result;
    xSemaphoreGive(control_lock); return result;
}
esp_err_t tab5_audio_start(uint64_t generation,uint32_t rate,unsigned channels)
{
    if(!generation || (rate!=44100 && rate!=48000) || (channels!=1 && channels!=2)) return ESP_ERR_INVALID_ARG;
    return configure((audio_command){generation,rate,channels});
}
void tab5_audio_stop(void) { if(commands) (void)configure((audio_command){0}); }
bool tab5_audio_submit(uint64_t generation,uint32_t timestamp,const uint8_t *data,size_t bytes,bool big)
{
    if(!buffer_lock || xSemaphoreTake(buffer_lock,0)!=pdTRUE) return false;
    bool ok=active && cp_pcm_push(&pcm,generation,timestamp,data,bytes,big);
    xSemaphoreGive(buffer_lock); return ok;
}
