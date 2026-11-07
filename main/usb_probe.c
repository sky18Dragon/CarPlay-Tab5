#include "usb_probe.h"
#include <stdatomic.h>
#include "app_session.h"
#include "usbmux.h"
#include "ncm_host.h"
#include "core/ncm_descriptor.h"
#include "bsp/m5stack_tab5.h"
#include "usb_config_scan.h"
#include "esp_timer.h"
#include "usb_descriptors.h"
#include "usb/usb_host.h"
#include "esp_log.h"
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
        if(usb_host_lib_set_root_port_power(false)==ESP_OK) {
            gone[a]=true; power_restore_at=esp_timer_get_time()+500000;
        }
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
    const usb_host_client_config_t cfg = {
        .is_synchronous = false, .max_num_event_msg = 16,
        .async = {.client_event_callback = event_cb},
    };
    ESP_ERROR_CHECK(usb_host_client_register(&cfg, &client));
    ESP_ERROR_CHECK(usb_host_lib_set_root_port_power(true));
    ESP_LOGI(TAG, "Probe ready; USBMUX pairing and NCM discovery enabled");
    int64_t status_at = 0;
    for (;;) {
        if(power_restore_at && esp_timer_get_time()>=power_restore_at) {
            bool closed=true;for(unsigned a=1;a<128;++a) if(devices[a]) closed=false;
            if(closed) { ESP_ERROR_CHECK(usb_host_lib_set_root_port_power(true)); power_restore_at=0; }
        }
        if (esp_timer_get_time() >= status_at) {
            unsigned active = 0;
            for (unsigned i = 1; i < 128; ++i) active += devices[i] != NULL;
            ESP_LOGI(TAG, "STATUS active=%u connected=%u disconnected=%u", active, connected_count, disconnected_count);
            ESP_LOGI("USBMUX", "STATUS %s", usbmux_status());
            status_at = esp_timer_get_time() + 10000000;
        }
        cp_action action=tab5_session_poll();
        if(action==CP_ACTION_STOP && session_address) { usbmux_disconnect(); session_stopping=true; }
        if(session_stopping && usbmux_drained() && usbmux_worker_drained()) {
            usbmux_release(); mux_address=0; session_stopping=false;
            tab5_session_drained();
        }
        if(action==CP_ACTION_RECONNECT && session_address && devices[session_address] && !gone[session_address]) {
            esp_err_t rc=usbmux_start(client,devices[session_address]);
            if(rc==ESP_OK) { mux_address=session_address; tab5_session_attach(usbmux_epoch()); }
            else tab5_session_post(usbmux_epoch(),CP_IO_ERROR);
        }
        esp_err_t err = usb_host_client_handle_events(client, pdMS_TO_TICKS(100));
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
                ESP_LOGI(TAG, "DISCONNECTED addr=%u count=%u heap=%u min=%u", a, ++disconnected_count,
                         (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
            }
            if (devices[a]) { usb_config_scan_poll(a); discover_transport(a); }
            if (mux_address == a) usbmux_poll();
            if (ncm_address == a) ncm_host_poll();
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
