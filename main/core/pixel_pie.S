/* PIE versions of the two row loops in pixel_convert.c, which documents them and
 * holds the C equivalents. All pointers must be 16-byte aligned. */
#include "soc/soc_caps.h"
#if SOC_CPU_HAS_PIE
    .text
    .align 4

/* void cp_pack_row_pie(uint8_t *out,const uint8_t *luma,const uint8_t *chroma,unsigned blocks)
 * One block is 32 pixels: 32 luma and 16 chroma bytes in, 48 bytes out. */
    .global cp_pack_row_pie
    .type cp_pack_row_pie,@function
cp_pack_row_pie:
    beqz a3,2f
1:  esp.vld.128.ip q0,a1,16
    esp.vld.128.ip q1,a1,16
    esp.vld.128.ip q2,a2,16
    esp.vunzip.8 q0,q1
    esp.vzipt.8 q2,q0,q1
    esp.vst.128.ip q2,a0,16
    esp.vst.128.ip q0,a0,16
    esp.vst.128.ip q1,a0,16
    addi a3,a3,-1
    bnez a3,1b
2:  ret
    .size cp_pack_row_pie,.-cp_pack_row_pie

/* void cp_sharpen_row_pie(uint8_t *out,const uint8_t *above,const uint8_t *row,
 *                         const uint8_t *below,unsigned blocks,unsigned strength,
 *                         unsigned limit)
 * One block is 16 pixels. Vector add and subtract saturate, and each pixel moves
 * in one direction only, so the order of the last two steps does not matter. */
    .global cp_sharpen_row_pie
    .type cp_sharpen_row_pie,@function
cp_sharpen_row_pie:
    beqz a4,2f
    li t3,8
    esp.movx.w.sar t3
    li t3,128
    esp.zero.q q5
    esp.vsadds.u8 q5,q5,t3          /* q5 = one half in 8 fractional bits */
    esp.zero.q q6
    esp.vsadds.u8 q6,q6,a5          /* q6 = strength */
    mv t3,a6                        /* PIE cannot name a6. */
    esp.zero.q q7
    esp.vsadds.u8 q7,q7,t3          /* q7 = limit */
1:  esp.vld.128.ip q0,a1,16
    esp.vld.128.ip q1,a3,16
    esp.vld.128.ip q2,a2,16
    esp.vmul.u8 q0,q0,q5
    esp.vmul.u8 q1,q1,q5
    esp.vadd.u8 q0,q0,q1            /* q0 = average of above and below */
    esp.vsub.u8 q3,q2,q0
    esp.vsub.u8 q4,q0,q2
    esp.vmul.u8 q3,q3,q6
    esp.vmul.u8 q4,q4,q6
    esp.vmin.u8 q3,q3,q7
    esp.vmin.u8 q4,q4,q7
    esp.vadd.u8 q2,q2,q3
    esp.vsub.u8 q2,q2,q4
    esp.vst.128.ip q2,a0,16
    addi a4,a4,-1
    bnez a4,1b
2:  ret
    .size cp_sharpen_row_pie,.-cp_sharpen_row_pie
#endif
