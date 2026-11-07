#include "core/video_decoder.h"
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <zlib.h>
int main(int,char **argv) {
    FILE *in=fopen(argv[1],"rb"); if(!in) return 2;
    cp_decoder *d=cp_decoder_create(1280,720); if(!d) return 3;
    uint32_t size,frames=0; std::vector<uint8_t> buf; uint64_t pts=0; uLong crc=0;
    while(fread(&size,4,1,in)==1) {
        buf.resize(size+64); if(fread(buf.data(),1,size,in)!=size) return 4;
        cp_i420 f;
        cp_decode_result r=cp_decoder_decode(d,buf.data(),size,pts++,&f);
        if(r==CP_DECODE_FRAME) { ++frames;
            for(unsigned k=0;k<3;++k) { unsigned h=k?f.height/2:f.height,w=k?f.width/2:f.width;
                for(unsigned y=0;y<h;++y) crc=crc32(crc,f.planes[k]+y*f.stride[k],w); }
            printf("C %d %d %08lx\n",(int)pts-1,(int)frames-1,(unsigned long)crc); }
        else if(r==CP_DECODE_ERROR) printf("E %d\n",(int)pts-1);
    }
    printf("frames=%u crc=%08lx\n",frames,(unsigned long)crc); return 0;
}
