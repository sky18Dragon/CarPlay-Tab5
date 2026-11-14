#include "usb_probe.h"
#include <stdatomic.h>
#include "app_session.h"
#include "pairing_reset.h"
#include "usbmux.h"
#include "ncm_host.h"
#include "core/ncm_descriptor.h"
#include "bsp/m5stack_tab5.h"
#include "usb_config_scan.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "usb_descriptors.h"
#include "usb/usb_host.h"
#include "esp_log.h"
#include "sd_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "USB";
static usb_host_client_handle_t client;
static usb_device_handle_t devices[128];
static bool pending[128], gone[128];
static unsigned connected_count, disconnected_count;
static unsigned mux_address,ncm_address,session_address;
static bool session_stopping;
static bool discovery_done[128];
static atomic_uint selected_profile;
static atomic_int network_interface=-1;
/* These limits stop a re-enumeration loop. The phone leaves CarPlay mode on every
 * real detach, so a plug that comes after a quiet period starts with a full budget. */
#define ENUMERATION_QUIET_US 5000000
static unsigned mode_requests,config_restarts;
static int64_t last_enumeration_at;
static int64_t power_restore_at;
static atomic_uint probe_heartbeat_ms;
static atomic_uint recovery_started_ms;
static int64_t pairing_idle_since;
#define USB_PROGRESS_TIMEOUT_MS 15000U

static uint32_t usb_now_ms(void) { return (uint32_t)(esp_timer_get_time()/1000); }

static void usb_guard_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        const uint32_t heartbeat=atomic_load(&probe_heartbeat_ms);
        const uint32_t recovery=atomic_load(&recovery_started_ms);
        const uint32_t now=usb_now_ms();
        const uint32_t quiet=now-heartbeat;
        if(quiet<USB_PROGRESS_TIMEOUT_MS && (!recovery || now-recovery<USB_PROGRESS_TIMEOUT_MS)) continue;
        ESP_LOGE(TAG,"USB progress timeout: heartbeat_age=%lu ms recovery_age=%lu ms; restarting",
                 (unsigned long)quiet,(unsigned long)(recovery ? now-recovery : 0));
        tab5_log_event("usb_progress_timeout");
        /* The SD writer is independent of USB. Give its notification a bounded
         * opportunity to reach disk; a missing/broken card must not block reset. */
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
}

static void begin_power_cycle(void)
{
    atomic_store(&recovery_started_ms,usb_now_ms());
    /* Toggle the board's actual USB-A 5 V supply. Keep the host root port in its
     * normal state so its disconnect/recycle ISR owns the HCD transition. */
    esp_err_t rc=bsp_feature_enable(BSP_FEATURE_USB,false);
    if(rc!=ESP_OK) { ESP_LOGE(TAG,"USB VBUS off failed: %s",esp_err_to_name(rc)); return; }
    power_restore_at=esp_timer_get_time()+500000;
    ESP_LOGW(TAG,"USB VBUS off; waiting for DEV_GONE before endpoint cleanup");
}

int tab5_usb_ncm_interface(void) { return atomic_load(&network_interface); }
static void discover_transport(unsigned a) {
    if(discovery_done[a] || session_stopping || mux_address) return;
    uint8_t wanted=0;
    if(!usb_config_scan_result(a,&wanted)) return;
    discovery_done[a]=true;
    const int64_t enumerated_at=esp_timer_get_time();
    if(enumerated_at-last_enumeration_at>ENUMERATION_QUIET_US) mode_requests=config_restarts=0;
    last_enumeration_at=enumerated_at;
    const usb_device_desc_t *desc;
    usb_device_info_t info;
    if(usb_host_get_device_descriptor(devices[a],&desc)!=ESP_OK || desc->idVendor!=0x05ac ||
       usb_host_device_info(devices[a],&info)!=ESP_OK) return;
    if(!wanted) {
        if(mode_requests++<2) usb_config_scan_request_mode(a);
        else ESP_LOGE(TAG,"No NCM configuration after bounded mode requests; reconnect/reset to retry");
        return;
    }
    if(info.bConfigurationValue!=wanted) {
        if(config_restarts++>=1) { ESP_LOGE(TAG,"Configuration retry limit"); return; }
        atomic_store(&selected_profile,((unsigned)desc->idProduct<<16)|((unsigned)desc->bNumConfigurations<<8)|wanted);
        ESP_LOGI(TAG,"Selecting discovered NCM config=%u by root re-enumeration",wanted);
        begin_power_cycle();
        return;
    }
    const usb_config_desc_t *config; cp_ncm_descriptor ncm;
    if(usb_host_get_active_config_descriptor(devices[a],&config)!=ESP_OK ||
       !cp_ncm_discover((const uint8_t *)config,config->wTotalLength,&ncm)) return;
    esp_err_t rc=ncm_host_start(client,devices[a]);
    ESP_LOGI(TAG,"NCM start=%s control_interface=%u",esp_err_to_name(rc),ncm.control);
    if(rc!=ESP_OK) return;
    ncm_address=a; atomic_store(&network_interface,ncm.control);
    rc=usbmux_start(client,devices[a]);
    ESP_LOGI(TAG,"USBMUX start=%s",esp_err_to_name(rc));
    if(rc==ESP_OK) { mux_address=session_address=a; tab5_session_attach(usbmux_epoch()); }
}

