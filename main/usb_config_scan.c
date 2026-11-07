#include "usb_config_scan.h"
#include "usb_descriptors.h"
#include "core/ncm_descriptor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#define CAPACITY 4096
static const char *TAG = "USB_SCAN";
typedef struct {
    usb_transfer_t *transfer;
    usb_host_client_handle_t client;
    unsigned index, count, expected;
    bool busy, done, body, stopped, expired, complete, mode;
    uint8_t network_configuration;
    int64_t deadline;
} scan_t;
static scan_t scans[128];
static void completed(usb_transfer_t *t)
{
    scan_t *s = t->context;
    s->busy = false;
    s->done = true;
}
static void submit(scan_t *s, unsigned length, bool body)
{
    usb_transfer_t *t = s->transfer;
    memset(t->data_buffer, 0, CAPACITY + 8);
    usb_setup_packet_t *setup = (usb_setup_packet_t *)t->data_buffer;
    setup->bmRequestType = 0x80;
    setup->bRequest = 6; /* Standard GET_DESCRIPTOR, never a vendor request. */
    setup->wValue = (2 << 8) | s->index;
    setup->wLength = length;
    t->num_bytes = length + 8;
    s->expected = length;
    s->body = body;
    s->done = false;
    esp_err_t err = usb_host_transfer_submit_control(s->client, t);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "submit index=%u failed: %s", s->index, esp_err_to_name(err));
        s->stopped = true;
        return;
    }
    s->busy = true;
    s->deadline = esp_timer_get_time() + 3000000;
}
void usb_config_scan_start(usb_host_client_handle_t client, usb_device_handle_t device, unsigned address, unsigned count)
{
    if (address == 0 || address >= 128 || count == 0) return;
    scan_t *s = &scans[address];
    if (s->transfer) return;
    memset(s, 0, sizeof(*s));
    if (usb_host_transfer_alloc(CAPACITY + 8, 0, &s->transfer) != ESP_OK) {
        ESP_LOGE(TAG, "No scan buffer addr=%u", address);
        return;
    }
    s->client = client;
    s->count = count;
    s->transfer->device_handle = device;
    s->transfer->bEndpointAddress = 0;
    s->transfer->callback = completed;
    s->transfer->context = s;
    ESP_LOGI(TAG, "START addr=%u configurations=%u", address, count);
    submit(s, 9, false);
}
static void describe(const uint8_t *d, size_t length, void *ctx)
{
    (void)length; (void)ctx;
    if (d[1] == 4) {
        const char *kind = d[5] == 0xff && d[6] == 0xfe && d[7] == 2 ? "USBMUX" :
                           d[5] == 2 && d[6] == 0x0d ? "NCM_CONTROL" : "OTHER";
        ESP_LOGI(TAG, "IF=%u alt=%u class=%02x/%02x/%02x kind=%s", d[2], d[3], d[5], d[6], d[7], kind);
    } else if (d[1] == 5) {
        ESP_LOGI(TAG, "EP=%02x type=%u mps=%u", d[2], d[3] & 3, (d[4] | (d[5] << 8)) & 0x7ff);
    }
}
void usb_config_scan_poll(unsigned address)
{
    if (address >= 128) return;
    scan_t *s = &scans[address];
    if (!s->transfer) return;
    if (s->busy && !s->expired && esp_timer_get_time() > s->deadline) {
        s->expired = true;
        s->stopped = true;
        ESP_LOGE(TAG, "TIMEOUT addr=%u index=%u; buffer retained until completion/disconnect", address, s->index);
    }
    if(s->mode && s->done) {
        s->done=false;
        ESP_LOGI(TAG,"MODE_REQUEST status=%d bytes=%d; awaiting re-enumeration",s->transfer->status,s->transfer->actual_num_bytes);
        return;
    }
    if (s->stopped || !s->done) return;
    s->done = false;
    usb_transfer_t *t = s->transfer;
    if (t->status != USB_TRANSFER_STATUS_COMPLETED || t->actual_num_bytes != (int)s->expected + 8) {
        ESP_LOGW(TAG, "INCOMPLETE addr=%u index=%u status=%d bytes=%d expected=%u", address, s->index,
                 t->status, t->actual_num_bytes, s->expected + 8);
        s->stopped = true;
        return;
    }
    const uint8_t *d = t->data_buffer + 8;
    size_t total = d[2] | (d[3] << 8);
    if (d[0] < 9 || d[1] != 2 || total < 9 || total > CAPACITY) {
        ESP_LOGE(TAG, "Invalid configuration header addr=%u index=%u size=%u", address, s->index, (unsigned)total);
        s->stopped = true;
        return;
    }
    if (!s->body) { submit(s, total, true); return; }
    if (total != s->expected || !usb_descriptors_walk(d, total, NULL, NULL)) {
        ESP_LOGE(TAG, "Invalid configuration body addr=%u index=%u", address, s->index);
        s->stopped = true;
        return;
    }
    ESP_LOGI(TAG, "CONFIG addr=%u index=%u value=%u interfaces=%u bytes=%u", address, s->index, d[5], d[4], (unsigned)total);
    usb_descriptors_walk(d, total, describe, NULL);
    cp_ncm_descriptor ncm; bool mux=false;
    for(size_t off=0;off<total;off+=d[off]) {
        const uint8_t *item=d+off;
        if(item[1]==4 && item[3]==0 && item[5]==0xff && item[6]==0xfe && item[7]==2) mux=true;
    }
    if(mux && cp_ncm_discover(d,total,&ncm) && d[5]) s->network_configuration=d[5];
    if (++s->index < s->count) submit(s, 9, false);
    else {
        s->stopped = true; s->complete=true;
        ESP_LOGI(TAG, "COMPLETE addr=%u configs=%u; active configuration unchanged", address, s->count);
    }
}
bool usb_config_scan_release(unsigned address)
{
    if (address >= 128) return false;
    scan_t *s = &scans[address];
    s->stopped = true;
    if (s->busy) return false;
    if (s->transfer) usb_host_transfer_free(s->transfer);
    memset(s, 0, sizeof(*s));
    return true;
}

bool usb_config_scan_result(unsigned address,uint8_t *value) {
    if(address>=128 || !value || !scans[address].complete) return false;
    *value=scans[address].network_configuration; return true;
}
bool usb_config_scan_request_mode(unsigned address) {
    if(address>=128) return false;
    scan_t *s=&scans[address];
    if(!s->complete || s->busy || s->mode || !s->transfer) return false;
    usb_transfer_t *t=s->transfer;
    memset(t->data_buffer,0,CAPACITY+8);
    usb_setup_packet_t *setup=(usb_setup_packet_t *)t->data_buffer;
    setup->bmRequestType=0xc0; setup->bRequest=0x52; setup->wIndex=4; setup->wLength=1;
    t->num_bytes=9; s->done=false; s->mode=true;
    esp_err_t rc=usb_host_transfer_submit_control(s->client,t);
    s->busy=rc==ESP_OK; s->deadline=esp_timer_get_time()+3000000;
    ESP_LOGI(TAG,"CarPlay mode C0/52 value=0 index=4 length=1 submit=%s",esp_err_to_name(rc));
    return rc==ESP_OK;
}
