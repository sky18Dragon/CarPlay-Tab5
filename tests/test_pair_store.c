// SPDX-License-Identifier: GPL-3.0-or-later
/* Fake NVS with the real partition's update rule: a blob update needs room for
 * the old and the new version at the same time. Sizes are in 32-byte entries. */
#include "pair_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SLOTS 24
#define CAPACITY 630          /* 24 KiB: six pages of 126 entries, one reserved. */
#define RECORD_BYTES 6768     /* sizeof(PairRecord) in lockdown_client.cpp. */
#define OTHER_NAMESPACES 54   /* Entries used on the device with no pairing. */
typedef struct { bool used, blob; char key[16]; unsigned entries; uint8_t value, first; } slot_t;
static slot_t slots[SLOTS];
static int opened, closed, commits, fail_erase;
struct fake_iterator { int index; };
static unsigned used_entries(void)
{
    unsigned total=OTHER_NAMESPACES;
    for(int i=0;i<SLOTS;++i) if(slots[i].used) total+=slots[i].entries;
    return total;
}
static slot_t *find(const char *key)
{
    for(int i=0;i<SLOTS;++i) if(slots[i].used && !strcmp(slots[i].key,key)) return &slots[i];
    return NULL;
}
static esp_err_t put(const char *key,bool blob,unsigned entries,uint8_t value,uint8_t first)
{
    assert(strlen(key)<16);
    if(used_entries()+entries>CAPACITY) return ESP_ERR_NVS_NOT_ENOUGH_SPACE; /* Old copy still counted. */
    slot_t *s=find(key);
    for(int i=0;!s && i<SLOTS;++i) if(!slots[i].used) s=&slots[i];
    assert(s);
    s->used=true; s->blob=blob; s->entries=entries; s->value=value; s->first=first;
    strcpy(s->key,key);
    return ESP_OK;
}
const char *esp_err_to_name(esp_err_t e) { return e==ESP_OK?"OK":"error"; }
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h)
{ (void)mode; assert(!strcmp(ns,"tab5_pair")); ++opened; *h=7; return ESP_OK; }
void nvs_close(nvs_handle_t h) { assert(h==7); ++closed; }
esp_err_t nvs_commit(nvs_handle_t h) { assert(h==7); ++commits; return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t n)
{ assert(h==7); return put(key,true,(unsigned)((n+31)/32)+2,0,*(const uint8_t *)data); }
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t v) { assert(h==7); return put(key,false,1,v,0); }
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *v)
{
    assert(h==7);
    slot_t *s=find(key);
    if(!s || s->blob) return ESP_ERR_NVS_NOT_FOUND;
    *v=s->value; return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key)
{
    assert(h==7);
    if(fail_erase) return ESP_FAIL;
    slot_t *s=find(key);
    if(!s) return ESP_ERR_NVS_NOT_FOUND;
    s->used=false; return ESP_OK;
}
static esp_err_t advance(nvs_iterator_t *it)
{
    while(++(*it)->index<SLOTS) if(slots[(*it)->index].used) return ESP_OK;
    free(*it); *it=NULL;
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_entry_find(const char *part,const char *ns,int type,nvs_iterator_t *it)
{
    assert(!strcmp(part,"nvs") && !strcmp(ns,"tab5_pair") && type==NVS_TYPE_ANY);
    *it=malloc(sizeof(**it)); (*it)->index=-1;
    return advance(it);
}
esp_err_t nvs_entry_next(nvs_iterator_t *it) { return advance(it); }
esp_err_t nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t *info)
{ memset(info,0,sizeof(*info)); strcpy(info->key,slots[it->index].key); return ESP_OK; }
void nvs_release_iterator(nvs_iterator_t it) { free(it); }

static uint8_t record[RECORD_BYTES];
static esp_err_t save(const char *key,uint8_t marker,unsigned *evicted)
{ record[0]=marker; return tab5_pair_store_save(key,record,sizeof(record),evicted); }
int main(void)
{
    const char *a="p00000000000001",*b="p00000000000002",*c="p00000000000003";
    unsigned evicted=9;
    /* First phone: identity, then Trust. */
    assert(save(a,1,&evicted)==ESP_OK && evicted==0);
    assert(!tab5_pair_store_trusted(a,false) && tab5_pair_store_trusted(a,true)); /* v1.0.0 fallback. */
    assert(tab5_pair_store_set_trusted(a,true)==ESP_OK && tab5_pair_store_trusted(a,false));
    /* Second phone. v1.0.0 stored its identity and then failed here, because
     * confirming Trust rewrote the whole blob beside the first phone's record. */
    assert(save(b,2,&evicted)==ESP_OK && evicted==0);
    assert(nvs_set_blob(7,b,record,sizeof(record))==ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    assert(tab5_pair_store_set_trusted(b,true)==ESP_OK);
    assert(tab5_pair_store_trusted(b,false) && tab5_pair_store_trusted(a,false) && find(a) && find(b));
    /* Third phone does not fit: the other phones are evicted, not this one. */
    assert(save(c,3,&evicted)==ESP_OK && evicted==4);
    assert(find(c) && find(c)->first==3 && !find(a) && !find(b) && !find("t00000000000001") && !find("t00000000000002"));
    assert(!tab5_pair_store_trusted(c,false));
    assert(tab5_pair_store_set_trusted(c,true)==ESP_OK && tab5_pair_store_trusted(c,false));
    /* A legacy "record" entry and an evicted phone's stale trust are removed too. */
    memset(slots,0,sizeof(slots));
    assert(put("record",true,214,0,0)==ESP_OK && save(a,1,&evicted)==ESP_OK);
    assert(tab5_pair_store_set_trusted(a,true)==ESP_OK);
    assert(save(b,2,&evicted)==ESP_OK && evicted==3 && find(b) && !find("record") && !find(a));
    assert(!tab5_pair_store_trusted(a,false)); /* Returning phone starts untrusted. */
    /* Eviction failure is reported; the caller sees the storage error. */
    memset(slots,0,sizeof(slots));
    assert(save(a,1,&evicted)==ESP_OK && save(b,2,&evicted)==ESP_OK);
    fail_erase=1;
    assert(save(c,3,&evicted)==ESP_FAIL && evicted==0 && find(a) && find(b) && !find(c));
    fail_erase=0;
    /* Only this phone stored and still no room: no eviction possible. */
    memset(slots,0,sizeof(slots));
    assert(put(a,true,600,0,0)==ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    assert(put(a,true,500,0,7)==ESP_OK);
    assert(save(a,1,&evicted)==ESP_ERR_NVS_NOT_ENOUGH_SPACE && evicted==0 && find(a)->first==7);
    /* Invalid keys never reach storage. */
    assert(tab5_pair_store_save("record",record,sizeof(record),NULL)==ESP_ERR_INVALID_ARG);
    assert(tab5_pair_store_save(a,NULL,0,NULL)==ESP_ERR_INVALID_ARG);
    assert(tab5_pair_store_set_trusted("x",true)==ESP_ERR_INVALID_ARG);
    assert(tab5_pair_store_trusted(NULL,true));
    assert(opened==closed && commits>0);
    puts("pair store trust split, eviction, legacy and storage errors: OK");
}