uint32_t tab5_usb_hal_recovered_errors(void);
/* A real unplug also reports a link fault. Let its pending disconnect arrive
 * before cycling VBUS, at most three times per minute. Never manually change the
 * HCD root-port state or synthesize DEV_GONE while its pipes may still be live. */
#define NCM_RECOVERY_DELAY_US 1000000
static int64_t recover_at; static unsigned recover_address;
static void recover_ncm(unsigned a)
{
    if(recover_at) return;
    atomic_store(&recovery_started_ms,usb_now_ms());
    tab5_log_event("ncm_fault");
    ESP_LOGW(TAG,"NCM link failed (usb_hal_recovered=%lu); checking the phone in 1 s",
             (unsigned long)tab5_usb_hal_recovered_errors());
    recover_address=a; recover_at=esp_timer_get_time()+NCM_RECOVERY_DELAY_US;
}
static void recover_ncm_due(void)
{
    if(!recover_at || esp_timer_get_time()<recover_at) return;
    const unsigned a=recover_address; recover_at=0;
    if(!devices[a] || gone[a]) { ESP_LOGI(TAG,"NCM recovery skipped: the phone detached by itself"); return; }
    static int64_t window_start; static unsigned recoveries;
    const int64_t now=esp_timer_get_time();
    if(now-window_start>60000000) { window_start=now; recoveries=0; }
    if(recoveries>=3) {
        ESP_LOGE(TAG,"NCM failed again; recovery limit reached, replug the phone");
        atomic_store(&recovery_started_ms,0); return;
    }
    ++recoveries;
    tab5_log_event("ncm_recovery");
    ESP_LOGW(TAG,"re-enumerating the phone (recovery %u)",recoveries);
    begin_power_cycle();
}

static void event_cb(const usb_host_client_event_msg_t *event, void *arg)
{
    (void)arg;
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        unsigned addr = event->new_dev.address;
        if (addr > 0 && addr < 128) pending[addr] = true;
    } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        for (unsigned a = 1; a < 128; ++a)
            if (devices[a] == event->dev_gone.dev_hdl) gone[a] = true;
    }
}

static void descriptor_log(const uint8_t *d, size_t n, void *ctx)
{
    (void)n; (void)ctx;
    if (d[1] == 4) ESP_LOGI(TAG, "IF num=%u alt=%u class=%02x sub=%02x proto=%02x endpoints=%u",
                           d[2], d[3], d[5], d[6], d[7], d[4]);
    if (d[1] == 5) ESP_LOGI(TAG, "EP addr=%02x type=%u max_packet=%u interval=%u",
                           d[2], d[3] & 3, d[4] | (d[5] << 8), d[6]);
}

