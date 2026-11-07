#include "video_decoder.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "soc/soc_caps.h"
#endif
#if defined(SOC_CPU_HAS_PIE) && SOC_CPU_HAS_PIE
#define HAVE_PIE 1
/* pixel_pie.S. Same results as the C loops below; aligned pointers only. */
void cp_pack_row_pie(uint8_t *out,const uint8_t *luma,const uint8_t *chroma,unsigned blocks);
void cp_sharpen_row_pie(uint8_t *out,const uint8_t *above,const uint8_t *row,const uint8_t *below,unsigned blocks,unsigned strength,unsigned limit);
#else
#define HAVE_PIE 0
#endif
/* Sharpening stops here, so that strong edges such as white text on a dark bar get
 * no bright halo. In the same units as the 8-bit luma it changes. */
#define SHARPEN_LIMIT 24
#define SHARPEN_MAX_WIDTH 1280
static unsigned clip(int v) { return v<0?0:v>255?255:(unsigned)v; }
bool cp_i420_rgb565(const cp_i420 *f,uint16_t *out,size_t capacity,unsigned w,unsigned h)
{
    if(!f || !out || !w || !h || w>1280 || h>720 || capacity<(size_t)w*h ||
       !f->width || !f->height || f->width>1280 || f->height>720) return false;
    for(unsigned p=0;p<3;++p)
        if(!f->planes[p] || f->stride[p]<(int)(p?(f->width+1)/2:f->width)) return false;
    /* Nearest-neighbour scaling repeats source pixels and rows, so each one is
     * converted once. All factors are at most 1280, which fits 32-bit products. */
    uint16_t column[1280];
    for(unsigned x=0;x<w;++x) column[x]=(uint16_t)(x*f->width/w);
    unsigned previous=0;
    for(unsigned y=0;y<h;++y) {
        unsigned sy=y*f->height/h;
        uint16_t *row=out+(size_t)y*w;
        if(y && sy==previous) { memcpy(row,row-w,w*sizeof(*row)); continue; }
        previous=sy;
        const uint8_t *luma=f->planes[0]+(size_t)sy*f->stride[0];
        const uint8_t *cb=f->planes[1]+(size_t)(sy/2)*f->stride[1];
        const uint8_t *cr=f->planes[2]+(size_t)(sy/2)*f->stride[2];
        for(unsigned x=0;x<w;++x) {
            unsigned sx=column[x];
            if(x && sx==column[x-1]) { row[x]=row[x-1]; continue; }
            int l=(int)luma[sx]-16,u=(int)cb[sx/2]-128,v=(int)cr[sx/2]-128;
            /* Division, not signed shifts, gives portable negative rounding. */
            unsigned r=clip((298*l+409*v+128)/256);
            unsigned g=clip((298*l-100*u-208*v+128)/256);
            unsigned b=clip((298*l+516*u+128)/256);
            row[x]=(uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
        }
    }
    return true;
}
/* The picture is stretched more across rows than along them, so detail is lost
 * between rows: this is an unsharp mask against the rows above and below. The
 * average drops each low bit, as the vector multiply does. */
static void sharpen_row(uint8_t *out,const uint8_t *above,const uint8_t *row,const uint8_t *below,unsigned width,unsigned strength)
{
    unsigned x=0;
#if HAVE_PIE
    if(!(((uintptr_t)out|(uintptr_t)above|(uintptr_t)row|(uintptr_t)below)&15)) {
        cp_sharpen_row_pie(out,above,row,below,width/16,strength,SHARPEN_LIMIT);
        x=width/16*16;
    }
#endif
    for(;x<width;++x) {
        const int average=(above[x]>>1)+(below[x]>>1),difference=row[x]-average;
        unsigned change=(unsigned)(difference<0?-difference:difference)*strength>>8;
        if(change>SHARPEN_LIMIT) change=SHARPEN_LIMIT;
        out[x]=(uint8_t)clip(row[x]+(difference<0?-(int)change:(int)change));
    }
}
static uint8_t *pack_row(uint8_t *out,const uint8_t *luma,const uint8_t *chroma,unsigned width)
{
    unsigned x=0;
#if HAVE_PIE
    if(!(((uintptr_t)luma|(uintptr_t)chroma|(uintptr_t)out)&15)) {
        cp_pack_row_pie(out,luma,chroma,width/32);
        x=width/32*32; out+=width/32*48;
    }
#endif
    /* Three words at a time: most of the cost of byte stores was in the PSRAM.
     * Rows from the decoder are word aligned. */
    if(!(((uintptr_t)luma|(uintptr_t)chroma|(uintptr_t)out)&3)) {
        uint32_t *word=(uint32_t *)out;
        for(;x+8<=width;x+=8,word+=3) {
            const uint32_t y0=*(const uint32_t *)(luma+x),y1=*(const uint32_t *)(luma+x+4);
            const uint32_t c=*(const uint32_t *)(chroma+x/2);
            word[0]=(c&0xff)|(y0&0xffff)<<8|(c&0xff00)<<16;
            word[1]=y0>>16|(c&0xff0000)|y1<<24;
            word[2]=(y1>>8&0xff)|(c>>24)<<8|(y1&0xffff0000);
        }
        out=(uint8_t *)word;
    }
    for(;x<width;x+=2) {
        *out++=chroma[x/2]; *out++=luma[x]; *out++=luma[x+1];
    }
    return out;
}
bool cp_i420_pack(const cp_i420 *f,uint8_t *out,size_t capacity,unsigned sharpen)
{
    if(!f || !out || !f->width || !f->height || (f->width&1) || (f->height&1) ||
       capacity<(size_t)f->width*f->height*3/2 || sharpen>255 ||
       (sharpen && f->width>SHARPEN_MAX_WIDTH)) return false;
    for(unsigned p=0;p<3;++p)
        if(!f->planes[p] || f->stride[p]<(int)(p?f->width/2:f->width)) return false;
    /* One task packs at a time. In memory that is not behind the PSRAM cache. */
    static uint8_t sharpened[SHARPEN_MAX_WIDTH] __attribute__((aligned(16)));
    for(unsigned y=0;y<f->height;++y) {
        const uint8_t *luma=f->planes[0]+(size_t)y*f->stride[0];
        const uint8_t *chroma=f->planes[y&1?2:1]+(size_t)(y/2)*f->stride[y&1?2:1];
        if(sharpen && y && y+1<f->height) {
            sharpen_row(sharpened,luma-f->stride[0],luma,luma+f->stride[0],f->width,sharpen);
            luma=sharpened;
        }
        out=pack_row(out,luma,chroma,f->width);
    }
    return true;
}
