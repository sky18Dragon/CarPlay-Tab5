#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct cp_decoder cp_decoder;
typedef struct {
    const uint8_t *planes[3]; int stride[3]; unsigned width,height; uint64_t pts;
} cp_i420;
typedef enum { CP_DECODE_ERROR=-1,CP_DECODE_PENDING=0,CP_DECODE_FRAME=1 } cp_decode_result;
/* Single decoder worker owns handle and plane views, valid until next call. */
cp_decoder *cp_decoder_create(unsigned max_width,unsigned max_height);
void cp_decoder_destroy(cp_decoder *);
bool cp_decoder_reset(cp_decoder *);
cp_decode_result cp_decoder_decode(cp_decoder *,const uint8_t *annex_b,size_t,uint64_t pts,cp_i420 *);
/* With a sink, cp_decoder_decode returns once the frame has been parsed, and the
 * sink gets each finished frame in order: on the target on a helper task on the
 * parsing core, elsewhere before the decode call returns. The plane views stay valid
 * and unchanged until the sink returns. Reconstruction of later frames continues
 * meanwhile and waits only before writing into the picture the sink is reading, so
 * the sink must not touch any other picture. One sink call runs at a time. Without a
 * sink the frame is finished when cp_decoder_decode returns. */
typedef void (*cp_frame_sink)(const cp_i420 *,void *context);
void cp_decoder_set_sink(cp_decoder *,cp_frame_sink,void *context);
/* Waits for every frame that has been accepted to reach the sink. */
void cp_decoder_wait(cp_decoder *);
/* RGB565 in native-endian words, nearest-neighbour scaling, BT.601 limited range. */
/* Packs a frame for the PPA, which converts it to RGB in hardware: odd lines hold
 * U Y Y, even lines V Y Y, 12 bits per pixel. Width and height must be even.
 * sharpen is 0 to 255, in 1/256ths of the luma difference from the rows around it.
 * Only one call may run at a time. */
bool cp_i420_pack(const cp_i420 *,uint8_t *output,size_t capacity_bytes,unsigned sharpen);
bool cp_i420_rgb565(const cp_i420 *,uint16_t *output,size_t capacity_pixels,unsigned width,unsigned height);
#ifdef __cplusplus
}
#endif
