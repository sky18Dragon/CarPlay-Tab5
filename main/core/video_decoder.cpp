#include "video_decoder.h"
#include "wels/codec_api.h"
#include <new>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "tab5_recon_queue.h"
#endif
namespace {
constexpr size_t max_parameter_bytes=256;
}
struct cp_decoder {
    ISVCDecoder *codec=nullptr; unsigned width,height; uint8_t *input=nullptr;
    cp_frame_sink sink=nullptr; void *sink_context=nullptr;
    uint8_t parameters[max_parameter_bytes]; size_t parameter_bytes=0;
};
#ifdef ESP_PLATFORM
namespace {
struct delivery { cp_i420 frame; cp_frame_sink sink; void *context; };
static_assert(sizeof(delivery)<=TAB5_RECON_PAYLOAD_BYTES);
void deliver(const void *payload)
{
    delivery item; memcpy(&item,payload,sizeof(item));
    item.sink(&item.frame,item.context);
}
/* iOS repeats its SPS and PPS before every frame. Upstream leaves a repeat alone,
 * but different ones overwrite what queued reconstruction is reading. */
bool parameters_changed(cp_decoder *d,const uint8_t *p,size_t size)
{
    size_t span=0;
    for(;span+3<size;++span) {
        if(p[span] || p[span+1] || p[span+2]!=1) continue;
        const unsigned type=p[span+3]&31;
        if(type==1 || type==5) break;
    }
    if(span+3>=size) span=size;
    if(span<=1) return false;
    const bool changed=span!=d->parameter_bytes || span>max_parameter_bytes || memcmp(d->parameters,p,span)!=0;
    if(changed) {
        d->parameter_bytes=span;
        if(span<=max_parameter_bytes) memcpy(d->parameters,p,span);
    }
    return changed;
}
}
#endif
void cp_decoder_set_sink(cp_decoder *d,cp_frame_sink sink,void *context)
{
    if(!d) return;
    cp_decoder_wait(d);
    d->sink=sink; d->sink_context=context;
}
void cp_decoder_wait(cp_decoder *d)
{
    (void)d;
#ifdef ESP_PLATFORM
    tab5_recon_drain();
#endif
}
bool cp_decoder_reset(cp_decoder *d)
{
    if(!d) return false;
    cp_decoder_wait(d);
#ifdef ESP_PLATFORM
    tab5_recon_clear_failure();
#endif
    d->parameter_bytes=0;
    if(d->codec) { d->codec->Uninitialize(); WelsDestroyDecoder(d->codec); d->codec=nullptr; }
    if(WelsCreateDecoder(&d->codec)!=0 || !d->codec) return false;
    int threads=0,trace=WELS_LOG_ERROR;
    d->codec->SetOption(DECODER_OPTION_NUM_OF_THREADS,&threads);
    d->codec->SetOption(DECODER_OPTION_TRACE_LEVEL,&trace);
    SDecodingParam cfg={}; cfg.sVideoProperty.eVideoBsType=VIDEO_BITSTREAM_AVC;
    cfg.uiTargetDqLayer=255; cfg.sVideoProperty.size=sizeof(cfg.sVideoProperty);
    cfg.eEcActiveIdc=ERROR_CON_DISABLE;
    if(d->codec->Initialize(&cfg)==0) return true;
    WelsDestroyDecoder(d->codec); d->codec=nullptr; return false;
}
cp_decoder *cp_decoder_create(unsigned w,unsigned h)
{
    if(!w || !h || w>1280 || h>720) return nullptr;
    auto *d=new(std::nothrow) cp_decoder;
    if(!d) return nullptr;
    d->width=w; d->height=h; d->input=static_cast<uint8_t *>(malloc(512*1024+64));
    if(!d->input || !cp_decoder_reset(d)) { cp_decoder_destroy(d); return nullptr; }
    return d;
}
void cp_decoder_destroy(cp_decoder *d)
{
    if(!d) return;
    cp_decoder_wait(d);
#ifdef ESP_PLATFORM
    tab5_recon_clear_failure();
#endif
    if(d->codec) { d->codec->Uninitialize(); WelsDestroyDecoder(d->codec); }
    free(d->input); delete d;
}
cp_decode_result cp_decoder_decode(cp_decoder *d,const uint8_t *data,size_t size,uint64_t pts,cp_i420 *frame)
{
    if(frame) memset(frame,0,sizeof(*frame));
    if(!d || !d->codec || !data || !size || size>512*1024 || !frame) return CP_DECODE_ERROR;
#ifdef ESP_PLATFORM
    if(tab5_recon_failed()) return CP_DECODE_ERROR;
    if(parameters_changed(d,data,size)) tab5_recon_drain();
#endif
    memcpy(d->input,data,size); memset(d->input+size,0,64);
    uint8_t *planes[3]={}; SBufferInfo info={}; info.uiInBsTimeStamp=pts;
    int rc=d->codec->DecodeFrame2(d->input,static_cast<int>(size),planes,&info);
    if(!info.iBufferStatus && rc==dsErrorFree) rc|=d->codec->DecodeFrame2(nullptr,0,planes,&info);
    if(!info.iBufferStatus && rc==dsErrorFree) rc|=d->codec->FlushFrame(planes,&info);
    if(rc!=dsErrorFree) return CP_DECODE_ERROR;
    if(!info.iBufferStatus) return CP_DECODE_PENDING;
    const auto &s=info.UsrData.sSystemBuffer;
    if(s.iWidth<=0 || s.iHeight<=0 || unsigned(s.iWidth)>d->width || unsigned(s.iHeight)>d->height) return CP_DECODE_ERROR;
    frame->width=s.iWidth; frame->height=s.iHeight; frame->pts=info.uiOutYuvTimeStamp;
    for(unsigned p=0;p<3;++p) {
        frame->planes[p]=planes[p]; frame->stride[p]=s.iStride[p?1:0];
        if(!planes[p] || frame->stride[p]<int(p?(frame->width+1)/2:frame->width)) return CP_DECODE_ERROR;
    }
#ifdef ESP_PLATFORM
    if(d->sink) {
        const delivery item={*frame,d->sink,d->sink_context};
        if(tab5_recon_post(deliver,&item,sizeof(item))) return CP_DECODE_FRAME;
    }
    tab5_recon_drain();
#endif
    if(d->sink) d->sink(frame,d->sink_context);
    return CP_DECODE_FRAME;
}
