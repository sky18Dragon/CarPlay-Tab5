#include "audio_output.h"
#include "airplay_audio_sink.h"
/* Current output exposes one PCM stream. Reject additional SETUP requests
 * instead of acknowledging audio that would be discarded or stealing a stream. */
static uint64_t owner,next_generation;
static uint64_t open_stream(unsigned rate,unsigned channels) {
    if(owner) return 0;
    if(++next_generation==0) ++next_generation;
    if(tab5_audio_start(next_generation,rate,channels)!=ESP_OK) return 0;
    owner=next_generation; return owner;
}
static bool write_stream(uint64_t generation,uint32_t timestamp,const uint8_t *data,size_t size) {
    return owner==generation && tab5_audio_submit(generation,timestamp,data,size,true);
}
static void close_stream(uint64_t generation) {
    if(generation==owner) { tab5_audio_stop(); owner=0; }
}
esp_err_t tab5_audio_routes_init(void) {
    const airplay_audio_sink sink={open_stream,write_stream,close_stream};
    return airplay_audio_install_sink(&sink)?ESP_OK:ESP_ERR_INVALID_STATE;
}
