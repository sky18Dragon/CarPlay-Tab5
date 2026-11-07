/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "airplay_media_bridge.h"

#include "airplay_pair_verify.h"
#include "media_bridge.h"
#include "mbedtls/platform_util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

static SemaphoreHandle_t selection_lock;
static StaticSemaphore_t selection_storage;
static portMUX_TYPE selection_init_lock=portMUX_INITIALIZER_UNLOCKED;
static unsigned selected=110;
static uint8_t *configs[2];
static size_t config_lengths[2];
static uint64_t ids[2];
static uint16_t ports[2];
static void init_selection(void) {
    portENTER_CRITICAL(&selection_init_lock);
    if (!selection_lock) selection_lock=xSemaphoreCreateMutexStatic(&selection_storage);
    portEXIT_CRITICAL(&selection_init_lock);
}
unsigned airplay_media_bridge_selected_screen(void) {
    return __atomic_load_n(&selected,__ATOMIC_RELAXED);
}
void airplay_media_bridge_select_screen(unsigned type) {
    if (type!=110 && type!=111) return;
    init_selection(); xSemaphoreTake(selection_lock,portMAX_DELAY);
    __atomic_store_n(&selected,type,__ATOMIC_RELAXED); media_bridge_reset();
    unsigned slot=type-110;
    if (config_lengths[slot]) media_bridge_set_h264_config(configs[slot],config_lengths[slot]);
    xSemaphoreGive(selection_lock);
    ESP_LOGI("screen_select","selected=%u decoder=shared",type);
}
static esp_err_t typed_message(airplay_screen_message_type_t type,uint8_t *data,size_t len,
    size_t prefix,uint64_t sender,uint64_t arrival,void *context) {
    unsigned slot=(unsigned)(uintptr_t)context;
    xSemaphoreTake(selection_lock,portMAX_DELAY);
    esp_err_t result=ESP_OK;
    if (type==AIRPLAY_SCREEN_MESSAGE_CONFIG_H264) {
        if (len>4096) result=ESP_ERR_INVALID_SIZE;
        else {
            if (!configs[slot]) configs[slot]=heap_caps_malloc(4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if (!configs[slot]) result=ESP_ERR_NO_MEM;
            else { memcpy(configs[slot],data,len); config_lengths[slot]=len; }
        }
    }
    if (result==ESP_OK && selected==slot+110)
        result=airplay_media_bridge_on_screen_message(type,data,len,prefix,sender,arrival,NULL);
    xSemaphoreGive(selection_lock);
    return result;
}
esp_err_t airplay_media_bridge_start_typed_screen(const airplay_rtsp_pairing_t *pairing,
    unsigned type,uint64_t connection_id,airplay_screen_server_exit_cb_t exit_cb,
    void *exit_context,uint16_t *port) {
    if (type!=110 && type!=111) return ESP_ERR_INVALID_ARG;
    init_selection();
    unsigned slot=type-110;
    if (ports[slot] && ids[slot]==connection_id) { *port=ports[slot]; return ESP_OK; }
    if (ports[slot]) airplay_media_bridge_stop_typed_screen(type);
    uint8_t key[32];
    if (airplay_rtsp_pairing_derive_screen_output_key(pairing,connection_id,key)!=AIRPLAY_PAIR_VERIFY_OK) return ESP_FAIL;
    esp_err_t err=airplay_screen_server_start_slot(slot,0,key,AIRPLAY_SCREEN_SERVER_MAX_BODY_BYTES,
        typed_message,(void *)(uintptr_t)slot,exit_cb,exit_context,port);
    mbedtls_platform_zeroize(key,sizeof(key));
    if (err==ESP_OK) { ports[slot]=*port; ids[slot]=connection_id; }
    return err;
}
void airplay_media_bridge_stop_typed_screen(unsigned type) {
    if (type!=110 && type!=111) return;
    unsigned slot=type-110;
    airplay_screen_server_stop_slot(slot);
    init_selection(); xSemaphoreTake(selection_lock,portMAX_DELAY);
    ports[slot]=0; ids[slot]=0;
    heap_caps_free(configs[slot]); configs[slot]=NULL; config_lengths[slot]=0;
    if (selected==type) media_bridge_reset();
    xSemaphoreGive(selection_lock);
}

esp_err_t airplay_media_bridge_on_screen_message(airplay_screen_message_type_t type,
                                                 uint8_t *annex_b,
                                                 size_t annex_b_len,
                                                 size_t prefix_capacity,
                                                 uint64_t sender_time_ns,
                                                 uint64_t arrival_time_us,
                                                 void *context)
{
    (void)context;
    if (annex_b == NULL || annex_b_len == 0) return ESP_ERR_INVALID_ARG;

    switch (type) {
    case AIRPLAY_SCREEN_MESSAGE_CONFIG_H264:
        return media_bridge_set_h264_config(annex_b, annex_b_len);
    case AIRPLAY_SCREEN_MESSAGE_FRAME_H264:
        return media_bridge_send_h264_borrowed(annex_b, prefix_capacity,
                                               annex_b_len, sender_time_ns,
                                               arrival_time_us);
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t airplay_media_bridge_start_screen_stream(
    const airplay_rtsp_pairing_t *pairing,
    uint64_t stream_connection_id,
    uint16_t requested_port,
    airplay_screen_server_exit_cb_t on_unexpected_exit,
    void *exit_context,
    uint16_t *out_port)
{
    if (pairing == NULL || out_port == NULL) return ESP_ERR_INVALID_ARG;
    *out_port = 0U;
    uint8_t output_key[AIRPLAY_SCREEN_KEY_BYTES];
    airplay_pair_verify_result_t key_result =
        airplay_rtsp_pairing_derive_screen_output_key(
            pairing, stream_connection_id, output_key);
    if (key_result != AIRPLAY_PAIR_VERIFY_OK) {
        mbedtls_platform_zeroize(output_key, sizeof(output_key));
        return key_result == AIRPLAY_PAIR_VERIFY_STATE_ERROR
                   ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
    }
    media_bridge_reset();
    esp_err_t result = airplay_screen_server_start(
        requested_port, output_key, AIRPLAY_SCREEN_SERVER_MAX_BODY_BYTES,
        airplay_media_bridge_on_screen_message, NULL,
        on_unexpected_exit, exit_context, out_port);
    mbedtls_platform_zeroize(output_key, sizeof(output_key));
    return result;
}

void airplay_media_bridge_stop_screen_stream(void)
{
    airplay_media_bridge_stop_typed_screen(110);
    airplay_media_bridge_stop_typed_screen(111);
}
