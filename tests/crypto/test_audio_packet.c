#include "airplay_audio_packet.h"
#include "pcm_buffer.h"
#include "mbedtls/chachapoly.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t key[32]={1,2,3};
static const uint8_t pcm[]={0x00,0x01,0x7f,0xff,0x80,0x00,0xff,0xff};
static void encrypt_packet(uint8_t *wire,size_t payload)
{
    memset(wire,0,44);
    wire[0]=0x80; wire[1]=0x60; wire[2]=0xff; wire[3]=0xfe;
    wire[4]=0xff; wire[5]=0xff; wire[6]=0xff; wire[7]=0xfe;
    wire[8]=1; wire[9]=2; wire[10]=3; wire[11]=4;
    uint8_t nonce[12]={0}; nonce[4]=7;
    memcpy(wire+12+payload+16,nonce+4,8);
    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    assert(!mbedtls_chachapoly_setkey(&ctx,key));
    assert(!mbedtls_chachapoly_encrypt_and_tag(&ctx,payload,nonce,wire+4,8,pcm,wire+12,wire+12+payload));
    mbedtls_chachapoly_free(&ctx);
}
int main(void)
{
    uint8_t wire[44],saved[44],wrong_key[32]={0};
    airplay_audio_packet out;
    for(unsigned channels=1;channels<=2;++channels) {
        encrypt_packet(saved,8); memcpy(wire,saved,44);
        assert(airplay_audio_packet_decode(key,channels,wire,44,&out)==AIRPLAY_AUDIO_PACKET_OK);
        assert(out.sequence==65534 && out.timestamp==UINT32_MAX-1 && out.size==8);
        assert(out.pcm==wire+12 && !memcmp(out.pcm,pcm,8));
        uint8_t samples[32],valid[8],played[8]; cp_pcm buffer;
        assert(cp_pcm_init(&buffer,samples,valid,8,48000,channels,1)); cp_pcm_reset(&buffer,7);
        assert(cp_pcm_push(&buffer,7,out.timestamp,out.pcm,out.size,true));
        assert(cp_pcm_read(&buffer,7,played,4/channels)==4/channels);
        const uint8_t expected[]={1,0,0xff,0x7f,0,0x80,0xff,0xff};
        assert(!memcmp(played,expected,8));
    }
    /* Authenticate timestamp/SSRC, ciphertext, tag and nonce before exposing PCM. */
    const unsigned tamper[]={4,8,12,20,36};
    for(unsigned i=0;i<sizeof(tamper)/sizeof(tamper[0]);++i) {
        memcpy(wire,saved,44); wire[tamper[i]]^=1;
        memset(&out,0xff,sizeof(out));
        assert(airplay_audio_packet_decode(key,2,wire,44,&out)==AIRPLAY_AUDIO_PACKET_AUTH_ERROR);
        assert(!out.pcm && !out.size && !out.timestamp && !out.sequence);
    }
    memcpy(wire,saved,44);
    assert(airplay_audio_packet_decode(wrong_key,2,wire,44,&out)==AIRPLAY_AUDIO_PACKET_AUTH_ERROR);
    for(size_t size=0;size<44;++size) {
        memcpy(wire,saved,44);
        assert(airplay_audio_packet_decode(key,2,wire,size,&out)!=AIRPLAY_AUDIO_PACKET_OK);
        assert(!out.pcm && !out.size);
    }
    memcpy(wire,saved,44); wire[0]=0x90; /* unsupported RTP extension */
    assert(airplay_audio_packet_decode(key,2,wire,44,&out)==AIRPLAY_AUDIO_PACKET_FORMAT_ERROR);
    assert(airplay_audio_packet_decode(key,3,wire,44,&out)==AIRPLAY_AUDIO_PACKET_FORMAT_ERROR);
    assert(airplay_audio_packet_decode(key,2,wire,4097,&out)==AIRPLAY_AUDIO_PACKET_FORMAT_ERROR);
    assert(airplay_audio_packet_decode(NULL,2,wire,44,&out)==AIRPLAY_AUDIO_PACKET_FORMAT_ERROR);
    assert(airplay_audio_packet_decode(key,2,NULL,44,&out)==AIRPLAY_AUDIO_PACKET_FORMAT_ERROR);
    assert(airplay_audio_packet_decode(key,2,wire,44,NULL)==AIRPLAY_AUDIO_PACKET_FORMAT_ERROR);
    /* A valid 6-byte mono payload cannot be accepted as stereo. */
    encrypt_packet(wire,6);
    assert(airplay_audio_packet_decode(key,2,wire,42,&out)==AIRPLAY_AUDIO_PACKET_FORMAT_ERROR);
    assert(airplay_audio_packet_decode(key,1,wire,42,&out)==AIRPLAY_AUDIO_PACKET_OK);
    puts("audio RTP: authenticated PCM to playback buffer, mono/stereo, endian, truncation, tamper and format rejection passed");
}
