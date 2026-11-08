#include "app_ui.h"
#include "bsp/m5stack_tab5.h"
#include "carplay_video_profile.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "airplay_host_ui.h"
#include "airplay_control_port.h"
#include "audio_output.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>
/* Product UI: night-friendly neutrals, one accent, 8 px spacing.
 * All objects and source pixels are accessed under the LVGL port lock. */
#define UI_BG 0x101820
#define UI_TEXT 0xe8eef3
#define UI_MUTED 0xaab8c5
#define UI_ACCENT 0x70c6ad
#define UI_ERROR 0xffb6a3
#define TOUCH_READ_MS 10
static lv_obj_t *title,*detail,*mode,*brand;
static lv_display_t *display;
/* Video bypasses LVGL: frames are converted at their own size, then PPA scales
 * and rotates them into the panel frame buffer that is not on screen. */
static esp_lcd_panel_handle_t panel;
static ppa_client_handle_t scaler;
static void *framebuffers[2];
static unsigned hidden_buffer=1; /* The DPI driver starts on buffer 0. */
static bool video_visible,simulated;
static bool ready;
static int width,height;
static cp_touch touch;
static tab5_touch_sink sink;
static void *sink_context;
/* Local settings page, opened from the CarPlay "Tab5 设置" icon (requestUI).
 * While it is open, video is not drawn and touches stay on Tab5. */
