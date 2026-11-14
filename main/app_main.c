#include <stdint.h>
#include "bsp/m5stack_tab5.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "usb_probe.h"
#include "power_status.h"
#include "app_session.h"
#include "ncm_host.h"
#include "app_ui.h"
#include "video_output.h"
#include "audio_output.h"
#include "media_routes.h"
#include "hid_output.h"
#include "airplay_control_port.h"
#include "sd_log.h"
static const char *TAG = "APP";

void app_main(void)
{
    tab5_log_init();
    ESP_LOGI(TAG, "CarPlay-Tab5 v%s | IDF %s", esp_app_get_description()->version, esp_get_idf_version());
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(tab5_power_init());
    ESP_ERROR_CHECK(tab5_audio_init());
    ESP_ERROR_CHECK(tab5_audio_routes_init());
    ESP_ERROR_CHECK(tab5_ui_init());
    ESP_ERROR_CHECK(tab5_video_init());
    ESP_ERROR_CHECK(airplay_control_port_init());
    ESP_ERROR_CHECK(tab5_media_routes_init());
    ESP_ERROR_CHECK(tab5_hid_init());
    ESP_ERROR_CHECK(tab5_session_init());
    ESP_ERROR_CHECK(ncm_host_init());
    ESP_ERROR_CHECK(tab5_usb_start());
    if (xTaskCreate(usb_probe_task, "usb_probe", 6144, NULL, 8, NULL) != pdPASS) abort();
    for (unsigned tick = 1;; tick++) {
        if (tick % 6 == 0) tab5_power_log();
        ESP_LOGI(TAG, "heap_internal=%u psram=%u min_heap=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)esp_get_minimum_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
