#include "audio_resample.h"
#include <string.h>

void cp_upsampler_reset(cp_upsampler *u) { memset(u, 0, sizeof(*u)); }

static int16_t frame_sample(const cp_upsampler *u, const int16_t *in, unsigned channels, long index, unsigned ch)
{
    if (index < 0) return u->last[ch];
    return in[(size_t)index * channels + (channels == 2 ? ch : 0)];
}

void cp_upsample_block(cp_upsampler *u, const int16_t *in, size_t in_frames, unsigned channels,
                       int16_t *out, size_t out_frames)
{
    if (!out_frames) return;
    if (!in_frames) { memset(out, 0, out_frames * 4); return; }
    /* Output j sits at input position (j+1)*n/out - 1, so the last output
     * lands exactly on the last input frame and index -1 is the previous block. */
    for (size_t j = 0; j < out_frames; ++j) {
        const uint64_t num = (uint64_t)(j + 1) * in_frames;
        const long index = (long)(num / out_frames) - 1;
        const uint32_t frac = (uint32_t)(num % out_frames);
        for (unsigned ch = 0; ch < 2; ++ch) {
            const int32_t a = frame_sample(u, in, channels, index, ch);
            int32_t value = a;
            if (frac) {
                const int32_t b = frame_sample(u, in, channels, index + 1, ch);
                value = a + (int32_t)(((int64_t)(b - a) * frac) / (int64_t)out_frames);
            }
            out[j * 2 + ch] = (int16_t)value;
        }
    }
    for (unsigned ch = 0; ch < 2; ++ch) u->last[ch] = frame_sample(u, in, channels, (long)in_frames - 1, ch);
}

bool cp_decimate_supported(unsigned rate)
{
    return rate && rate <= CP_AUDIO_RATE && CP_AUDIO_RATE % rate == 0;
}

void cp_decimate_mono(const int16_t *in, size_t out_frames, unsigned rate, int16_t *out)
{
    const unsigned factor = CP_AUDIO_RATE / rate;
    for (size_t i = 0; i < out_frames; ++i) {
        int32_t sum = 0;
        for (unsigned k = 0; k < factor; ++k) sum += in[i * factor + k];
        out[i] = (int16_t)(sum / (int32_t)factor);
    }
}