#define SETTINGS_NAMESPACE "tab5_ui"
#define VOLUME_DEFAULT 30
#define BRIGHTNESS_DEFAULT 40
#define BRIGHTNESS_MIN 10
static lv_obj_t *settings,*volume_value,*brightness_value;
static bool settings_open;
/* The BSP keeps its panel handle private; bsp_display_start() is its only caller. */
lv_display_t *__real_lvgl_port_add_disp_dsi(const lvgl_port_display_cfg_t *,const lvgl_port_display_dsi_cfg_t *);
lv_display_t *__wrap_lvgl_port_add_disp_dsi(const lvgl_port_display_cfg_t *config,const lvgl_port_display_dsi_cfg_t *dsi)
{
    panel=config->panel_handle;
    return __real_lvgl_port_add_disp_dsi(config,dsi);
}
static void deliver(cp_touch_event event) { if(event.kind!=CP_TOUCH_NONE && sink) sink(event,sink_context); }
static int load_setting(const char *key,int fallback,int low,int high)
{
    nvs_handle_t h; uint8_t value;
    if(nvs_open(SETTINGS_NAMESPACE,NVS_READONLY,&h)!=ESP_OK) return fallback;
    esp_err_t e=nvs_get_u8(h,key,&value); nvs_close(h);
    return e==ESP_OK && value>=low && value<=high ? value : fallback;
}
static void save_setting(const char *key,int value)
{
    nvs_handle_t h;
    if(nvs_open(SETTINGS_NAMESPACE,NVS_READWRITE,&h)!=ESP_OK) return;
    if(nvs_set_u8(h,key,(uint8_t)value)==ESP_OK) nvs_commit(h);
    nvs_close(h);
    ESP_LOGI("SETTINGS","saved %s=%d",key,value);
}
static void touch_callback(lv_event_t *event)
{
    lv_indev_t *input=lv_indev_active(); if(!input) return;
    lv_point_t p; lv_indev_get_point(input,&p);
    lv_event_code_t code=lv_event_get_code(event);
    if(settings_open) return;
    if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) deliver(cp_touch_cancel(&touch));
    else if(code==LV_EVENT_PRESSED || code==LV_EVENT_PRESSING)
        deliver(cp_touch_update(&touch,true,p.x,p.y));
}
static lv_obj_t *label(lv_obj_t *screen,const char *text,int y,uint32_t color)
{
    lv_obj_t *obj=lv_label_create(screen); lv_label_set_text(obj,text);
    lv_obj_set_width(obj,width-96); lv_obj_set_pos(obj,48,y);
    lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_obj_set_style_text_font(obj,&lv_font_montserrat_28,0);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_WRAP); return obj;
}
/* The LVGL lock is held for every function below (LVGL callbacks and timers). */
static void show_percent(lv_obj_t *value_label,int percent)
{
    char text[8]; snprintf(text,sizeof(text),"%d%%",percent); lv_label_set_text(value_label,text);
}
static void slider_event(lv_event_t *event)
{
    lv_obj_t *slider=lv_event_get_target(event);
    const bool is_volume=lv_event_get_user_data(event)!=NULL;
    const int value=lv_slider_get_value(slider);
    const lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_VALUE_CHANGED) {
        if(is_volume) { tab5_audio_set_volume(value); show_percent(volume_value,value); }
        else { bsp_display_brightness_set(value); show_percent(brightness_value,value); }
    } else if(code==LV_EVENT_RELEASED) {
        save_setting(is_volume?"volume":"brightness",value);   /* flash once per drag */
    }
}
static void leave_video(void);
static void close_settings(void)
{
    if(!settings_open) return;
    settings_open=false;
    lv_obj_add_flag(settings,LV_OBJ_FLAG_HIDDEN);
    /* CarPlay sends frames only when its screen changes; ask for a full one so
     * the video comes back at once. The next frame re-enters video mode. */
    airplay_control_port_request_main_screen_keyframe();
    ESP_LOGI("SETTINGS","closed");
}
static void back_event(lv_event_t *event)
{
    if(lv_event_get_code(event)==LV_EVENT_CLICKED) close_settings();
}
static void open_settings(void)
{
    if(settings_open) return;
    deliver(cp_touch_cancel(&touch));
    settings_open=true;
    leave_video();
    lv_obj_remove_flag(settings,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(settings);
    lv_obj_invalidate(lv_screen_active());
    ESP_LOGI("SETTINGS","opened from CarPlay");
}
static void settings_timer(lv_timer_t *timer)
{
    (void)timer;
    if(airplay_host_ui_take()) open_settings();
}
static lv_obj_t *settings_row(lv_obj_t *parent,const char *name,int y,int value,int low,lv_obj_t **value_label,bool is_volume)
{
    lv_obj_t *caption=lv_label_create(parent); lv_label_set_text(caption,name);
    lv_obj_set_style_text_color(caption,lv_color_hex(UI_TEXT),0);
    lv_obj_set_style_text_font(caption,&lv_font_montserrat_28,0);
    lv_obj_set_pos(caption,96,y);
    *value_label=lv_label_create(parent);
    lv_obj_set_style_text_color(*value_label,lv_color_hex(UI_MUTED),0);
    lv_obj_set_style_text_font(*value_label,&lv_font_montserrat_28,0);
    lv_obj_align(*value_label,LV_ALIGN_TOP_RIGHT,-96,y);
    show_percent(*value_label,value);
    lv_obj_t *slider=lv_slider_create(parent);
    lv_slider_set_range(slider,low,100); lv_slider_set_value(slider,value,LV_ANIM_OFF);
    lv_obj_set_size(slider,width-192-48,24); lv_obj_set_pos(slider,96+24,y+64);
    lv_obj_set_ext_click_area(slider,32);
    lv_obj_set_style_bg_color(slider,lv_color_hex(UI_ACCENT),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider,lv_color_hex(UI_ACCENT),LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider,12,LV_PART_KNOB);
    lv_obj_add_event_cb(slider,slider_event,LV_EVENT_VALUE_CHANGED,is_volume?(void *)1:NULL);
    lv_obj_add_event_cb(slider,slider_event,LV_EVENT_RELEASED,is_volume?(void *)1:NULL);
    return slider;
}
static void build_settings(lv_obj_t *screen,int volume,int brightness)
{
    settings=lv_obj_create(screen);
    lv_obj_remove_style_all(settings);
    lv_obj_set_size(settings,width,height); lv_obj_set_pos(settings,0,0);
    lv_obj_set_style_bg_color(settings,lv_color_hex(UI_BG),0);
    lv_obj_set_style_bg_opa(settings,LV_OPA_COVER,0);
    lv_obj_add_flag(settings,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(settings,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *heading=lv_label_create(settings); lv_label_set_text(heading,"Tab5 Settings");
    lv_obj_set_style_text_color(heading,lv_color_hex(UI_ACCENT),0);
    lv_obj_set_style_text_font(heading,&lv_font_montserrat_40,0);
    lv_obj_set_pos(heading,96,64);
    settings_row(settings,"Volume",200,volume,0,&volume_value,true);
    settings_row(settings,"Brightness",360,brightness,BRIGHTNESS_MIN,&brightness_value,false);
    lv_obj_t *back=lv_button_create(settings);
    lv_obj_set_size(back,360,88); lv_obj_align(back,LV_ALIGN_BOTTOM_MID,0,-64);
    lv_obj_set_style_bg_color(back,lv_color_hex(UI_ACCENT),0);
    lv_obj_t *text=lv_label_create(back); lv_label_set_text(text,LV_SYMBOL_LEFT "  Back to CarPlay");
    lv_obj_set_style_text_color(text,lv_color_hex(UI_BG),0);
    lv_obj_set_style_text_font(text,&lv_font_montserrat_28,0);
    lv_obj_center(text);
    lv_obj_add_event_cb(back,back_event,LV_EVENT_CLICKED,NULL);
    lv_obj_add_flag(settings,LV_OBJ_FLAG_HIDDEN);
}
esp_err_t tab5_ui_init(void)
{
    if(ready) return ESP_OK;
    /* Video bypasses LVGL, so the status screens can draw from PSRAM. Internal
     * memory is worth more as L2 cache for the decoder. */
    const bsp_display_cfg_t config={
        .lvgl_port_cfg=ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size=BSP_LCD_H_RES*CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer=0,
        .flags={.buff_dma=false,.buff_spiram=true,.sw_rotate=true},
    };
    display=bsp_display_start_with_config(&config); if(!display) return ESP_FAIL;
    ppa_client_config_t client={.oper_type=PPA_OPERATION_SRM};
    esp_err_t e=panel?esp_lcd_dpi_panel_get_frame_buffer(panel,2,&framebuffers[0],&framebuffers[1]):ESP_ERR_INVALID_STATE;
    if(e==ESP_OK) e=ppa_register_client(&client,&scaler);
    if(e!=ESP_OK) return e;
    if(!bsp_display_lock(1000)) return ESP_ERR_TIMEOUT;
    bsp_display_rotate(display,LV_DISPLAY_ROTATION_90);
    /* CarPlay scrolling follows the touch reports, so read faster than LVGL's 33 ms. */
    lv_timer_set_period(lv_indev_get_read_timer(bsp_display_get_input_dev()),TOUCH_READ_MS);
    width=lv_display_get_horizontal_resolution(display); height=lv_display_get_vertical_resolution(display);
    lv_obj_t *screen=lv_screen_active();
    lv_obj_set_style_bg_color(screen,lv_color_hex(UI_BG),0);
    brand=label(screen,"Tab5 / CarPlay",48,UI_ACCENT);
    title=label(screen,"Connect your iPhone",height/2-64,UI_TEXT);
    lv_obj_set_style_text_font(title,&lv_font_montserrat_40,0);
    detail=label(screen,"Connect an unlocked iPhone to the USB-A port.",height/2,UI_MUTED);
    mode=label(screen,"Device mode",height-64,UI_MUTED);
    lv_obj_set_style_bg_color(mode,lv_color_hex(UI_BG),0);
    lv_obj_set_style_bg_opa(mode,LV_OPA_COVER,0);
    lv_obj_set_style_pad_all(mode,8,0);
    lv_obj_add_flag(screen,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen,touch_callback,LV_EVENT_ALL,NULL);
    /* NVS is shared with pairing; initializing twice is harmless and never erases. */
    (void)nvs_flash_init();
    const int volume=load_setting("volume",VOLUME_DEFAULT,0,100);
    const int brightness=load_setting("brightness",BRIGHTNESS_DEFAULT,BRIGHTNESS_MIN,100);
    tab5_audio_set_volume(volume);
    build_settings(screen,volume,brightness);
    lv_timer_create(settings_timer,100,NULL);
    ESP_LOGI("SETTINGS","volume=%d brightness=%d",volume,brightness);
    ready=true;
    bsp_display_unlock(); return bsp_display_brightness_set(brightness);
}
void tab5_ui_touch_sink(tab5_touch_sink callback,void *context)
{
    if(!ready || !bsp_display_lock(1000)) return;
    deliver(cp_touch_cancel(&touch)); sink=callback; sink_context=context;
    bsp_display_unlock();
}
/* The LVGL lock is held; the next render repaints the whole screen. */
static void leave_video(void)
{
    if(!video_visible) return;
    video_visible=false;
    lv_display_enable_invalidation(display,true);
    lv_obj_remove_flag(title,LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(detail,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(mode,LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(lv_screen_active());
}
/* The LVGL lock is held. Finish LVGL's pending work first, so that it cannot
 * overwrite video later. Simulation keeps its label on top of the frames. */
static void enter_video(void)
{
    if(video_visible) return;
    lv_obj_add_flag(title,LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(detail,LV_OBJ_FLAG_HIDDEN);
    if(!simulated) lv_obj_add_flag(mode,LV_OBJ_FLAG_HIDDEN);
    lv_refr_now(display);
    if(!simulated) lv_display_enable_invalidation(display,false);
    video_visible=true;
}
static void clear_framebuffers(void)
{
    const size_t size=(size_t)width*height*2;
    for(unsigned i=0;i<2;++i) {
        memset(framebuffers[i],0,size);
        esp_cache_msync(framebuffers[i],size,ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
}
void tab5_ui_status(cp_stage stage,cp_error error,bool simulation)
{
    if(!ready || !bsp_display_lock(1000)) return;
    simulated=simulation;
    const char *hint="Preparing the connection.";
    switch(stage) {
    case CP_DETACHED: hint="Connect an unlocked iPhone to the USB-A port."; break;
    case CP_TRUST_PENDING: hint="Confirm Trust on your iPhone when prompted."; break;
    case CP_LOCKDOWN: hint="Phone paired. Preparing CarPlay authentication."; break;
    case CP_AUTHENTICATING: hint="Checking accessory authentication."; break;
    case CP_BACKOFF: hint="The connection will retry shortly."; break;
    case CP_DRAINING: hint="Closing the current connection."; break;
    case CP_STREAMING: hint="Connected"; break;
    default: break;
    }
    if(error==CP_ERR_AUTH_MISSING) hint="CarPlay authentication material is required.";
    else if(error==CP_ERR_AUTH_REJECTED) hint="Accessory authentication was rejected.";
    else if(error==CP_ERR_TIMEOUT) hint="The connection timed out.";
    else if(error==CP_ERR_IO) hint="The connection was interrupted.";
    else if(error==CP_ERR_STORAGE) hint="Pairing storage unavailable. Existing records preserved.";
    else if(error==CP_ERR_PROFILE) hint="Phone USB network profile needs configuration.";
    lv_label_set_text(title,cp_stage_name(stage)); lv_label_set_text(detail,hint);
    lv_obj_set_style_text_color(detail,lv_color_hex(error?UI_ERROR:UI_MUTED),0);
    lv_label_set_text(mode,simulation?"Simulation / synthetic media":"Device mode");
    if(stage==CP_STREAMING) lv_obj_add_flag(brand,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(brand,LV_OBJ_FLAG_HIDDEN);
    if(stage!=CP_STREAMING) {
        deliver(cp_touch_cancel(&touch)); memset(&touch,0,sizeof(touch));
        if(settings_open) { settings_open=false; lv_obj_add_flag(settings,LV_OBJ_FLAG_HIDDEN); }
        leave_video();
    }
    bsp_display_unlock();
}
bool tab5_ui_frame(const uint8_t *data,unsigned frame_width,unsigned frame_height)
{
    if(!ready || !data || !bsp_display_lock(1000)) return false;
    const bool resized=frame_width!=touch.video_width || frame_height!=touch.video_height;
    if(resized) {
        deliver(cp_touch_cancel(&touch));
        const carplay_video_profile_t *profile=carplay_video_profile_current();
        if(!cp_touch_configure(&touch,width,height,frame_width,frame_height,profile->aspect_width,profile->aspect_height)) { bsp_display_unlock(); return false; }
    }
    if(settings_open) { bsp_display_unlock(); return true; }   /* the settings page covers the video */
    const bool entering=!video_visible;
    enter_video();
    const cp_touch area=touch;
    bsp_display_unlock();
    if(entering || resized) clear_framebuffers();
    /* ROTATION_90 maps landscape (x,y) to panel (y,1279-x), as LVGL's flush does.
     * The coefficients match BT.601 limited range, the decoder's output so far. */
    const ppa_srm_oper_config_t operation={
        .in={.buffer=data,.pic_w=frame_width,.pic_h=frame_height,.block_w=frame_width,.block_h=frame_height,
             .srm_cm=PPA_SRM_COLOR_MODE_YUV420,.yuv_range=PPA_COLOR_RANGE_LIMIT,.yuv_std=PPA_COLOR_CONV_STD_RGB_YUV_BT601},
        .out={.buffer=framebuffers[hidden_buffer],.buffer_size=(size_t)width*height*2,.pic_w=(uint32_t)height,.pic_h=(uint32_t)width,
              .block_offset_x=(uint32_t)area.y,.block_offset_y=(uint32_t)(width-area.x-area.width),.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
        .rotation_angle=PPA_SRM_ROTATION_ANGLE_90,
        .scale_x=(float)area.width/frame_width,.scale_y=(float)area.height/frame_height,
        .mode=PPA_TRANS_MODE_BLOCKING,
    };
    esp_err_t e=ppa_do_scale_rotate_mirror(scaler,&operation);
    if(e!=ESP_OK) { ESP_LOGE("VIDEO","PPA failed: %s",esp_err_to_name(e)); return false; }
    if(!bsp_display_lock(1000)) return false;
    /* A status change may have returned the screen to LVGL during the PPA pass. */
    if(video_visible) {
        e=esp_lcd_panel_draw_bitmap(panel,0,0,height,width,framebuffers[hidden_buffer]);
        if(e==ESP_OK) hidden_buffer^=1;
        if(simulated) { lv_obj_invalidate(mode); lv_refr_now(display); }
    }
    bsp_display_unlock(); return e==ESP_OK;
}
void tab5_ui_clear(void)
{
    if(!ready || !bsp_display_lock(1000)) return;
    deliver(cp_touch_cancel(&touch)); memset(&touch,0,sizeof(touch));
    leave_video();
    bsp_display_unlock();
}
