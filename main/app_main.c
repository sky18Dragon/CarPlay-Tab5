#include <math.h>
#include <stdint.h>
#include "bsp/m5stack_tab5.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "usb_probe.h"
#include "power_status.h"
#include "app_session.h"
#include "ncm_host.h"
#include "app_ui.h"
#include "video_output.h"
#include "audio_output.h"
#include "offline_demo.h"
#include "media_routes.h"
#include "hid_output.h"
#include "airplay_control_port.h"
#ifdef TAB5_PAIR_RESET_MAINTENANCE
#include "nvs_flash.h"
#include "nvs.h"
static void reset_pairing_maintenance(void)
{
    esp_err_t rc=nvs_flash_init();
    nvs_handle_t handle;
    if(rc==ESP_OK) {
        rc=nvs_open("tab5_pair",NVS_READWRITE,&handle);
        if(rc==ESP_OK) {
            rc=nvs_erase_all(handle); /* Only this opened namespace. */
            if(rc==ESP_OK) rc=nvs_commit(handle);
            nvs_close(handle);
        }
    }
    if(rc==ESP_OK) {
        nvs_iterator_t it=NULL;
        rc=nvs_entry_find("nvs","tab5_pair",NVS_TYPE_ANY,&it);
        nvs_release_iterator(it);
        rc=rc==ESP_ERR_NVS_NOT_FOUND?ESP_OK:ESP_FAIL;
    }
    ESP_LOGI("PAIR_RESET","namespace=tab5_pair result=%s; halted, restore normal firmware",esp_err_to_name(rc));
    /* Never pair in this image: a reboot must not delete a newly created identity. */
    for(;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
#endif
static const char *TAG = "BRINGUP";

#if CONFIG_TAB5_AUDIO_TEST
static void audio_test(void *arg)
{
    (void)arg;
    esp_codec_dev_handle_t speaker = bsp_audio_codec_speaker_init();
    esp_codec_dev_handle_t mic = bsp_audio_codec_microphone_init();
    esp_codec_dev_sample_info_t fmt = {.sample_rate = 48000, .channel = 2, .bits_per_sample = 16};
    int16_t samples[480 * 2];
    if (speaker && esp_codec_dev_open(speaker, &fmt) == ESP_CODEC_DEV_OK) {
        esp_codec_dev_set_out_vol(speaker, 20);
        for (int i = 0; i < 480; i++) samples[2*i] = samples[2*i+1] = (int16_t)(1000 * sinf(2 * 3.14159265f * 1000 * i / 48000));
        for (int i = 0; i < 30; i++) {
            if (esp_codec_dev_write(speaker, samples, sizeof(samples)) != ESP_CODEC_DEV_OK) { ESP_LOGE("AUDIO", "write failed"); break; }
        }
        esp_codec_dev_close(speaker);
        ESP_LOGI("AUDIO", "Tone write complete; audible output requires human verification");
    } else ESP_LOGE("AUDIO", "Speaker open failed");
    if (mic && esp_codec_dev_open(mic, &fmt) == ESP_CODEC_DEV_OK) {
        int peak = 0;
        bool ok = true;
        for (int i = 0; i < 100; i++) {
            if (esp_codec_dev_read(mic, samples, sizeof(samples)) != ESP_CODEC_DEV_OK) { ok = false; break; }
            for (unsigned j = 0; j < sizeof(samples)/sizeof(samples[0]); j++) {
                int v = samples[j]; if (v < 0) v = -v; if (v > peak) peak = v;
            }
        }
        ESP_LOGI("MIC", "read=%s peak=%d (no recordings saved)", ok ? "OK" : "FAIL", peak);
        esp_codec_dev_close(mic);
    } else ESP_LOGE("MIC", "Microphone open failed");
    vTaskDelete(NULL);
}
#endif
void app_main(void)
{
#ifdef TAB5_PAIR_RESET_MAINTENANCE
    reset_pairing_maintenance();
#endif
    ESP_LOGI(TAG, "CarPlay-Tab5 offline development baseline | IDF %s | authentication requires provider", esp_get_idf_version());
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(tab5_power_init());
    ESP_ERROR_CHECK(tab5_audio_init());
    ESP_ERROR_CHECK(tab5_audio_routes_init());
#if CONFIG_TAB5_DISPLAY_TEST
    ESP_ERROR_CHECK(tab5_ui_init());
    ESP_ERROR_CHECK(tab5_video_init());
#if !CONFIG_TAB5_OFFLINE_DEMO
    ESP_ERROR_CHECK(airplay_control_port_init());
    ESP_ERROR_CHECK(tab5_media_routes_init());
    ESP_ERROR_CHECK(tab5_hid_init());
#endif
#endif
#if !CONFIG_TAB5_OFFLINE_DEMO
    ESP_ERROR_CHECK(tab5_session_init());
    ESP_ERROR_CHECK(ncm_host_init());
    ESP_ERROR_CHECK(tab5_usb_start());
    if (xTaskCreate(usb_probe_task, "usb_probe", 6144, NULL, 8, NULL) != pdPASS) abort();
#endif
#if CONFIG_TAB5_OFFLINE_DEMO
    tab5_offline_demo_start();
#endif
#if CONFIG_TAB5_AUDIO_TEST
    if (xTaskCreate(audio_test, "audio_test", 6144, NULL, 5, NULL) != pdPASS) abort();
#endif
    for (unsigned tick = 1;; tick++) {
        if (tick % 6 == 0) tab5_power_log();
        ESP_LOGI(TAG, "heap_internal=%u psram=%u min_heap=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)esp_get_minimum_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
