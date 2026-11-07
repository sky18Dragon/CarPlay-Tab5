#include "airplay_control_cipher.h"
#include "airplay_screen.h"
#include "mbedtls/chachapoly.h"
#include "rtsp_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned callbacks;
static unsigned requests;
static bool rtsp_receive(const airplay_rtsp_request_t *r,void *ctx) {
 (void)ctx;
 assert(!strcmp(r->method,"POST") && !strcmp(r->path,"/command") && !strcmp(r->cseq,"1"));
 assert(r->body_len==0); ++requests; return true;
}
static esp_err_t receive(airplay_screen_message_type_t type,uint8_t *p,size_t n,size_t prefix,uint64_t timestamp,uint64_t arrival,void *ctx) {
 (void)prefix;(void)timestamp;(void)arrival;(void)ctx;
 assert(type==AIRPLAY_SCREEN_MESSAGE_FRAME_H264 && n==6 && !memcmp(p,"\0\0\0\1\x65\x88",6));
 ++callbacks;return ESP_OK;
}
int main(void) {
 uint8_t key[32]={0},wire[100],out[100]; size_t wire_len,used,output;
 const uint8_t text[]="POST /command RTSP/1.0\r\nCSeq: 1\r\n\r\n";
 for(size_t split=0;split<sizeof(text)-1+18;++split) {
  airplay_control_cipher_t *tx=NULL,*rx=NULL;
  assert(airplay_control_cipher_create(key,key,&tx)==0 && airplay_control_cipher_create(key,key,&rx)==0);
  assert(airplay_control_cipher_encrypt(tx,text,sizeof(text)-1,wire,sizeof(wire),&wire_len)==0);
  assert(split<wire_len);
  assert(airplay_control_cipher_decrypt(rx,wire,split,&used,out,sizeof(out),&output)==
         (split ? AIRPLAY_CONTROL_CIPHER_INCOMPLETE : AIRPLAY_CONTROL_CIPHER_OK));
  assert(used==split && output==0);
  assert(airplay_control_cipher_decrypt(rx,wire+split,wire_len-split,&used,out,sizeof(out),&output)==0);
  assert(output==sizeof(text)-1 && !memcmp(out,text,output));
  static uint8_t rtsp_buffer[CP_RTSP_CAPACITY]; cp_rtsp_stream rtsp;
  assert(cp_rtsp_init(&rtsp,rtsp_buffer,sizeof(rtsp_buffer))); requests=0;
  for(size_t i=0;i<output;++i) assert(cp_rtsp_feed(&rtsp,out+i,1,rtsp_receive,NULL));
  assert(requests==1 && rtsp.used==0);
  assert(airplay_control_cipher_decrypt(rx,wire,wire_len,&used,out,sizeof(out),&output)==AIRPLAY_CONTROL_CIPHER_AUTH_FAILED);
  assert(airplay_control_cipher_decrypt(rx,wire,wire_len,&used,out,sizeof(out),&output)==AIRPLAY_CONTROL_CIPHER_FAILED_STATE);
  airplay_control_cipher_destroy(tx);airplay_control_cipher_destroy(rx);
 }
 assert(airplay_screen_crypto_self_test());
 uint8_t header[128]={22},plain[]={0,0,0,2,0x65,0x88},body[22],saved[22],nonce[12]={0};
 mbedtls_chachapoly_context ctx;mbedtls_chachapoly_init(&ctx);
 assert(!mbedtls_chachapoly_setkey(&ctx,key));
 assert(!mbedtls_chachapoly_encrypt_and_tag(&ctx,6,nonce,header,128,plain,body,body+6));
 memcpy(saved,body,sizeof(body));
 airplay_screen_stream_t *s=airplay_screen_stream_create(key,1024,receive,NULL);assert(s);
 assert(airplay_screen_stream_process(s,header,body,21,0,0)==ESP_ERR_INVALID_SIZE && !callbacks);
 body[21]^=1;
 assert(airplay_screen_stream_process(s,header,body,22,0,0)==ESP_ERR_INVALID_MAC && !callbacks);
 memcpy(body,saved,22);
 assert(airplay_screen_stream_process(s,header,body,22,0,0)==ESP_OK && callbacks==1);
 memcpy(body,saved,22);
 assert(airplay_screen_stream_process(s,header,body,22,0,0)==ESP_ERR_INVALID_MAC && callbacks==1);
 assert(airplay_screen_stream_frame_counter(s)==1 && airplay_screen_stream_auth_failures(s)==2);
 airplay_screen_stream_destroy(s);mbedtls_chachapoly_free(&ctx);
 puts("crypto: control all splits/replay/poisoning, screen HKDF/tamper/truncation/replay passed");
}
