#include "audio_output.h"
#include "airplay_audio_sink.h"
/* Maps AirPlay streams to playback lanes: type 101 (navigation) to lane 1,
 * everything else to lane 0. A new stream on a lane replaces the old one;
 * writes from the replaced stream are refused. */
static uint64_t owners[TAB5_AUDIO_LANES],next_generation;
static unsigned lane_for(unsigned type) { return type==101?1u:0u; }
static uint64_t open_stream(unsigned type,unsigned rate,unsigned channels) {
    const unsigned lane=lane_for(type);
    if(++next_generation==0) ++next_generation;
    if(tab5_audio_start(lane,next_generation,rate,channels)!=ESP_OK) return 0;
    owners[lane]=next_generation; return owners[lane];
}
static int lane_of(uint64_t generation) {
    for(unsigned lane=0;lane<TAB5_AUDIO_LANES;++lane) if(generation && owners[lane]==generation) return (int)lane;
    return -1;
}
static bool write_stream(uint64_t generation,uint32_t timestamp,const uint8_t *data,size_t size) {
    const int lane=lane_of(generation);
    return lane>=0 && tab5_audio_submit((unsigned)lane,generation,timestamp,data,size,true);
}
static void close_stream(uint64_t generation) {
    const int lane=lane_of(generation);
    if(lane<0) return;
    tab5_audio_stop((unsigned)lane,generation); owners[lane]=0;
}
static bool mic_start(void) { return tab5_mic_start()==ESP_OK; }
esp_err_t tab5_audio_routes_init(void) {
    const airplay_audio_sink sink={
        .open=open_stream,.write=write_stream,.close=close_stream,.duck=tab5_audio_duck,
        .mic_start=mic_start,.mic_read=tab5_mic_read,.mic_stop=tab5_mic_stop,
    };
    return airplay_audio_install_sink(&sink)?ESP_OK:ESP_ERR_INVALID_STATE;
}
