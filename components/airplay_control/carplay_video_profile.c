/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "carplay_video_profile.h"
#include "video_profiles_generated.h"
#include "nvs.h"
#include "esp_log.h"
#include "sdkconfig.h"
static unsigned active=0;
static bool initialized,fell_back;
unsigned carplay_video_profile_count(void) { return sizeof(video_profiles)/sizeof(video_profiles[0]); }
/* "wanted" remembers the requested height, so a build asking for a new height
 * starts one trial, and one that already fell back does not try again. Probe:
 * 0 settled, 1 trial pending, 2 trial running. Probe 2 at boot means the last
 * trial crashed or ended before it was judged, so the safe profile is used. */
static void load(void) {
    if (initialized) return;
    unsigned index=0;
    uint8_t probe=0,fallback=0; uint16_t height=192,wanted=0; nvs_handle_t n;
    if (nvs_open("tab5_video",NVS_READWRITE,&n)==ESP_OK) {
        nvs_get_u16(n,"profile_h",&height);
        nvs_get_u8(n,"probe",&probe); nvs_get_u8(n,"fallback",&fallback);
        nvs_get_u16(n,"wanted",&wanted);
        const bool request=wanted!=CONFIG_TAB5_VIDEO_PROFILE_HEIGHT;
        if (request) { height=CONFIG_TAB5_VIDEO_PROFILE_HEIGHT; probe=1; fallback=0; }
        bool found=false;
        for (unsigned i=0;i<carplay_video_profile_count();++i) if (video_profiles[i].height==height) { index=i; found=true; }
        if (!found || (video_profiles[index].experimental && probe==2)) { index=0; fallback=1; probe=0; }
        if (!video_profiles[index].experimental) probe=0;
        nvs_set_u16(n,"wanted",CONFIG_TAB5_VIDEO_PROFILE_HEIGHT);
        nvs_set_u16(n,"profile_h",video_profiles[index].height);
        nvs_set_u8(n,"probe",probe);
        nvs_set_u8(n,"fallback",fallback); nvs_commit(n); nvs_close(n);
    }
    active=index; fell_back=fallback; initialized=true;
    ESP_LOGI("VIDEO_PROFILE","active=%s max_fps=%u probe=%u fallback=%u",video_profiles[active].label,video_profiles[active].fps,probe,fell_back);
}
unsigned carplay_video_profile_index(void) { load(); return active; }
const carplay_video_profile_t *carplay_video_profile_at(unsigned index) {
    return index<carplay_video_profile_count()?&video_profiles[index]:NULL;
}
const carplay_video_profile_t *carplay_video_profile_current(void) { return carplay_video_profile_at(carplay_video_profile_index()); }
unsigned carplay_video_profile_touch_height(void) {
    const carplay_video_profile_t *p=carplay_video_profile_current();
    return p->width*p->aspect_height/p->aspect_width;
}
esp_err_t carplay_video_profile_select(unsigned index) {
    if (index>=carplay_video_profile_count()) return ESP_ERR_INVALID_ARG;
    nvs_handle_t n; esp_err_t err=nvs_open("tab5_video",NVS_READWRITE,&n);
    if (err!=ESP_OK) return err;
    err=nvs_set_u16(n,"profile_h",video_profiles[index].height);
    if (err==ESP_OK) err=nvs_set_u8(n,"probe",video_profiles[index].experimental?1:0);
    if (err==ESP_OK) err=nvs_set_u8(n,"fallback",0);
    if (err==ESP_OK) err=nvs_commit(n);
    nvs_close(n); return err;
}
bool carplay_video_profile_did_fallback(void) { load(); return fell_back; }
void carplay_video_profile_fallback(void) {
    carplay_video_profile_select(0);
    nvs_handle_t n;
    if (nvs_open("tab5_video",NVS_READWRITE,&n)==ESP_OK) { nvs_set_u8(n,"fallback",1); nvs_commit(n); nvs_close(n); }
}
bool carplay_video_profile_begin(void) {
    if (!carplay_video_profile_current()->experimental) return false;
    nvs_handle_t n; bool trial=false;
    if (nvs_open("tab5_video",NVS_READWRITE,&n)==ESP_OK) {
        uint8_t probe=0; nvs_get_u8(n,"probe",&probe);
        trial=probe==1;
        if (trial) { nvs_set_u8(n,"probe",2); nvs_commit(n); }
        nvs_close(n);
    }
    return trial;
}
void carplay_video_profile_confirm(void) {
    if (!carplay_video_profile_current()->experimental) return;
    nvs_handle_t n;
    if (nvs_open("tab5_video",NVS_READWRITE,&n)==ESP_OK) { nvs_set_u8(n,"probe",0); nvs_commit(n); nvs_close(n); }
}
