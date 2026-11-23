#include "echo_cancel.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#define ECHO_ALLOC(bytes) heap_caps_calloc(1, (bytes), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define ECHO_LOG(...) ESP_LOGI("ECHO", __VA_ARGS__)
#else
#define ECHO_ALLOC(bytes) calloc(1, (bytes))
#define ECHO_LOG(...) ((void)0)
#endif

#if defined(CONFIG_TAB5_ECHO_ESPSR)
/* Espressif ESP-SR: closed binary libraries under the Espressif licence. */
#include "esp_aec.h"
#define ENGINE_NAME "esp-sr"
static aec_handle_t *engine;
static size_t engine_open(void)
{
    aec_config_t config = {
        .mic_num = 1, .ref_num = 1, .out_num = 1,
        /* Frames of echo path to model; 8 covers the I2S buffering plus the room. */
        .filter_length = 8,
        .sample_rate = TAB5_ECHO_RATE,
        .caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        .mode = AEC_MODE_FD_LOW_COST,   /* full duplex: both people can talk at once */
        .nlp_level = AEC_NLP_LEVEL_AGGR,
    };
    engine = aec_create_from_config(&config);
    return engine ? (size_t)aec_get_chunksize(engine) : 0;
}
static void engine_run(int16_t *mic, int16_t *speaker, int16_t *out) { aec_process(engine, mic, speaker, out); }
static void engine_close(void) { if (engine) aec_destroy(engine); engine = NULL; }

#elif defined(CONFIG_TAB5_ECHO_SPEEX) || defined(TAB5_ECHO_HOST_SPEEX)
/* SpeexDSP (BSD-3-Clause): adaptive filter, then the preprocessor removes the
 * echo the filter leaves behind. */
#include "speex/speex_echo.h"
#include "speex/speex_preprocess.h"
#define ENGINE_NAME "speexdsp"
#define SPEEX_TAIL_SAMPLES 2048u   /* 128 ms of echo path */
static SpeexEchoState *echo;
static SpeexPreprocessState *residual;
static size_t engine_open(void)
{
    spx_int32_t rate = TAB5_ECHO_RATE;
    echo = speex_echo_state_init(TAB5_ECHO_BLOCK, SPEEX_TAIL_SAMPLES);
    residual = speex_preprocess_state_init(TAB5_ECHO_BLOCK, TAB5_ECHO_RATE);
    if (!echo || !residual) {
        if (echo) speex_echo_state_destroy(echo);
        if (residual) speex_preprocess_state_destroy(residual);
        echo = NULL; residual = NULL;
        return 0;
    }
    speex_echo_ctl(echo, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
    speex_preprocess_ctl(residual, SPEEX_PREPROCESS_SET_ECHO_STATE, echo);
    return TAB5_ECHO_BLOCK;
}
static void engine_run(int16_t *mic, int16_t *speaker, int16_t *out)
{
    speex_echo_cancellation(echo, mic, speaker, out);
    speex_preprocess_run(residual, out);
}
static void engine_close(void)
{
    if (residual) speex_preprocess_state_destroy(residual);
    if (echo) speex_echo_state_destroy(echo);
    residual = NULL; echo = NULL;
}

#elif defined(TAB5_ECHO_HOST_FRAME)
/* Host test only: passes the microphone through in frames of TAB5_ECHO_HOST_FRAME. */
#define ENGINE_NAME "host-passthrough"
static size_t engine_open(void) { return TAB5_ECHO_HOST_FRAME; }
static void engine_run(int16_t *mic, int16_t *speaker, int16_t *out)
{ (void)speaker; memcpy(out, mic, TAB5_ECHO_HOST_FRAME * sizeof(int16_t)); }
static void engine_close(void) {}

#else
#define ENGINE_NAME "none"
static size_t engine_open(void) { return 0; }
static void engine_run(int16_t *mic, int16_t *speaker, int16_t *out) { (void)mic; (void)speaker; (void)out; }
static void engine_close(void) {}
#endif

/* Bridges 10 ms blocks to the engine's frame: inputs gather until a frame is full,
 * results queue until the caller takes them. A frame shorter than a block can finish
 * twice per block, so every buffer holds two frames and two blocks. */
static size_t frame, gathered, queued;
static int16_t *mic_in, *speaker_in, *result, *output;
static bool active, started;

const char *tab5_echo_engine(void) { return ENGINE_NAME; }
bool tab5_echo_active(void) { return active; }

void tab5_echo_close(void)
{
    if (active) ECHO_LOG("off");
    engine_close();
    free(mic_in); free(speaker_in); free(result); free(output);
    mic_in = speaker_in = result = output = NULL;
    frame = gathered = queued = 0;
    active = started = false;
}

bool tab5_echo_open(void)
{
    if (active) return true;
    frame = engine_open();
    if (!frame || frame > 4096u) { tab5_echo_close(); return false; }
    const size_t bytes = 2u * (frame + TAB5_ECHO_BLOCK) * sizeof(int16_t);
    mic_in = ECHO_ALLOC(bytes); speaker_in = ECHO_ALLOC(bytes);
    result = ECHO_ALLOC(bytes); output = ECHO_ALLOC(bytes);
    if (!mic_in || !speaker_in || !result || !output) { tab5_echo_close(); return false; }
    active = true;
    ECHO_LOG("on: engine=%s frame=%u samples", ENGINE_NAME, (unsigned)frame);
    return true;
}

void tab5_echo_process(const int16_t *mic, const int16_t *speaker, int16_t *out)
{
    if (!active) { if (out != mic) memcpy(out, mic, TAB5_ECHO_BLOCK * sizeof(int16_t)); return; }
    memcpy(mic_in + gathered, mic, TAB5_ECHO_BLOCK * sizeof(int16_t));
    memcpy(speaker_in + gathered, speaker, TAB5_ECHO_BLOCK * sizeof(int16_t));
    gathered += TAB5_ECHO_BLOCK;
    while (gathered >= frame) {
        engine_run(mic_in, speaker_in, result);
        memcpy(output + queued, result, frame * sizeof(int16_t));
        queued += frame;
        gathered -= frame;
        memmove(mic_in, mic_in + frame, gathered * sizeof(int16_t));
        memmove(speaker_in, speaker_in + frame, gathered * sizeof(int16_t));
    }
    /* Output starts once gathered + queued can never fall below a block between two
     * frames: after a frame, gathered < frame, so queued >= total - frame + 1. From
     * then on every block in is a block out and the total stays put. */
    if (!started) started = gathered + queued >= frame + TAB5_ECHO_BLOCK - 1u && queued >= TAB5_ECHO_BLOCK;
    if (!started) { memset(out, 0, TAB5_ECHO_BLOCK * sizeof(int16_t)); return; }
    memcpy(out, output, TAB5_ECHO_BLOCK * sizeof(int16_t));
    queued -= TAB5_ECHO_BLOCK;
    memmove(output, output + TAB5_ECHO_BLOCK, queued * sizeof(int16_t));
}
