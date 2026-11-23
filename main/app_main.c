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
#include "core/battery.h"
static const char *TAG = "APP";
#define BATTERY_PERIOD_MS 5000u
#define POWER_OFF_NOTICE_MS 8000u

/* One battery reading: updates the UI and, after a sustained empty pack, warns and
 * turns the Tab5 off. */
static void battery_step(cp_battery *battery)
{
    tab5_power_reading reading;
    if (!tab5_power_read(&reading)) return;
    const cp_battery_level before = battery->level;
    const cp_battery_level level = cp_battery_sample(battery, reading.battery_mv, reading.charging);
    tab5_ui_battery(cp_battery_percent(battery), reading.charging, level == CP_BATTERY_LOW || level == CP_BATTERY_EMPTY);
    if (level != before)
        ESP_LOGW(TAG, "battery level %d -> %d: %u mV, %d mA, %d%%", (int)before, (int)level,
                 reading.battery_mv, reading.charge_ma, cp_battery_percent(battery));
    if (level != CP_BATTERY_EMPTY) return;
    tab5_log_event("battery_empty_power_off");
    tab5_ui_power_off_notice();
    vTaskDelay(pdMS_TO_TICKS(POWER_OFF_NOTICE_MS));
    /* Plugging in during the notice cancels the power-off. */
    if (tab5_power_read(&reading) && reading.charging) {
        ESP_LOGW(TAG, "charger connected during power-off notice; restarting instead");
        esp_restart();
    }
    tab5_power_off();
}

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
    tab5_audio_begin();
    if (xTaskCreate(usb_probe_task, "usb_probe", 6144, NULL, 8, NULL) != pdPASS) abort();
    cp_battery battery;
    cp_battery_init(&battery);
    for (unsigned tick = 1;; tick++) {
        battery_step(&battery);
        if (tick % 12 == 0) tab5_power_log();
        if (tick % 2 == 0)
            ESP_LOGI(TAG, "heap_internal=%u dma_free=%u dma_largest=%u psram=%u min_heap=%u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)esp_get_minimum_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(BATTERY_PERIOD_MS));
    }
}
