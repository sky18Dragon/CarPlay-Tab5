#include "core/video_decoder.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <chrono>
static uint32_t table[256];
static void init_table() { for(uint32_t i=0;i<256;++i) { uint32_t c=i; for(int b=0;b<8;++b) c=(c>>1)^(0xedb88320u&(0u-(c&1))); table[i]=c; } }
static uint32_t crc_frame(const cp_i420 &f) {
    uint32_t crc=~0u;
    for(unsigned p=0;p<3;++p) for(unsigned y=0;y<(p?(f.height+1)/2:f.height);++y)
        for(unsigned x=0;x<(p?(f.width+1)/2:f.width);++x) {
            crc^=f.planes[p][y*f.stride[p]+x];
            crc=(crc>>8)^table[crc&255];
        }
    return ~crc;
}
int main(int argc,char **argv) {
    (void)argc; init_table();
    FILE *in=fopen(argv[1],"rb"); if(!in) return 2;
    cp_decoder *d=cp_decoder_create(1280,720); if(!d) return 3;
    uint32_t size; std::vector<uint8_t> buf; uint64_t pts=0; int frames=0,errors=0; double spent=0;
    while(fread(&size,4,1,in)==1) {
        buf.resize(size+64); if(fread(buf.data(),1,size,in)!=size) return 4;
        cp_i420 f; auto t0=std::chrono::steady_clock::now();
        cp_decode_result r=cp_decoder_decode(d,buf.data(),size,pts++,&f);
        spent+=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
        if(r==CP_DECODE_FRAME) { printf("%d %ux%u %08x\n",frames++,f.width,f.height,crc_frame(f)); }
        else if(r==CP_DECODE_ERROR) { printf("ERR\n"); ++errors; }
    }
    fprintf(stderr,"frames=%d errors=%d decode_seconds=%.3f\n",frames,errors,spent);
    return 0;
}
