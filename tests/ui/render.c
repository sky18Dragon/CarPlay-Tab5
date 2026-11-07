#include "app_ui.h"
#include "bsp/m5stack_tab5.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t frame[1280*720],draw[1280*64];
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *p) {
    for(int y=area->y1;y<=area->y2;++y) {
        memcpy(frame+y*1280+area->x1,p,(area->x2-area->x1+1)*2);
        p+=(area->x2-area->x1+1)*2;
    }
    lv_display_flush_ready(d);
}
lv_display_t *bsp_display_start(void) {
    lv_init(); lv_display_t *d=lv_display_create(1280,720);
    lv_display_set_buffers(d,draw,NULL,sizeof(draw),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d,flush); return d;
}
bool bsp_display_lock(int timeout) { (void)timeout; return true; }
void bsp_display_unlock(void) {}
void bsp_display_rotate(lv_display_t *d,lv_display_rotation_t rotation) { (void)d;(void)rotation; }
esp_err_t bsp_display_brightness_set(int level) { (void)level;return ESP_OK; }
static void save(const char *path) {
    lv_refr_now(NULL); FILE *f=fopen(path,"wb"); assert(f);
    fprintf(f,"P6\n1280 720\n255\n");
    for(unsigned i=0;i<1280*720;++i) {
        uint16_t c=frame[i]; uint8_t rgb[]={((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};
        fwrite(rgb,1,3,f);
    }
    fclose(f);
}
int main(void) {
    assert(tab5_ui_init()==ESP_OK);
    save("artifacts/ui-disconnected.ppm");
    tab5_ui_status(CP_FAILED,CP_ERR_AUTH_MISSING,false); save("artifacts/ui-auth-missing.ppm");
    uint8_t y[160*96],u[80*48],v[80*48];
    memset(y,100,sizeof(y));memset(u,128,sizeof(u));memset(v,128,sizeof(v));
    cp_i420 image={.planes={y,u,v},.stride={160,80,80},.width=160,.height=96};
    tab5_ui_status(CP_STREAMING,CP_ERR_NONE,true);
    assert(tab5_ui_frame(&image)); save("artifacts/ui-simulation.ppm");
    tab5_ui_clear(); tab5_ui_status(CP_DETACHED,CP_ERR_NONE,false);
    save("artifacts/ui-cleared.ppm");
    puts("LVGL host rendered four UI states using production app_ui.c");
}