void usb_probe_task(void *arg)
{
    (void)arg;
    atomic_store(&probe_heartbeat_ms,usb_now_ms());
    if(xTaskCreate(usb_guard_task,"usb_guard",3072,NULL,4,NULL)!=pdPASS) abort();
    const usb_host_client_config_t cfg = {
        .is_synchronous = false, .max_num_event_msg = 16,
        .async = {.client_event_callback = event_cb},
    };
    ESP_ERROR_CHECK(usb_host_client_register(&cfg, &client));
    ESP_ERROR_CHECK(usb_host_lib_set_root_port_power(true));
    ESP_LOGI(TAG, "Probe ready; USBMUX pairing and NCM discovery enabled");
    int64_t status_at = 0;
    for (;;) {
        atomic_store(&probe_heartbeat_ms,usb_now_ms());
        if(power_restore_at && esp_timer_get_time()>=power_restore_at) {
            bool closed=true;for(unsigned a=1;a<128;++a) if(devices[a]) closed=false;
            if(closed) {
                ESP_ERROR_CHECK(bsp_feature_enable(BSP_FEATURE_USB,true));
                power_restore_at=0; atomic_store(&recovery_started_ms,0);
                ESP_LOGI(TAG,"USB VBUS restored; ready for re-enumeration");
            }
        }
        if (esp_timer_get_time() >= status_at) {
            unsigned active = 0;
            for (unsigned i = 1; i < 128; ++i) active += devices[i] != NULL;
            ESP_LOGI(TAG, "STATUS active=%u connected=%u disconnected=%u usb_hal_recovered=%lu", active, connected_count, disconnected_count,
                     (unsigned long)tab5_usb_hal_recovered_errors());
            ESP_LOGI("USBMUX", "STATUS %s", usbmux_status());
            status_at = esp_timer_get_time() + 10000000;
        }
        cp_action action=tab5_session_poll();
        /* While a faulty link awaits physical disconnection, avoid any path
         * that could synchronously halt its still-live endpoints. */
        const bool recovering=atomic_load(&recovery_started_ms)!=0;
        if(action==CP_ACTION_STOP && session_address && !recovering) { usbmux_disconnect(); session_stopping=true; }
        if(session_stopping && usbmux_drained() && usbmux_worker_drained()) {
            usbmux_release(); mux_address=0; session_stopping=false;
            tab5_session_drained();
        }
        if(action==CP_ACTION_RECONNECT && session_address && devices[session_address] && !gone[session_address] && !recovering) {
            esp_err_t rc=usbmux_start(client,devices[session_address]);
            if(rc==ESP_OK) { mux_address=session_address; tab5_session_attach(usbmux_epoch()); }
            else tab5_session_post(usbmux_epoch(),CP_IO_ERROR);
        }
        esp_err_t err = usb_host_client_handle_events(client, pdMS_TO_TICKS(100));
        recover_ncm_due();
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) ESP_LOGE(TAG, "events: %s", esp_err_to_name(err));
        for (unsigned a = 1; a < 128; ++a) {
            if (gone[a]) {
                if(session_address==a) { tab5_session_detach(); session_address=0; session_stopping=true; usbmux_disconnect(); }

                if (mux_address == a) {
                    usbmux_disconnect();
                    if (!usbmux_drained() || !usbmux_worker_drained()) continue;
                    usbmux_release(); mux_address = 0;
                    if(session_stopping) { session_stopping=false; tab5_session_drained(); }
                }
                /* The Lockdown/iAP2 worker stops RTSP/media before its network
                 * interface is removed. Keep pumping USB callbacks while it exits. */
                if(ncm_address==a) {
                    ncm_host_disconnect();
                    if(!ncm_host_drained()) continue;
                    ncm_host_release(); ncm_address=0; atomic_store(&network_interface,-1);
                }
                if (!usb_config_scan_release(a)) continue;
                err = usb_host_device_close(client, devices[a]);
                if (err != ESP_OK) { ESP_LOGE(TAG, "close addr=%u: %s", a, esp_err_to_name(err)); continue; }
                devices[a] = NULL; discovery_done[a]=false;
                gone[a] = false;
                /* Leave time for usb_lib to recycle the closed device's root
                 * port before the phone sees VBUS again. */
                if(power_restore_at) power_restore_at=esp_timer_get_time()+500000;
                ESP_LOGI(TAG, "DISCONNECTED addr=%u count=%u heap=%u min=%u", a, ++disconnected_count,
                         (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
                tab5_log_event("usb_disconnected");
            }
            if (devices[a] && !atomic_load(&recovery_started_ms)) { usb_config_scan_poll(a); discover_transport(a); }
            if (mux_address == a && !atomic_load(&recovery_started_ms)) usbmux_poll();
            if (ncm_address == a) { ncm_host_poll(); if (ncm_host_take_fault()) recover_ncm(a); }
            if (!pending[a] || devices[a] || session_stopping) continue;
            pending[a] = false;
            err = usb_host_device_open(client, a, &devices[a]);
            if (err != ESP_OK) { devices[a] = NULL; ESP_LOGW(TAG, "open addr=%u: %s", a, esp_err_to_name(err)); continue; }
            const usb_device_desc_t *desc;
            const usb_config_desc_t *config;
            usb_device_info_t info;
            if (usb_host_get_device_descriptor(devices[a], &desc) == ESP_OK) {
                ESP_LOGI(TAG, "CONNECTED addr=%u VID=%04x PID=%04x Apple=%s count=%u", a,
                         desc->idVendor, desc->idProduct, desc->idVendor == 0x05ac ? "yes" : "no", ++connected_count);
                if (desc->idVendor == 0x05ac) usb_config_scan_start(client, devices[a], a, desc->bNumConfigurations);
            }
            if (usb_host_device_info(devices[a], &info) == ESP_OK) ESP_LOGI(TAG, "speed_enum=%d config=%u", info.speed, info.bConfigurationValue);
            if (usb_host_get_active_config_descriptor(devices[a], &config) == ESP_OK &&
                !usb_descriptors_walk((const uint8_t *)config, config->wTotalLength, descriptor_log, NULL))
                ESP_LOGE(TAG, "Malformed configuration descriptor addr=%u", a);

        }
        if(atomic_load(&recovery_started_ms) && !recover_at && !power_restore_at) {
            bool closed=true;for(unsigned a=1;a<128;++a) if(devices[a]) closed=false;
            if(closed) atomic_store(&recovery_started_ms,0);
        }
        /* Clearing runs on this owner after all transport workers have exited.
         * A quiet interval excludes temporary disconnects during re-enumeration.
         * While NVS is busy no new worker can start; the existing USB guard
         * bounds a stalled flash operation independently of this task. */
        bool idle=!session_stopping && !session_address && !mux_address && !ncm_address &&
                  !recover_at && !power_restore_at && !atomic_load(&recovery_started_ms) &&
                  usbmux_drained() && usbmux_worker_drained() && ncm_host_drained();
        for(unsigned a=1;a<128;++a) if(devices[a] || pending[a] || gone[a]) idle=false;
        if(!idle) pairing_idle_since=0;
        else if(!pairing_idle_since) pairing_idle_since=esp_timer_get_time();
        tab5_pair_reset_poll(idle && esp_timer_get_time()-pairing_idle_since>=1000000);
    }
}

/* Device-specific profile established by the recorded four-configuration scan.
 * Other devices keep their default configuration; never guess from config count alone. */
static bool select_configuration(const usb_device_desc_t *d, uint8_t *value)
{
    unsigned cached=atomic_load(&selected_profile);
    if(d->idVendor==0x05ac && (cached>>16)==d->idProduct && ((cached>>8)&255)==d->bNumConfigurations && (cached&255)) {
        *value=cached&255; return true;
    }
    if (d->idVendor == 0x05ac && d->idProduct == 0x12a8 && d->bNumConfigurations == 4) {
        *value = 3;
        ESP_LOGI(TAG, "Selecting observed USBMUX configuration 3 for 05ac:12a8");
    }
    return true;
}
static void usb_daemon(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &flags));
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}
esp_err_t tab5_usb_start(void)
{
    esp_err_t rc = bsp_feature_enable(BSP_FEATURE_USB, true);
    if (rc != ESP_OK) return rc;
    const usb_host_config_t cfg = {.intr_flags = ESP_INTR_FLAG_LOWMED,
        .root_port_unpowered = true, .enum_filter_cb = select_configuration};
    rc = usb_host_install(&cfg);
    if (rc != ESP_OK) return rc;
    if (xTaskCreate(usb_daemon, "usb_lib", 4096, NULL, 10, NULL) != pdPASS) abort();
    return ESP_OK;
}
