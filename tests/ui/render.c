#include "app_ui.h"
#include "pairing_reset.h"
#include "bsp/m5stack_tab5.h"
#include "carplay_video_profile.h"
#include "driver/ppa.h"
#include "esp_lcd_mipi_dsi.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t frame[1280*720],draw[1280*64],panel_a[1280*720],panel_b[1280*720];
static lv_indev_t *input;
static lv_display_t *host_display;
static bool reset_ready,settings_requested;
static tab5_pair_reset_state reset_state;
static unsigned requests;
static int64_t clock_us;
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *p) {
    for(int y=area->y1;y<=area->y2;++y) {
        memcpy(frame+y*1280+area->x1,p,(area->x2-area->x1+1)*2);
        p+=(area->x2-area->x1+1)*2;
    }
    lv_display_flush_ready(d);
}
static void read_input(lv_indev_t *d,lv_indev_data_t *data) { (void)d; data->state=LV_INDEV_STATE_RELEASED; }
lv_display_t *__wrap_lvgl_port_add_disp_dsi(const lvgl_port_display_cfg_t *,const lvgl_port_display_dsi_cfg_t *);
lv_display_t *__real_lvgl_port_add_disp_dsi(const lvgl_port_display_cfg_t *c,const lvgl_port_display_dsi_cfg_t *s) {
    (void)c; (void)s; return host_display;
}
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg) {
    (void)cfg; lv_init(); host_display=lv_display_create(1280,720);
    lv_display_set_buffers(host_display,draw,NULL,sizeof(draw),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(host_display,flush);
    input=lv_indev_create(); lv_indev_set_type(input,LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(input,read_input);
    lvgl_port_display_cfg_t c={.panel_handle=(void *)1};
    return __wrap_lvgl_port_add_disp_dsi(&c,NULL);
}
lv_indev_t *bsp_display_get_input_dev(void) { return input; }
bool bsp_display_lock(int timeout) { (void)timeout; return true; }
void bsp_display_unlock(void) {}
void bsp_display_rotate(lv_display_t *d,lv_display_rotation_t rotation) { (void)d;(void)rotation; }
esp_err_t bsp_display_brightness_set(int level) { (void)level;return ESP_OK; }
esp_err_t esp_lcd_dpi_panel_get_frame_buffer(esp_lcd_panel_handle_t p,int n,void **a,void **b) {
    assert(p && n==2); *a=panel_a; *b=panel_b; return ESP_OK;
}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t p,int x,int y,int w,int h,const void *b) {
    (void)p;(void)x;(void)y;(void)w;(void)h;(void)b;return ESP_OK;
}
esp_err_t ppa_register_client(const ppa_client_config_t *c,ppa_client_handle_t *h) { (void)c;*h=(void *)1;return ESP_OK; }
esp_err_t ppa_do_scale_rotate_mirror(ppa_client_handle_t h,const ppa_srm_oper_config_t *op) { (void)h;(void)op;return ESP_OK; }
static const carplay_video_profile_t profiles[]={{.width=1024,.height=192,.aspect_width=16,.aspect_height=9},
    {.width=890,.height=240,.aspect_width=16,.aspect_height=9},{.width=1024,.height=360,.aspect_width=16,.aspect_height=9},
    {.width=1024,.height=576,.aspect_width=16,.aspect_height=9}};
static unsigned profile_selected=1;
static int sharpening=-1,mic_level=-1;
const carplay_video_profile_t *carplay_video_profile_current(void) { return &profiles[1]; }
unsigned carplay_video_profile_count(void) { return 4; }
const carplay_video_profile_t *carplay_video_profile_at(unsigned i) { return i<4?&profiles[i]:NULL; }
esp_err_t carplay_video_profile_select(unsigned i) { profile_selected=i; return ESP_OK; }
bool carplay_video_profile_did_fallback(void) { return false; }
void tab5_video_set_sharpening(unsigned s) { sharpening=(int)s; }
static int video_paused;
void tab5_video_pause(bool p) { video_paused=p; }
void tab5_mic_set_level(int p) { mic_level=p; }
const char *esp_err_to_name(esp_err_t e) { (void)e;return "stub"; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h) { (void)ns;(void)mode;(void)h;return ESP_FAIL; }
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *v) { (void)h;(void)key;(void)v;return ESP_FAIL; }
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t v) { (void)h;(void)key;(void)v;return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h;return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
void tab5_audio_set_volume(int v) { (void)v; }
bool airplay_host_ui_take(void) { bool v=settings_requested;settings_requested=false;return v; }
void airplay_control_port_request_main_screen_keyframe(void) {}
static int right_hand_calls,right_hand_value=-1;
void airplay_software_provider_set_right_hand_drive(bool right) { ++right_hand_calls; right_hand_value=right; }
int64_t esp_timer_get_time(void) { return clock_us; }
bool tab5_pair_reset_ready(void) { return reset_ready; }
tab5_pair_reset_state tab5_pair_reset_status(void) { return reset_state; }
bool tab5_pair_reset_request(void) {
    if(!reset_ready || reset_state==TAB5_PAIR_REQUESTED || reset_state==TAB5_PAIR_CLEARING) return false;
    ++requests;reset_state=TAB5_PAIR_REQUESTED;return true;
}
static void tick(unsigned ms) { clock_us+=(int64_t)ms*1000;lv_tick_inc(ms);lv_timer_handler(); }
static void save(const char *path) {
    lv_refr_now(NULL); FILE *f=fopen(path,"wb"); assert(f);
    fprintf(f,"P6\n1280 720\n255\n");
    for(unsigned i=0;i<1280*720;++i) {
        uint16_t c=frame[i]; uint8_t rgb[]={((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};
        fwrite(rgb,1,3,f);
    }
    fclose(f);
}
static lv_obj_t *find_text(lv_obj_t *root,const char *text) {
    if(lv_obj_check_type(root,&lv_label_class) && !strcmp(lv_label_get_text(root),text)) return root;
    for(unsigned i=0;i<lv_obj_get_child_count(root);++i) {
        lv_obj_t *r=find_text(lv_obj_get_child(root,i),text); if(r)return r;
    }
    return NULL;
}
static lv_obj_t *button(lv_obj_t *root,const char *text) {
    lv_obj_t *label=find_text(root,text);assert(label);return lv_obj_get_parent(label);
}
static void click(lv_obj_t *b) { assert(!lv_obj_has_state(b,LV_STATE_DISABLED));lv_obj_send_event(b,LV_EVENT_CLICKED,NULL);tick(120); }
static lv_obj_t *find_matrix(lv_obj_t *parent,const char *label) {
    for(uint32_t i=0;i<lv_obj_get_child_count(parent);++i) {
        lv_obj_t *child=lv_obj_get_child(parent,(int32_t)i);
        if(lv_obj_check_type(child,&lv_buttonmatrix_class))
            for(uint32_t b=0;b<3;++b) if(!strcmp(lv_buttonmatrix_get_button_text(child,b),label)) return child;
        lv_obj_t *found=find_matrix(child,label); if(found) return found;
    }
    return NULL;
}
static lv_obj_t *find_switch(lv_obj_t *parent) {
    for(uint32_t i=0;i<lv_obj_get_child_count(parent);++i) {
        lv_obj_t *child=lv_obj_get_child(parent,(int32_t)i);
        if(lv_obj_check_type(child,&lv_switch_class)) return child;
        lv_obj_t *found=find_switch(child); if(found) return found;
    }
    return NULL;
}
int main(void) {
    assert(tab5_ui_init()==ESP_OK);tick(120);
    assert(right_hand_calls==1 && right_hand_value==0); /* Default left-hand drive applied at start. */
    assert(sharpening==128 && mic_level==72);            /* Defaults: normal sharpening, the old 30 dB. */
    lv_obj_t *screen=lv_screen_active(),*entry=button(screen,"Clear pairing");
    save("artifacts/ui-disconnected.ppm");
    click(entry);
    lv_obj_t *dialog=lv_obj_get_parent(find_text(screen,"Clear phone pairing?"));
    lv_obj_t *overlay=lv_obj_get_parent(dialog),*confirm=button(dialog,"Clear pairing"),*cancel=button(dialog,"Cancel");
    assert(lv_obj_has_state(confirm,LV_STATE_DISABLED) && !requests);
    click(cancel);assert(lv_obj_has_flag(overlay,LV_OBJ_FLAG_HIDDEN) && !requests);
    tab5_ui_status(CP_TRUST_PENDING,CP_ERR_NONE);click(entry);
    save("artifacts/ui-pairing-unplug.ppm");
    assert(find_text(dialog,"Unplug your iPhone from USB-A first."));
    tab5_ui_status(CP_DRAINING,CP_ERR_NONE);tick(120);
    assert(lv_obj_has_state(confirm,LV_STATE_DISABLED));
    tab5_ui_status(CP_DETACHED,CP_ERR_NONE);reset_ready=true;tick(120);
    assert(!lv_obj_has_state(confirm,LV_STATE_DISABLED));
    lv_obj_update_layout(dialog);
    lv_area_t condition_area,button_area;
    lv_obj_get_coords(find_text(dialog,"Ready to clear. Your other settings are kept."),&condition_area);
    lv_obj_get_coords(confirm,&button_area);assert(condition_area.y2<button_area.y1);
    save("artifacts/ui-pairing-confirm.ppm");
    click(confirm);assert(requests==1 && lv_obj_has_state(entry,LV_STATE_DISABLED));
    reset_state=TAB5_PAIR_CLEARING;tick(5200);
    assert(find_text(screen,"Clearing is taking longer. Please wait..."));
    save("artifacts/ui-pairing-clearing.ppm");
    reset_state=TAB5_PAIR_CLEARED;tick(120);
    assert(find_text(screen,"Pairing cleared"));save("artifacts/ui-pairing-success.ppm");
    click(entry);click(confirm);reset_state=TAB5_PAIR_CLEARED;tick(120); /* Same result twice. */
    assert(!lv_obj_has_state(entry,LV_STATE_DISABLED));
    click(entry);click(confirm);reset_state=TAB5_PAIR_FAILED;tick(120);
    assert(find_text(screen,"Couldn't clear pairing"));save("artifacts/ui-pairing-error.ppm");
    click(entry);click(confirm);reset_state=TAB5_PAIR_EMPTY;tick(120);
    assert(find_text(screen,"No saved phone pairing"));
    tab5_ui_status(CP_FAILED,CP_ERR_STORAGE);save("artifacts/ui-pairing-storage.ppm");
    assert(!lv_obj_has_state(entry,LV_STATE_DISABLED));
    tab5_ui_status(CP_AUTHENTICATING,CP_ERR_NONE);assert(lv_obj_has_state(entry,LV_STATE_DISABLED));
    tab5_ui_status(CP_DETACHED,CP_ERR_NONE);click(entry);
    tab5_ui_status(CP_STREAMING,CP_ERR_NONE);
    assert(lv_obj_has_flag(overlay,LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(entry,LV_OBJ_FLAG_HIDDEN));
    /* Settings page from CarPlay: the drive-side switch reaches the provider. */
    settings_requested=true;tick(200);
    assert(find_text(screen,"Right-hand drive"));
    assert(video_paused);                                   /* Video waits behind Settings. */
    lv_obj_t *toggle=find_switch(screen);assert(toggle);
    lv_obj_add_state(toggle,LV_STATE_CHECKED);lv_obj_send_event(toggle,LV_EVENT_VALUE_CHANGED,NULL);tick(120);
    assert(right_hand_value==1);
    /* Picture and sharpening choices reach their owners. */
    lv_obj_t *matrix=find_matrix(screen,"Sharpest");assert(matrix);
    lv_buttonmatrix_set_selected_button(matrix,2);lv_obj_send_event(matrix,LV_EVENT_VALUE_CHANGED,NULL);
    assert(profile_selected!=3);                            /* Saved when Settings closes. */
    matrix=find_matrix(screen,"Strong");assert(matrix);
    lv_buttonmatrix_set_selected_button(matrix,2);lv_obj_send_event(matrix,LV_EVENT_VALUE_CHANGED,NULL);
    assert(sharpening==255);
    lv_obj_scroll_to_y(lv_obj_get_parent(matrix),10000,LV_ANIM_OFF);tick(120);
    save("artifacts/ui-settings-lower.ppm");
    lv_obj_scroll_to_y(lv_obj_get_parent(matrix),0,LV_ANIM_OFF);tick(120);
    tab5_ui_battery(72,true,false);tick(120);
    assert(find_text(screen,"Charging 72%"));
    save("artifacts/ui-settings.ppm");
    click(button(screen,LV_SYMBOL_LEFT "  Back to CarPlay"));
    assert(!video_paused);
    assert(profile_selected==3);
    /* Low battery: the waiting screen says so, and a small icon is drawn over the
     * video on the side away from the dock (left, as right-hand drive is now on). */
    tab5_ui_battery(9,false,true);tick(120);
    assert(find_text(screen,"v1.0.0  |  Battery 9%"));
    static uint8_t packed[890*240*3/2];
    assert(tab5_ui_frame(packed,890,240));
    assert(panel_b[(1280-1-32)*720+32]==0xf904);            /* Icon outline, top-left. */
    assert(panel_b[(1280-1-1200)*720+32]!=0xf904);          /* Not on the right. */
    /* Power-off notice: video stops, status updates no longer replace the text. */
    tab5_ui_power_off_notice();tick(120);
    assert(find_text(screen,"Battery empty"));
    tab5_ui_status(CP_STREAMING,CP_ERR_NONE);
    assert(find_text(screen,"Battery empty"));
    save("artifacts/ui-power-off.ppm");
    puts("Production LVGL UI: cancel, unplug/drain gate, repeat, progress, errors and streaming visibility, drive side, picture, sharpening, microphone, battery and power-off passed");
}
