#include "echo_cancel.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define B TAB5_ECHO_BLOCK
__attribute__((unused)) static double energy(const int16_t *p, size_t n) { double e = 0; for (size_t i = 0; i < n; ++i) e += (double)p[i] * p[i]; return e; }
int main(void)
{
    assert(!tab5_echo_active());
    int16_t mic[B], spk[B], out[B];
#ifdef TAB5_ECHO_HOST_FRAME
    /* Frame bridge: output is the input, delayed by whole blocks, nothing lost or repeated. */
    assert(tab5_echo_open() && tab5_echo_active());
    size_t next = 0, expect = 0, silent = 0;   /* Sample counters; values wrap as int16. */
    for (unsigned block = 0; block < 400; ++block) {
        for (unsigned i = 0; i < B; ++i) { mic[i] = (int16_t)(next++ + 1); spk[i] = 0; }
        tab5_echo_process(mic, spk, out);
        for (unsigned i = 0; i < B; ++i) {
            if (expect == 0 && out[i] == 0) { ++silent; continue; }   /* Start-up silence. */
            assert(out[i] == (int16_t)(expect + 1)); ++expect;
        }
    }
    assert(silent <= TAB5_ECHO_HOST_FRAME + 2u * B && expect + silent == 400u * B);
    tab5_echo_close(); assert(!tab5_echo_active());
    printf("echo frame bridge (%u-sample frames): order kept, delay %u samples\n", TAB5_ECHO_HOST_FRAME, (unsigned)silent);
#else
    /* SpeexDSP: speaker noise reaches the mic through a short room response; after
     * converging, the echo-only output must be far quieter than the microphone. */
    assert(tab5_echo_open());
    static int16_t history[4096]; size_t at = 0;
    const int delay = 96;   /* 6 ms of buffering and air */
    unsigned seed = 1;
    double mic_e = 0, out_e = 0, near_e = 0, near_out_e = 0;
    for (unsigned block = 0; block < 1500; ++block) {
        const bool talk = block >= 1300;   /* last 2 s: the near end talks over silence */
        for (unsigned i = 0; i < B; ++i) {
            seed = seed * 1103515245u + 12345u;
            const int16_t s = talk ? 0 : (int16_t)((int)((seed >> 16) & 0x3fff) - 0x2000);
            spk[i] = s; history[at % 4096] = s;
            const int16_t d1 = history[(at + 4096 - delay) % 4096], d2 = history[(at + 4096 - delay - 37) % 4096];
            const double near = talk ? 3000.0 * sin(2 * 3.14159265358979 * 440.0 * at / TAB5_ECHO_RATE) : 0.0;
            mic[i] = (int16_t)(0.5 * d1 - 0.2 * d2 + near);
            ++at;
        }
        tab5_echo_process(mic, spk, out);
        if (block >= 1000 && block < 1300) { mic_e += energy(mic, B); out_e += energy(out, B); }
        if (block >= 1350) { near_e += energy(mic, B); near_out_e += energy(out, B); }
    }
    const double erle_db = 10 * log10(mic_e / (out_e + 1));
    const double near_db = 10 * log10((near_out_e + 1) / near_e);
    printf("speexdsp echo: %.1f dB of echo removed; near-end speech kept at %.1f dB\n", erle_db, near_db);
    assert(erle_db > 20.0);       /* Echo largely removed once converged. */
    assert(near_db > -10.0);      /* Talking over silence is not suppressed away. */
    tab5_echo_close();
#endif
}
