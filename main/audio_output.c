#include "audio_output.h"
#include "core/pcm_buffer.h"
#include "core/audio_resample.h"
#include "bsp/m5stack_tab5.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>

/* One worker owns both codecs. Each 10 ms it mixes the playback lanes into a
 * 48 kHz stereo block, writes it, then reads the same amount from the
 * microphones; the shared I2S clock paces the loop. */
#define TAG "AUDIO_OUT"
#define LANE_MAX_FRAMES 24000u           /* 0.5 s at 48 kHz */
#define PREROLL_MS 60u
#define MIC_RING_SAMPLES 9600u           /* 200 ms at 48 kHz mono */
#define MIC_READ_MAX 2880u               /* 60 ms at 48 kHz */
#define SPEAKER_VOLUME_DEFAULT 30
#define MIC_GAIN_DB 30.0f
#define DUCK_GAIN_Q8 77                  /* about 30 % */
#define IDLE_CLOSE_BLOCKS 300u           /* close the codecs after 3 s of nothing */
#define STATS_BLOCKS 300u

typedef struct {
    cp_pcm pcm;
    uint8_t *samples, *valid;
    cp_upsampler up;
    unsigned rate, channels;
    uint64_t generation;
    bool active;
    uint32_t reanchors, lock_misses, faster, slower;
} lane_t;

static lane_t lanes[TAB5_AUDIO_LANES];
static SemaphoreHandle_t lock;
static volatile bool ducked;
static int mic_users;
static int16_t *mic_ring, *mic_scratch;
static size_t mic_head, mic_count;
static int mic_peak;
static esp_codec_dev_handle_t speaker, microphone;
static bool codec_open;
static volatile int volume = SPEAKER_VOLUME_DEFAULT, applied_volume = -1;

static bool rate_supported(uint32_t rate)
{
    return rate == 8000 || rate == 16000 || rate == 24000 || rate == 32000 || rate == 48000;
}

static esp_err_t open_codecs(void)
{
    if (!speaker) speaker = bsp_audio_codec_speaker_init();
    if (!microphone) microphone = bsp_audio_codec_microphone_init();
    esp_codec_dev_sample_info_t fmt = {.sample_rate = CP_AUDIO_RATE, .channel = 2, .bits_per_sample = 16};
    if (!speaker || !microphone || esp_codec_dev_open(speaker, &fmt) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "speaker open failed");
        return ESP_FAIL;
    }
    applied_volume = volume;
    if (esp_codec_dev_set_out_vol(speaker, applied_volume) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_mute(speaker, false) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_open(microphone, &fmt) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "volume, unmute or microphone open failed");
        esp_codec_dev_close(speaker);
        return ESP_FAIL;
    }
    (void)esp_codec_dev_set_in_gain(microphone, MIC_GAIN_DB);
    codec_open = true;
    ESP_LOGI(TAG, "codecs open: 48 kHz stereo duplex, volume=%d mic_gain=%.1f dB", applied_volume, (double)MIC_GAIN_DB);
    return ESP_OK;
}

static void close_codecs(void)
{
    esp_codec_dev_close(microphone);
    esp_codec_dev_close(speaker);
    codec_open = false;
    ESP_LOGI(TAG, "codecs closed (idle)");
}

static bool wanted_locked(void)
{
    return lanes[0].active || lanes[1].active || mic_users > 0;
}

/* Reads one lane's next 10 ms and converts it to 48 kHz stereo. Reading one
 * frame more or less follows the phone's clock without letting the buffer
 * drift out of its window. */
static bool render_lane(lane_t *l, int16_t *pcm, int16_t *block)
{
    if (!l->active) return false;
    size_t frames = l->rate / 100u;
    if (l->pcm.playing) {
        if (l->pcm.queued > l->pcm.preroll * 3u) { ++frames; ++l->faster; }
        else if (l->pcm.queued && l->pcm.queued < l->pcm.preroll / 3u) { --frames; ++l->slower; }
    }
    cp_pcm_read(&l->pcm, l->generation, (uint8_t *)pcm, frames);
    cp_upsample_block(&l->up, pcm, frames, l->channels, block, CP_AUDIO_BLOCK_FRAMES);
    return true;
}

