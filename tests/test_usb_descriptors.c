#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usb_descriptors.h"
static unsigned visits;
static void visit(const uint8_t *d, size_t n, void *ctx) { (void)d; (void)n; (void)ctx; visits++; }
int main(void)
{
    const uint8_t valid[] = {9,2,25,0,1,1,0,0x80,50, 9,4,0,0,1,0xff,0xfe,2,0, 7,5,0x81,2,64,0,0};
    assert(usb_descriptors_walk(valid, sizeof(valid), visit, NULL) && visits == 3);
    for (size_t n = 0; n < sizeof(valid); n++) assert(!usb_descriptors_walk(valid, n, NULL, NULL));
    uint8_t bad[sizeof(valid)];
    memcpy(bad, valid, sizeof(bad)); bad[9] = 0;
    visits = 0; assert(!usb_descriptors_walk(bad, sizeof(bad), visit, NULL) && visits == 0);
    memcpy(bad, valid, sizeof(bad)); bad[18] = 8;
    assert(!usb_descriptors_walk(bad, sizeof(bad), NULL, NULL));
    memcpy(bad, valid, sizeof(bad)); bad[18] = 6;
    assert(!usb_descriptors_walk(bad, sizeof(bad), NULL, NULL));
    memcpy(bad, valid, sizeof(bad)); bad[2] = 8;
    assert(!usb_descriptors_walk(bad, sizeof(bad), NULL, NULL));
    assert(!usb_descriptors_walk(NULL, 10, NULL, NULL));
    /* Deterministic malformed streams exercise bounds under ASan/UBSan. */
    uint32_t rng = 1;
    for (unsigned k = 0; k < 100000; ++k) {
        for (unsigned j = 0; j < sizeof(bad); j++) { rng = rng * 1664525u + 1013904223u; bad[j] = rng >> 24; }
        if (k & 1) { bad[0] = 9; bad[1] = 2; bad[2] = sizeof(bad); bad[3] = 0; }
        usb_descriptors_walk(bad, sizeof(bad), NULL, NULL);
    }
    puts("PASS: valid configuration, truncation, malformed lengths, atomic dispatch, 100000 fuzz inputs");
}
