// SPDX-License-Identifier: BSD-2-Clause
// Derived from DecodeBinCabac in OpenH264 cabac_decoder.cpp.
//
// Tab5 change: this engine keeps at most 9+16 bits of look-ahead instead of 9+32,
// and works on 32-bit values. On RV32 each bin otherwise costs 64-bit shifts and
// compares, and bins are the largest single cost of decoding iPhone streams. The
// uint64_t fields of SWelsCabacDecEngine are unchanged and hold the same numbers,
// so only their low words are read and written (little endian, and the component
// builds with -fno-strict-aliasing).
#ifndef TAB5_CABAC_BIN_INLINE_H_
#define TAB5_CABAC_BIN_INLINE_H_
#include "cabac_decoder.h"
namespace WelsDec {
static inline uint32_t& Low32 (uint64_t& uiValue) {
  return *reinterpret_cast<uint32_t*> (&uiValue);
}
int32_t RefillCabacAfterBin (PWelsCabacDecEngine pDecEngine, uint32_t uiOffset);
__attribute__ ((always_inline)) static inline int32_t DecodeBinCabacInline (PWelsCabacDecEngine pDecEngine,
    PWelsCabacCtx pBinCtx, uint32_t& uiBinVal) {
  const uint32_t uiState = pBinCtx->uiState;
  uiBinVal = pBinCtx->uiMPS;
  uint32_t uiOffset = Low32 (pDecEngine->uiOffset);
  uint32_t uiRange = Low32 (pDecEngine->uiRange);
  const int32_t iBitsLeft = pDecEngine->iBitsLeft;

  int32_t iRenorm = 1;
  const uint32_t uiRangeLPS = g_kuiCabacRangeLps[uiState][ (uiRange >> 6) & 0x03];
  uiRange -= uiRangeLPS;
  const uint32_t uiScaledRange = uiRange << iBitsLeft;
  if (uiOffset >= uiScaledRange) { //LPS
    uiOffset -= uiScaledRange;
    uiBinVal ^= 0x0001;
    if (!uiState)
      pBinCtx->uiMPS ^= 0x01;
    pBinCtx->uiState = g_kuiStateTransTable[uiState][0];
    iRenorm = g_kRenormTable256[uiRangeLPS];
    uiRange = (uiRangeLPS << iRenorm);
  } else {  //MPS
    pBinCtx->uiState = g_kuiStateTransTable[uiState][1];
    if (uiRange >= WELS_CABAC_QUARTER) {
      Low32 (pDecEngine->uiRange) = uiRange;
      return ERR_NONE;
    }
    uiRange <<= 1;
  }
  //Renorm
  Low32 (pDecEngine->uiRange) = uiRange;
  pDecEngine->iBitsLeft = iBitsLeft - iRenorm;
  if (pDecEngine->iBitsLeft > 0) {
    Low32 (pDecEngine->uiOffset) = uiOffset;
    return ERR_NONE;
  }
  return RefillCabacAfterBin (pDecEngine, uiOffset);
}
} // namespace WelsDec
#endif
