#include "pairing_reset.h"
#include <stdatomic.h>
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"

static atomic_bool ready;
static atomic_int state;
static const char *TAG="PAIR_RESET";
static const char *PAIR_NAMESPACE="tab5_pair";

bool tab5_pair_reset_ready(void) { return atomic_load(&ready); }
tab5_pair_reset_state tab5_pair_reset_status(void) { return atomic_load(&state); }
bool tab5_pair_reset_request(void)
{
    if(!atomic_load(&ready)) return false;
    int previous=atomic_load(&state);
    do {
        if(previous==TAB5_PAIR_REQUESTED || previous==TAB5_PAIR_CLEARING) return false;
    } while(!atomic_compare_exchange_weak(&state,&previous,TAB5_PAIR_REQUESTED));
    return true;
}
static esp_err_t has_entries(bool *present)
{
    nvs_iterator_t it=NULL;
    esp_err_t rc=nvs_entry_find("nvs",PAIR_NAMESPACE,NVS_TYPE_ANY,&it);
    nvs_release_iterator(it);
    *present=rc==ESP_OK;
    return rc==ESP_ERR_NVS_NOT_FOUND?ESP_OK:rc;
}
void tab5_pair_reset_poll(bool quiescent)
{
    atomic_store(&ready,quiescent);
    int expected=TAB5_PAIR_REQUESTED;
    if(!atomic_compare_exchange_strong(&state,&expected,TAB5_PAIR_CLEARING)) return;
    atomic_store(&ready,false);
    /* Recheck on the owner: the phone may have been inserted after the click. */
    if(!quiescent) {
        ESP_LOGW(TAG,"request rejected: USB connection/teardown active");
        atomic_store(&state,TAB5_PAIR_BLOCKED);
        return;
    }
    int64_t start=esp_timer_get_time();
    ESP_LOGI(TAG,"begin namespace=tab5_pair; USB and pairing worker drained");
    bool present=false;
    esp_err_t rc=has_entries(&present);
    const bool empty=rc==ESP_OK && !present;
    if(rc==ESP_OK && present) {
        nvs_handle_t h;
        rc=nvs_open(PAIR_NAMESPACE,NVS_READWRITE,&h);
        if(rc==ESP_OK) {
            rc=nvs_erase_all(h); /* Never erase the shared NVS partition. */
            if(rc==ESP_OK) rc=nvs_commit(h);
            nvs_close(h);
        }
        if(rc==ESP_OK) {
            rc=has_entries(&present);
            if(rc==ESP_OK && present) rc=ESP_FAIL;
        }
    }
    ESP_LOGI(TAG,"end namespace=tab5_pair result=%s empty=%d elapsed_ms=%lld",
             esp_err_to_name(rc),empty,(long long)((esp_timer_get_time()-start)/1000));
    /* Lockdown's record is zeroized before worker_drained becomes true. */
    atomic_store(&state,rc==ESP_OK?(empty?TAB5_PAIR_EMPTY:TAB5_PAIR_CLEARED):TAB5_PAIR_FAILED);
}
