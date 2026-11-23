// SPDX-License-Identifier: GPL-3.0-or-later
#include "pair_store.h"
#include <stdint.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG="PAIR_STORE";
static const char *PAIR_NAMESPACE="tab5_pair";
#define EVICT_BATCH 8

static bool trust_key(const char *key, char out[TAB5_PAIR_KEY_SIZE])
{
    size_t n=key?strlen(key):0;
    if(n<2 || n>=TAB5_PAIR_KEY_SIZE || key[0]!='p') return false;
    memcpy(out,key,n+1); out[0]='t';
    return true;
}

/* Removes every entry that does not belong to the phone being saved. Names are
 * collected first: an iterator is not valid across a modification. */
static esp_err_t evict_others(nvs_handle_t h, const char *key, const char *trust, unsigned *evicted)
{
    for(;;) {
        char names[EVICT_BATCH][TAB5_PAIR_KEY_SIZE]; unsigned count=0;
        nvs_iterator_t it=NULL;
        esp_err_t rc=nvs_entry_find("nvs",PAIR_NAMESPACE,NVS_TYPE_ANY,&it);
        while(rc==ESP_OK && count<EVICT_BATCH) {
            nvs_entry_info_t info;
            rc=nvs_entry_info(it,&info);
            if(rc!=ESP_OK) break;
            if(strcmp(info.key,key) && strcmp(info.key,trust)) {
                /* NVS keys are at most 15 characters in a 16-byte field. */
                _Static_assert(sizeof(info.key)==TAB5_PAIR_KEY_SIZE,"NVS key size");
                memcpy(names[count],info.key,TAB5_PAIR_KEY_SIZE);
                names[count++][TAB5_PAIR_KEY_SIZE-1]=0;
            }
            rc=nvs_entry_next(&it);
        }
        nvs_release_iterator(it);
        if(rc!=ESP_OK && rc!=ESP_ERR_NVS_NOT_FOUND) return rc;
        if(!count) return ESP_OK;
        for(unsigned i=0;i<count;++i) {
            rc=nvs_erase_key(h,names[i]);
            if(rc!=ESP_OK) return rc;
            ++*evicted;
        }
        rc=nvs_commit(h);
        if(rc!=ESP_OK) return rc;
    }
}

esp_err_t tab5_pair_store_save(const char *key, const void *record, size_t size, unsigned *evicted)
{
    char trust[TAB5_PAIR_KEY_SIZE]; unsigned removed=0;
    if(evicted) *evicted=0;
    if(!record || !size || !trust_key(key,trust)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t rc=nvs_open(PAIR_NAMESPACE,NVS_READWRITE,&h);
    if(rc!=ESP_OK) return rc;
    rc=nvs_set_blob(h,key,record,size);
    if(rc==ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
        ESP_LOGW(TAG,"No room for this phone's record; removing records of other phones");
        rc=evict_others(h,key,trust,&removed);
        if(rc==ESP_OK && !removed) rc=ESP_ERR_NVS_NOT_ENOUGH_SPACE; /* Nothing to give up. */
        if(rc==ESP_OK) rc=nvs_set_blob(h,key,record,size);
        ESP_LOGW(TAG,"Removed %u other entries; retry=%s",removed,esp_err_to_name(rc));
    }
    if(rc==ESP_OK) rc=nvs_commit(h);
    nvs_close(h);
    if(evicted) *evicted=removed;
    return rc;
}

esp_err_t tab5_pair_store_set_trusted(const char *key, bool trusted)
{
    char trust[TAB5_PAIR_KEY_SIZE];
    if(!trust_key(key,trust)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t rc=nvs_open(PAIR_NAMESPACE,NVS_READWRITE,&h);
    if(rc!=ESP_OK) return rc;
    rc=nvs_set_u8(h,trust,trusted?1:0);
    if(rc==ESP_OK) rc=nvs_commit(h);
    nvs_close(h);
    return rc;
}

bool tab5_pair_store_trusted(const char *key, bool fallback)
{
    char trust[TAB5_PAIR_KEY_SIZE];
    if(!trust_key(key,trust)) return fallback;
    nvs_handle_t h;
    if(nvs_open(PAIR_NAMESPACE,NVS_READONLY,&h)!=ESP_OK) return fallback;
    uint8_t value=0;
    esp_err_t rc=nvs_get_u8(h,trust,&value);
    nvs_close(h);
    return rc==ESP_OK?value==1:fallback;
}
