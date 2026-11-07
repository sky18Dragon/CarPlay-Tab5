/* SPDX-License-Identifier: GPL-3.0-or-later; requestUI follows DiPlay. */
#include "airplay_host_ui.h"
#include "airplay_bplist.h"
#include <string.h>
static bool pending;
bool airplay_host_ui_handle(const airplay_rtsp_request_t *request) {
    if (!request || strcmp(request->method,"POST") || strcmp(request->path,"/command")) return false;
    airplay_bplist_doc_t doc;
    airplay_bplist_object_t root,type,params,url;
    char value[128]; size_t value_len;
    if (airplay_bplist_open(request->body,request->body_len,&doc)!=AIRPLAY_BPLIST_OK ||
        airplay_bplist_root(&doc,&root)!=AIRPLAY_BPLIST_OK ||
        airplay_bplist_dict_get(&doc,root,"type",&type)!=AIRPLAY_BPLIST_OK ||
        airplay_bplist_get_string(&doc,type,value,sizeof(value),&value_len)!=AIRPLAY_BPLIST_OK ||
        strcmp(value,"requestUI")) return false;
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
