#include "airplay_service.h"
#include "app_session.h"
#include <stdatomic.h>
#include "airplay_software_provider.h"
#include "airplay_identity_port.h"
#include "airplay_rtsp_server.h"
#include "airplay_audio.h"
#include "audio_output.h"
#include "video_output.h"
#include "hid_output.h"
#include "ncm_host.h"
#include "app_ui.h"
#include "nvs_flash.h"
#include "entropy_source.h"
#include "esp_log.h"
static airplay_rtsp_server_t *server;
static atomic_uint_fast32_t owner;
esp_err_t tab5_airplay_start(uint64_t generation)
{
    if(!generation) return ESP_ERR_INVALID_ARG;
    if(server) return owner==generation?ESP_OK:ESP_ERR_INVALID_STATE;
    char address[64];
    if(!ncm_host_ipv6(address,sizeof(address))) return ESP_ERR_INVALID_STATE;
    /* No erase fallback: preserve existing Lockdown and receiver identities. */
    tab5_entropy_acquire();
    esp_err_t err=nvs_flash_init();
    if(err==ESP_OK) err=airplay_software_provider_init();
    airplay_identity_config_t identity;
    if(err==ESP_OK) err=airplay_identity_port_get_config(&identity);
    if(err==ESP_OK) err=airplay_rtsp_server_start(&identity.rtsp_server,&server);
    if(err!=ESP_OK) { tab5_entropy_release(); ESP_LOGE("AIRPLAY","Service start failed: %s",esp_err_to_name(err));return err; }
    owner=generation;
    tab5_ui_status(CP_SESSION,CP_ERR_NONE,false);
    return ESP_OK;
}
void tab5_airplay_stop(void)
{
    if(server) { airplay_rtsp_server_stop(server); server=NULL; tab5_entropy_release(); }
    airplay_audio_stop(0); tab5_audio_stop_all(); tab5_video_stop(); tab5_hid_reset(); owner=0;
}
uint16_t tab5_airplay_port(void) { return server?airplay_rtsp_server_get_port(server):0; }

void tab5_airplay_media_ready(void) { uint32_t epoch=atomic_load(&owner); if(epoch) tab5_session_post(epoch,CP_MEDIA_OK); }