static void worker(void *arg)
{
    (void)arg;
    const size_t block_bytes = CP_AUDIO_BLOCK_FRAMES * 2u * sizeof(int16_t);
    int16_t *out = heap_caps_malloc(block_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int16_t *in = heap_caps_malloc(block_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int16_t *lane_block = heap_caps_malloc(block_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int16_t *pcm = heap_caps_malloc((CP_AUDIO_BLOCK_FRAMES + 2u) * 4u, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int32_t *mix = heap_caps_malloc(CP_AUDIO_BLOCK_FRAMES * 2u * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!out || !in || !lane_block || !pcm || !mix) {
        ESP_LOGE(TAG, "worker buffers unavailable");
        vTaskDelete(NULL);
        return;
    }
    unsigned idle = 0, blocks = 0;
    for (;;) {
        xSemaphoreTake(lock, portMAX_DELAY);
        const bool want = wanted_locked();
        xSemaphoreGive(lock);
        if (!codec_open) {
            if (!want || open_codecs() != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(want ? 1000 : 20)); continue; }
            idle = 0;
        }
        if (!want && ++idle > IDLE_CLOSE_BLOCKS) { close_codecs(); continue; }
        if (applied_volume != volume) {
            applied_volume = volume;
            (void)esp_codec_dev_set_out_vol(speaker, applied_volume);
        }
        if (want) idle = 0;

        memset(mix, 0, CP_AUDIO_BLOCK_FRAMES * 2u * sizeof(int32_t));
        for (unsigned i = 0; i < TAB5_AUDIO_LANES; ++i) {
            xSemaphoreTake(lock, portMAX_DELAY);
            const bool have = render_lane(&lanes[i], pcm, lane_block);
            xSemaphoreGive(lock);
            if (!have) continue;
            const int32_t gain = (i == 0 && ducked) ? DUCK_GAIN_Q8 : 256;
            for (unsigned k = 0; k < CP_AUDIO_BLOCK_FRAMES * 2u; ++k) mix[k] += lane_block[k] * gain;
        }
        for (unsigned k = 0; k < CP_AUDIO_BLOCK_FRAMES * 2u; ++k) {
            int32_t v = mix[k] / 256;
            out[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        if (esp_codec_dev_write(speaker, out, (int)block_bytes) != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "speaker write failed; reopening");
            close_codecs(); vTaskDelay(pdMS_TO_TICKS(100)); continue;
        }
        if (esp_codec_dev_read(microphone, in, (int)block_bytes) != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "microphone read failed; reopening");
            close_codecs(); vTaskDelay(pdMS_TO_TICKS(100)); continue;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        if (mic_users > 0) {
            for (unsigned k = 0; k < CP_AUDIO_BLOCK_FRAMES; ++k) {
                const int16_t s = (int16_t)(((int32_t)in[2 * k] + in[2 * k + 1]) / 2);
                const int a = s < 0 ? -s : s;
                if (a > mic_peak) mic_peak = a;
                mic_ring[mic_head] = s;
                mic_head = (mic_head + 1u) % MIC_RING_SAMPLES;
                if (mic_count < MIC_RING_SAMPLES) ++mic_count;
            }
        }
        if (++blocks >= STATS_BLOCKS) {
            blocks = 0;
            for (unsigned i = 0; i < TAB5_AUDIO_LANES; ++i) {
                lane_t *l = &lanes[i];
                if (!l->active) continue;
                ESP_LOGI(TAG, "STATS lane=%u rate=%u ch=%u queued_ms=%u underrun_frames=%llu rejected=%llu rebuffers=%llu reanchors=%lu lock_misses=%lu faster=%lu slower=%lu ducked=%d",
                    i, l->rate, l->channels, (unsigned)(l->pcm.queued * 1000u / l->rate),
                    (unsigned long long)l->pcm.underrun_frames, (unsigned long long)l->pcm.rejected_packets, (unsigned long long)l->pcm.rebuffer_count,
                    (unsigned long)l->reanchors, (unsigned long)l->lock_misses,
                    (unsigned long)l->faster, (unsigned long)l->slower, ducked);
            }
            if (mic_users > 0) ESP_LOGI(TAG, "STATS mic users=%d peak=%d buffered_ms=%u", mic_users, mic_peak, (unsigned)(mic_count / 48u));
            mic_peak = 0;
        }
        xSemaphoreGive(lock);
    }
}

esp_err_t tab5_audio_init(void)
{
    if (lock) return ESP_OK;
    lock = xSemaphoreCreateMutex();
    mic_ring = heap_caps_calloc(MIC_RING_SAMPLES, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    mic_scratch = heap_caps_calloc(MIC_READ_MAX, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool ok = lock && mic_ring && mic_scratch;
    for (unsigned i = 0; ok && i < TAB5_AUDIO_LANES; ++i) {
        lanes[i].samples = heap_caps_calloc(LANE_MAX_FRAMES, 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        lanes[i].valid = heap_caps_calloc(LANE_MAX_FRAMES, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ok = lanes[i].samples && lanes[i].valid &&
            cp_pcm_init(&lanes[i].pcm, lanes[i].samples, lanes[i].valid, LANE_MAX_FRAMES, CP_AUDIO_RATE, 2,
                        CP_AUDIO_RATE * PREROLL_MS / 1000u);
    }
    if (ok && xTaskCreate(worker, "pcm_play", 6144, NULL, 6, NULL) == pdPASS) return ESP_OK;
    ESP_LOGE(TAG, "init failed");
    return ESP_ERR_NO_MEM;
}

esp_err_t tab5_audio_start(unsigned lane, uint64_t generation, uint32_t rate, unsigned channels)
{
    if (!lock || lane >= TAB5_AUDIO_LANES || !generation || !rate_supported(rate) || (channels != 1 && channels != 2))
        return ESP_ERR_INVALID_ARG;
    lane_t *l = &lanes[lane];
    xSemaphoreTake(lock, portMAX_DELAY);
    /* Half a second of buffer at the stream's own rate. */
    bool ok = cp_pcm_init(&l->pcm, l->samples, l->valid, rate / 2u, rate, channels, rate * PREROLL_MS / 1000u);
    if (ok) {
        cp_pcm_reset(&l->pcm, generation);
        cp_upsampler_reset(&l->up);
        l->rate = rate; l->channels = channels; l->generation = generation; l->active = true;
        l->reanchors = l->lock_misses = l->faster = l->slower = 0;
    }
    xSemaphoreGive(lock);
    if (ok) ESP_LOGI(TAG, "lane %u start rate=%u channels=%u", lane, (unsigned)rate, channels);
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static void stop_lane_locked(lane_t *l)
{
    l->active = false; l->generation = 0;
    cp_pcm_reset(&l->pcm, 0);
}

void tab5_audio_stop(unsigned lane, uint64_t generation)
{
    if (!lock || lane >= TAB5_AUDIO_LANES) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    lane_t *l = &lanes[lane];
    if (l->active && l->generation == generation) {
        ESP_LOGI(TAG, "lane %u stop underrun_frames=%llu rejected=%llu rebuffers=%llu reanchors=%lu lock_misses=%lu faster=%lu slower=%lu",
            lane, (unsigned long long)l->pcm.underrun_frames, (unsigned long long)l->pcm.rejected_packets, (unsigned long long)l->pcm.rebuffer_count,
            (unsigned long)l->reanchors, (unsigned long)l->lock_misses, (unsigned long)l->faster, (unsigned long)l->slower);
        stop_lane_locked(l);
    }
    xSemaphoreGive(lock);
}

void tab5_audio_stop_all(void)
{
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (unsigned i = 0; i < TAB5_AUDIO_LANES; ++i) stop_lane_locked(&lanes[i]);
    ducked = false;
    xSemaphoreGive(lock);
}

bool tab5_audio_submit(unsigned lane, uint64_t generation, uint32_t timestamp, const uint8_t *data, size_t bytes, bool big)
{
    if (!lock || lane >= TAB5_AUDIO_LANES) return false;
    lane_t *l = &lanes[lane];
    if (xSemaphoreTake(lock, pdMS_TO_TICKS(5)) != pdTRUE) { ++l->lock_misses; return false; }
    bool ok = l->active && l->generation == generation && cp_pcm_push(&l->pcm, generation, timestamp, data, bytes, big);
    if (!ok && l->active && l->generation == generation && l->pcm.queued == 0) {
        /* After a pause the phone's timestamps no longer match the playout
         * cursor; with nothing queued, restart the timeline at this packet. */
        cp_pcm_reset(&l->pcm, generation);
        ++l->reanchors;
        ok = cp_pcm_push(&l->pcm, generation, timestamp, data, bytes, big);
    }
    xSemaphoreGive(lock);
    return ok;
}

void tab5_audio_set_volume(int percent)
{
    volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
}

int tab5_audio_volume(void) { return volume; }

void tab5_audio_duck(bool on)
{
    if (ducked != on) ESP_LOGI(TAG, "duck=%d", on);
    ducked = on;
}

esp_err_t tab5_mic_start(void)
{
    if (!lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (mic_users++ == 0) { mic_head = mic_count = 0; mic_peak = 0; }
    const int users = mic_users;
    xSemaphoreGive(lock);
    ESP_LOGI(TAG, "mic start users=%d", users);
    return ESP_OK;
}

void tab5_mic_stop(void)
{
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (mic_users > 0) --mic_users;
    const int users = mic_users;
    xSemaphoreGive(lock);
    ESP_LOGI(TAG, "mic stop users=%d", users);
}

size_t tab5_mic_read(unsigned rate, int16_t *samples, size_t frames)
{
    if (!lock || !samples || !frames || !cp_decimate_supported(rate)) return 0;
    const size_t need = frames * (CP_AUDIO_RATE / rate);
    if (need > MIC_READ_MAX) return 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (mic_count < need) { xSemaphoreGive(lock); return 0; }
    size_t tail = (mic_head + MIC_RING_SAMPLES - mic_count) % MIC_RING_SAMPLES;
    for (size_t i = 0; i < need; ++i) mic_scratch[i] = mic_ring[(tail + i) % MIC_RING_SAMPLES];
    mic_count -= need;
    xSemaphoreGive(lock);
    cp_decimate_mono(mic_scratch, frames, rate, samples);
    return frames;
}
