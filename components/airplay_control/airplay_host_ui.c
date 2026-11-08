/* SPDX-License-Identifier: GPL-3.0-or-later; requestUI follows DiPlay. */
#include "airplay_host_ui.h"
#include "airplay_bplist.h"
#include <string.h>
#include "esp_log.h"
static bool pending;
static void log_numeric(const airplay_bplist_doc_t *doc,airplay_bplist_object_t object,const char *key)
{
    airplay_bplist_object_t value; uint64_t number;
    if(airplay_bplist_dict_get(doc,object,key,&value)==AIRPLAY_BPLIST_OK &&
       airplay_bplist_get_uint(doc,value,&number)==AIRPLAY_BPLIST_OK)
        ESP_LOGI("AUDIO_ROUTE","%s=%llu",key,(unsigned long long)number);
}
static void log_modes(const airplay_bplist_doc_t *doc,airplay_bplist_object_t params)
{
    const char *fields[]={"mainAudio","screen","speech","phoneCall","turnByTurn"};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);++i) log_numeric(doc,params,fields[i]);
    airplay_bplist_object_t resources;airplay_bplist_type_t type;uint64_t count;
    if(airplay_bplist_dict_get(doc,params,"resources",&resources)!=AIRPLAY_BPLIST_OK ||
       airplay_bplist_get_type(doc,resources,&type,&count)!=AIRPLAY_BPLIST_OK ||
       type!=AIRPLAY_BPLIST_ARRAY) return;
    for(uint64_t i=0;i<count && i<8;++i) {
        airplay_bplist_object_t entry;
        if(airplay_bplist_array_get(doc,resources,i,&entry)!=AIRPLAY_BPLIST_OK) break;
        ESP_LOGI("AUDIO_ROUTE","resource_index=%u",(unsigned)i);
        const char *keys[]={"resourceID","entity","owner","transferType","transferPriority","takeConstraint","borrowConstraint"};
        for(unsigned k=0;k<sizeof(keys)/sizeof(keys[0]);++k) log_numeric(doc,entry,keys[k]);
    }
}
bool airplay_host_ui_handle(const airplay_rtsp_request_t *request) {
    if (!request || strcmp(request->method,"POST") || strcmp(request->path,"/command")) return false;
    airplay_bplist_doc_t doc;
    airplay_bplist_object_t root,type,params,url;
    char value[128]; size_t value_len;
    if (airplay_bplist_open(request->body,request->body_len,&doc)!=AIRPLAY_BPLIST_OK ||
        airplay_bplist_root(&doc,&root)!=AIRPLAY_BPLIST_OK ||
        airplay_bplist_dict_get(&doc,root,"type",&type)!=AIRPLAY_BPLIST_OK ||
        airplay_bplist_get_string(&doc,type,value,sizeof(value),&value_len)!=AIRPLAY_BPLIST_OK) return false;
    /* Only printable bounded command names and allowlisted numeric resource
     * fields are logged. Never log arbitrary command data or media payloads. */
    for(size_t i=0;i<value_len;++i) if((unsigned char)value[i]<32 || (unsigned char)value[i]>126) value[i]='?';
    ESP_LOGI("AUDIO_ROUTE","command=%s",value);
    if(!strcmp(value,"modesChanged")) {
        if(airplay_bplist_dict_get(&doc,root,"params",&params)==AIRPLAY_BPLIST_OK) log_modes(&doc,params);
    }
    if(strcmp(value,"requestUI")) return false;
    airplay_bplist_result_t result=airplay_bplist_dict_get(&doc,root,"params",&params);
    if (result==AIRPLAY_BPLIST_OK) {
        airplay_bplist_type_t kind;
        if (airplay_bplist_get_type(&doc,params,&kind,NULL)!=AIRPLAY_BPLIST_OK || kind!=AIRPLAY_BPLIST_DICTIONARY) return false;
        result=airplay_bplist_dict_get(&doc,params,"url",&url);
        if (result==AIRPLAY_BPLIST_OK) {
            if (airplay_bplist_get_string(&doc,url,value,sizeof(value),&value_len)!=AIRPLAY_BPLIST_OK ||
                strcmp(value,"videoplayback:")==0) return false;
        } else if (result!=AIRPLAY_BPLIST_NOT_FOUND) return false;
    } else if (result!=AIRPLAY_BPLIST_NOT_FOUND) return false;
    __atomic_store_n(&pending,true,__ATOMIC_RELEASE);
    return true;
}
bool airplay_host_ui_take(void) { return __atomic_exchange_n(&pending,false,__ATOMIC_ACQ_REL); }
