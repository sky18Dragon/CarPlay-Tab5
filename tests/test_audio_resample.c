#include "core/audio_resample.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int16_t out[CP_AUDIO_BLOCK_FRAMES * 2];
    cp_upsampler u;

    /* 48 kHz stereo passes through unchanged. */
    int16_t stereo[CP_AUDIO_BLOCK_FRAMES * 2];
    for (unsigned i = 0; i < CP_AUDIO_BLOCK_FRAMES * 2; ++i) stereo[i] = (int16_t)(i * 7 - 3000);
    cp_upsampler_reset(&u);
    cp_upsample_block(&u, stereo, CP_AUDIO_BLOCK_FRAMES, 2, out, CP_AUDIO_BLOCK_FRAMES);
    for (unsigned i = 0; i < CP_AUDIO_BLOCK_FRAMES * 2; ++i) assert(out[i] == stereo[i]);
    assert(u.last[0] == stereo[CP_AUDIO_BLOCK_FRAMES * 2 - 2] && u.last[1] == stereo[CP_AUDIO_BLOCK_FRAMES * 2 - 1]);

    /* 24 kHz mono: every second output is an input sample, the others are midpoints;
     * both channels carry the mono signal and the block joins the previous one. */
    int16_t mono[240];
    for (unsigned i = 0; i < 240; ++i) mono[i] = (int16_t)(100 * i);
    cp_upsampler_reset(&u);
    u.last[0] = u.last[1] = -100;
    cp_upsample_block(&u, mono, 240, 1, out, CP_AUDIO_BLOCK_FRAMES);
    for (unsigned j = 0; j < CP_AUDIO_BLOCK_FRAMES; ++j) {
        assert(out[2 * j] == out[2 * j + 1]);
        assert(out[2 * j] == (int16_t)(50 * (int)j - 50));
    }
    assert(u.last[0] == 23900);

    /* 16 kHz (x3) and 8 kHz (x6) stay monotonic for a ramp and end on the last input. */
    const unsigned rates[] = {8000, 16000, 32000};
    for (unsigned r = 0; r < 3; ++r) {
        const unsigned n = rates[r] / 100;
        int16_t ramp[320];
        for (unsigned i = 0; i < n; ++i) ramp[i] = (int16_t)(10 * i);
        cp_upsampler_reset(&u);
        cp_upsample_block(&u, ramp, n, 1, out, CP_AUDIO_BLOCK_FRAMES);
        for (unsigned j = 1; j < CP_AUDIO_BLOCK_FRAMES; ++j) assert(out[2 * j] >= out[2 * j - 2]);
        assert(out[2 * (CP_AUDIO_BLOCK_FRAMES - 1)] == ramp[n - 1]);
    }

    /* Drift correction: one extra or one missing input frame still fills the block. */
    int16_t extra[481];
    for (unsigned i = 0; i < 481; ++i) extra[i] = (int16_t)i;
    cp_upsampler_reset(&u);
    cp_upsample_block(&u, extra, 481, 1, out, CP_AUDIO_BLOCK_FRAMES);
    assert(out[2 * (CP_AUDIO_BLOCK_FRAMES - 1)] == 480);
    cp_upsample_block(&u, extra, 479, 1, out, CP_AUDIO_BLOCK_FRAMES);
    assert(out[2 * (CP_AUDIO_BLOCK_FRAMES - 1)] == 478);

    /* An empty block is silence. */
    cp_upsample_block(&u, extra, 0, 1, out, CP_AUDIO_BLOCK_FRAMES);
    for (unsigned i = 0; i < CP_AUDIO_BLOCK_FRAMES * 2; ++i) assert(out[i] == 0);

    /* Decimation: box average over the factor. */
    assert(cp_decimate_supported(8000) && cp_decimate_supported(16000) && cp_decimate_supported(24000) &&
           cp_decimate_supported(48000) && !cp_decimate_supported(32000) && !cp_decimate_supported(44100) &&
           !cp_decimate_supported(0));
    int16_t in48[480], low[480];
    for (unsigned i = 0; i < 480; ++i) in48[i] = (int16_t)(i % 2 ? 300 : 100);
    cp_decimate_mono(in48, 240, 24000, low);
    for (unsigned i = 0; i < 240; ++i) assert(low[i] == 200);
    for (unsigned i = 0; i < 480; ++i) in48[i] = (int16_t)(-32768 + (i % 3));
    cp_decimate_mono(in48, 160, 16000, low);
    for (unsigned i = 0; i < 160; ++i) assert(low[i] == -32767);
    cp_decimate_mono(in48, 480, 48000, low);
    assert(low[0] == -32768 && low[1] == -32767);

    puts("audio resample: passthrough, 8/16/24/32 kHz upsampling, drift +/-1 frame, silence, decimation passed");
    return 0;
}
