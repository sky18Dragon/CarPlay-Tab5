#include "core/video_decoder.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned clip(int v) { return v<0?0:v>255?255:(unsigned)v; }
/* Straightforward per-pixel definition; the production loop must match it exactly. */
static uint16_t reference(const cp_i420 *f,unsigned x,unsigned y,unsigned w,unsigned h)
{
    unsigned sy=(uint64_t)y*f->height/h,sx=(uint64_t)x*f->width/w;
    int l=(int)f->planes[0][(size_t)sy*f->stride[0]+sx]-16;
    int u=(int)f->planes[1][(size_t)(sy/2)*f->stride[1]+sx/2]-128;
    int v=(int)f->planes[2][(size_t)(sy/2)*f->stride[2]+sx/2]-128;
    unsigned r=clip((298*l+409*v+128)/256),g=clip((298*l-100*u-208*v+128)/256),b=clip((298*l+516*u+128)/256);
    return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}
static void check(unsigned fw,unsigned fh,unsigned w,unsigned h)
{
    int stride=(int)fw+7,chroma=(int)(fw+1)/2+3;
    size_t ysize=(size_t)stride*fh,csize=(size_t)chroma*((fh+1)/2);
    uint8_t *yp=malloc(ysize),*up=malloc(csize),*vp=malloc(csize);
    uint16_t *out=malloc((size_t)w*h*2);
    assert(yp && up && vp && out);
    for(size_t i=0;i<ysize;++i) yp[i]=(uint8_t)(i*31+(i>>5));
    for(size_t i=0;i<csize;++i) { up[i]=(uint8_t)(i*17+3); vp[i]=(uint8_t)(i*53+11); }
    cp_i420 f={0}; f.width=fw; f.height=fh;
    f.planes[0]=yp; f.planes[1]=up; f.planes[2]=vp;
    f.stride[0]=stride; f.stride[1]=f.stride[2]=chroma;
    assert(cp_i420_rgb565(&f,out,(size_t)w*h,w,h));
    for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) assert(out[(size_t)y*w+x]==reference(&f,x,y,w,h));
    free(yp); free(up); free(vp); free(out);
}
/* An extra of 0 keeps rows word aligned, which takes the fast path. */
static void check_pack(unsigned w,unsigned h,int extra)
{
    int stride=(int)w+extra*7,chroma=(int)w/2+extra*3;
    if(!extra) { stride=(int)(w+15)&~15; chroma=stride/2; }
    size_t ysize=(size_t)stride*h,csize=(size_t)chroma*(h/2),size=(size_t)w*h*3/2;
    uint8_t *yp=malloc(ysize),*up=malloc(csize),*vp=malloc(csize),*out=malloc(size);
    assert(yp && up && vp && out);
    for(size_t i=0;i<ysize;++i) yp[i]=(uint8_t)(i*31+(i>>5));
    for(size_t i=0;i<csize;++i) { up[i]=(uint8_t)(i*17+3); vp[i]=(uint8_t)(i*53+11); }
    cp_i420 f={0}; f.width=w; f.height=h;
    f.planes[0]=yp; f.planes[1]=up; f.planes[2]=vp;
    f.stride[0]=stride; f.stride[1]=f.stride[2]=chroma;
    assert(!cp_i420_pack(&f,out,size-1,0));
    assert(cp_i420_pack(&f,out,size,0));
    const uint8_t *p=out;
    for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;x+=2) {
        const uint8_t *c=y&1?vp:up;
        assert(*p++==c[(size_t)(y/2)*chroma+x/2]);
        assert(*p++==yp[(size_t)y*stride+x]);
        assert(*p++==yp[(size_t)y*stride+x+1]);
    }
    assert(p==out+size);
    f.width=w-1; assert(!cp_i420_pack(&f,out,size,0));
    f.width=w; f.height=h-1; assert(!cp_i420_pack(&f,out,size,0));
    free(yp); free(up); free(vp); free(out);
}
/* Per-pixel definition of the sharpened luma; the first and last rows stay as they are. */
static unsigned sharpened(const uint8_t *yp,int stride,unsigned x,unsigned y,unsigned h,unsigned k)
{
    const int c=yp[(size_t)y*stride+x];
    if(!y || y+1==h) return (unsigned)c;
    const int d=c-((yp[(size_t)(y-1)*stride+x]>>1)+(yp[(size_t)(y+1)*stride+x]>>1));
    unsigned m=(unsigned)abs(d)*k>>8;
    if(m>24) m=24;
    return clip(d<0?c-(int)m:c+(int)m);
}
static void check_sharpen(unsigned w,unsigned h,unsigned k)
{
    int stride=(int)(w+15)&~15,chroma=stride/2;
    size_t ysize=(size_t)stride*h,csize=(size_t)chroma*(h/2),size=(size_t)w*h*3/2;
    uint8_t *yp=malloc(ysize),*up=malloc(csize),*vp=malloc(csize),*out=malloc(size);
    assert(yp && up && vp && out);
    for(size_t i=0;i<ysize;++i) yp[i]=(uint8_t)(i%5==0?0:i%7==0?255:(i*31+(i>>5)));
    for(size_t i=0;i<csize;++i) { up[i]=(uint8_t)(i*17+3); vp[i]=(uint8_t)(i*53+11); }
    cp_i420 f={0}; f.width=w; f.height=h;
    f.planes[0]=yp; f.planes[1]=up; f.planes[2]=vp;
    f.stride[0]=stride; f.stride[1]=f.stride[2]=chroma;
    assert(cp_i420_pack(&f,out,size,k));
    const uint8_t *p=out;
    for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;x+=2) {
        const uint8_t *c=y&1?vp:up;
        assert(*p++==c[(size_t)(y/2)*chroma+x/2]);
        assert(*p++==sharpened(yp,stride,x,y,h,k));
        assert(*p++==sharpened(yp,stride,x+1,y,h,k));
    }
    assert(!cp_i420_pack(&f,out,size,256));
    free(yp); free(up); free(vp); free(out);
}
int main(void)
{
    check_sharpen(1024,240,128); check_sharpen(1024,192,255); check_sharpen(10,4,64); check_sharpen(16,2,128);
    check_pack(852,480,1); check_pack(2,2,1);
    check_pack(852,480,0); check_pack(1024,576,0); check_pack(10,4,0);
    check(320,192,1200,720); /* Tab5 upscale */
    check(320,192,320,192);  /* 1:1 */
    check(640,384,1200,720);
    check(1280,720,640,360); /* downscale */
    check(1280,720,1280,720);
    check(321,193,977,613);  /* odd, non-integer ratios */
    check(2,2,1,1);
    puts("pixel convert: scaled output matches per-pixel reference");
}
