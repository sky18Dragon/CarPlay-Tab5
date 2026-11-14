#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    AIRPLAY_AUDIO_PACKET_OK,
    AIRPLAY_AUDIO_PACKET_FORMAT_ERROR,
    AIRPLAY_AUDIO_PACKET_AUTH_ERROR,
} airplay_audio_packet_result;

typedef struct {
    uint16_t sequence;
    uint32_t timestamp;
    uint8_t *pcm; /* authenticated network-order 16-bit PCM, owned by caller */
    size_t size;
} airplay_audio_packet;

/* RTP fixed header + ciphertext + 16-byte tag + 8-byte nonce suffix.
 * Decrypts in place. On any error out is cleared and no PCM is exposed. */
airplay_audio_packet_result airplay_audio_packet_decode(
    const uint8_t key[32], unsigned channels, uint8_t *wire, size_t size,
    airplay_audio_packet *out);
