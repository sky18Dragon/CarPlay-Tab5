#include "core/video_decoder.h"
#include "core/video_queue.h"
#include "core/touch.h"
#include "core/hid_report.h"
extern "C" {
#include "airplay_screen_codec.h"
}
#include "../assets/high_ltr_crc.h"
#include <assert.h>
#include <stdio.h>
#include <fstream>
#include <vector>
#include <iterator>
static uint32_t crc_frame(const cp_i420 &f) {
    uint32_t crc=~0u;
    for(unsigned p=0;p<3;++p) for(unsigned y=0;y<(p?(f.height+1)/2:f.height);++y)
        for(unsigned x=0;x<(p?(f.width+1)/2:f.width);++x) {
            crc^=f.planes[p][y*f.stride[p]+x];
            for(unsigned b=0;b<8;++b) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));
        }
    return ~crc;
}
static std::vector<uint8_t> load(const char *path) {
    std::ifstream file(path,std::ios::binary); assert(file.good());
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
int main(int argc,char **argv) {
    assert(argc==3);
    cp_decoder *d=cp_decoder_create(640,384); assert(d);
    auto high=load(argv[1]); cp_i420 f;
    assert(cp_decoder_decode(d,high.data(),high.size(),0,&f)==CP_DECODE_FRAME);
    assert(f.width==160 && f.height==96 && crc_frame(f)==0xf0cd6fba);
    std::vector<uint16_t> rgb(320*192);
    assert(cp_i420_rgb565(&f,rgb.data(),rgb.size(),320,192));
    assert(!cp_i420_rgb565(&f,rgb.data(),10,320,192));
    // Simulate an already authenticated Screen DataStream payload, using the
    // production framing/conversion/queue/decoder/coordinate modules together.
    std::vector<uint8_t> wire;
    std::vector<size_t> starts, lengths;
    for(size_t i=0;i+3<high.size();++i) {
        size_t prefix=0;
        if(high[i]==0 && high[i+1]==0 && high[i+2]==1) prefix=3;
        else if(i+4<high.size() && high[i]==0 && high[i+1]==0 && high[i+2]==0 && high[i+3]==1) prefix=4;
        if(prefix) { starts.push_back(i); lengths.push_back(prefix); i+=prefix-1; }
    }
    assert(!starts.empty());
    for(size_t i=0;i<starts.size();++i) {
        size_t begin=starts[i]+lengths[i],end=i+1<starts.size()?starts[i+1]:high.size(),n=end-begin;
        wire.push_back(n>>24);wire.push_back(n>>16);wire.push_back(n>>8);wire.push_back(n);
        wire.insert(wire.end(),high.begin()+begin,high.begin()+end);
    }
    uint8_t header[128]={};
    for(unsigned i=0;i<4;++i) header[i]=wire.size()>>(8*i);
    airplay_screen_header_info_t info;
    assert(airplay_screen_parse_header(header,wire.size(),&info));
    assert(airplay_screen_convert_length_prefixed_to_annex_b(wire.data(),wire.size()));
    std::vector<uint8_t> queue_memory(512*1024),au(512*1024);
    cp_video_queue q;assert(cp_video_init(&q,queue_memory.data(),queue_memory.size()));cp_video_reset(&q,41);
    assert(cp_video_push(&q,41,wire.data(),wire.size(),123,true));
    cp_video_unit unit;assert(cp_video_pop(&q,41,au.data(),au.size(),&unit));
    assert(cp_decoder_reset(d));
    assert(cp_decoder_decode(d,au.data(),unit.size,unit.pts,&f)==CP_DECODE_FRAME);
    assert(crc_frame(f)==0xf0cd6fba);
    cp_touch touch={};assert(cp_touch_configure(&touch,1280,720,f.width,f.height,f.width,f.height));
    auto event=cp_touch_update(&touch,true,touch.x+touch.width-1,touch.y+touch.height-1);
    uint8_t report[12];cp_hid_report(event,report);
    assert(report[1]==1 && (report[2]|report[3]<<8)==159 && (report[4]|report[5]<<8)==95);
    cp_hid_report(cp_touch_cancel(&touch),report);assert(report[1]==0);
    cp_video_reset(&q,42);assert(!cp_video_push(&q,41,wire.data(),wire.size(),123,true));
    assert(cp_decoder_reset(d));
    auto ltr=load(argv[2]);
    for(unsigned pass=0;pass<8;++pass) {
        size_t pos=0; unsigned count=0;
        while(pos<ltr.size()) {
            assert(ltr.size()-pos>=4);
            size_t n=(uint32_t(ltr[pos])<<24)|(uint32_t(ltr[pos+1])<<16)|(uint32_t(ltr[pos+2])<<8)|ltr[pos+3]; pos+=4;
            assert(n && n<=ltr.size()-pos && count<4);
            assert(cp_decoder_decode(d,ltr.data()+pos,n,count,&f)==CP_DECODE_FRAME);
            assert(crc_frame(f)==high_ltr_crc[count]); ++count; pos+=n;
        }
        assert(count==4);
    }
    cp_decoder_destroy(d);
    uint8_t y[]={16,235,81,145},u[]={128},v[]={128};
    f={}; f.width=f.height=2; f.planes[0]=y;f.planes[1]=u;f.planes[2]=v;
    f.stride[0]=2; f.stride[1]=f.stride[2]=1;
    assert(cp_i420_rgb565(&f,rgb.data(),4,2,2));
    assert(rgb[0]==0 && rgb[1]==65535);
    puts("decoder: High/LTR pixel CRC, Screen codec -> queue -> decode -> touch/HID integration passed");
}
