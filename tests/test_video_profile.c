/* Boot-by-boot trial rules for a new CarPlay video profile. */
#include <assert.h>
#include <string.h>
#include "../components/airplay_control/carplay_video_profile.c"
unsigned test_requested_height;
static struct { uint16_t profile_h,wanted; uint8_t probe,fallback; bool h,w,p,f; } flash,ram;
esp_err_t nvs_open(const char *space,int mode,nvs_handle_t *handle) {
    (void)mode; assert(!strcmp(space,"tab5_video")); *handle=1; ram=flash; return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v) {
    (void)h; if(!strcmp(k,"probe") && ram.p) *v=ram.probe; else if(!strcmp(k,"fallback") && ram.f) *v=ram.fallback; else return 1;
    return ESP_OK;
}
esp_err_t nvs_get_u16(nvs_handle_t h,const char *k,uint16_t *v) {
    (void)h; if(!strcmp(k,"profile_h") && ram.h) *v=ram.profile_h; else if(!strcmp(k,"wanted") && ram.w) *v=ram.wanted; else return 1;
    return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v) {
    (void)h; if(!strcmp(k,"probe")) { ram.probe=v; ram.p=true; } else { assert(!strcmp(k,"fallback")); ram.fallback=v; ram.f=true; }
    return ESP_OK;
}
esp_err_t nvs_set_u16(nvs_handle_t h,const char *k,uint16_t v) {
    (void)h; if(!strcmp(k,"profile_h")) { ram.profile_h=v; ram.h=true; } else { assert(!strcmp(k,"wanted")); ram.wanted=v; ram.w=true; }
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; flash=ram; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
static unsigned boot(unsigned requested) {
    test_requested_height=requested; initialized=false; active=0; fell_back=false;
    return carplay_video_profile_current()->height;
}
int main(void) {
    /* A board already running the old firmware: 192 stored, no "wanted" key. */
    flash.profile_h=192; flash.h=true; flash.probe=0; flash.p=true;
    assert(boot(192)==192 && !carplay_video_profile_did_fallback());
    /* Boots without a phone never count against the trial. */
    assert(boot(288)==288); assert(boot(288)==288); assert(boot(288)==288);
    /* A crash, or an end before the verdict, returns to the safe profile. */
    assert(carplay_video_profile_begin());
    assert(boot(288)==192 && carplay_video_profile_did_fallback());
    /* Same request again: no second trial. */
    assert(boot(288)==192 && carplay_video_profile_did_fallback());
    /* A new request is a new trial, and confirming it makes it permanent. */
    assert(boot(384)==384 && !carplay_video_profile_did_fallback());
    assert(carplay_video_profile_begin()); carplay_video_profile_confirm();
    /* A settled profile is not a trial, so it is never judged again. */
    assert(boot(384)==384 && !carplay_video_profile_begin() && boot(384)==384);
    /* Too slow: the running session keeps its profile, the next boot does not. */
    assert(boot(288)==288);
    carplay_video_profile_begin(); carplay_video_profile_fallback();
    assert(boot(288)==192 && carplay_video_profile_did_fallback());
    /* An unknown height falls back instead of advertising nothing. */
    assert(boot(999)==192 && carplay_video_profile_did_fallback());
    /* Touch keeps the layout height when the video is squeezed. */
    assert(boot(192)==192 && carplay_video_profile_touch_height()==576);
    assert(boot(288)==288 && carplay_video_profile_touch_height()==288);
    /* A confirmed safe profile is not experimental. */
    assert(boot(192)==192 && !carplay_video_profile_did_fallback());
    return 0;
}
