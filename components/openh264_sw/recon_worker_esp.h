// SPDX-License-Identifier: GPL-3.0-or-later
// Runs OpenH264 macroblock reconstruction and deblocking on the second core while
// CABAC parsing runs on the first, and lets the next frame be parsed while this one
// is still being finished. A frame then costs the slower of the two stages rather
// than the time from the start of parsing to the end of deblocking.
//
// Upstream parses a whole slice before reconstructing it, so parsing never reads
// pixels, and reconstruction reads only per-macroblock arrays and slice-wide values.
// Reconstruction therefore works from its own copy of the context and layer, and
// stays behind the published macroblock count. Pictures are written only by jobs,
// strictly in decode order, so a reference that parsing has already released cannot
// change until the jobs that read it are done. Parsing alternates between two sets
// of macroblock arrays for the same reason.
//
// Only a slice that parsed cleanly and covers its whole picture goes through without
// waiting. Anything else drains the queue and continues exactly as upstream would.
#ifndef TAB5_RECON_WORKER_ESP_H_
#define TAB5_RECON_WORKER_ESP_H_
#include "decoder_context.h"
namespace WelsDec {
#ifdef ESP_PLATFORM
void Tab5ReconAllow (bool bAllowed);
void Tab5ReconBegin (PWelsDecoderContext pCtx);
void Tab5ReconPublish (int32_t iParsedMb);
void Tab5ReconFinish (PWelsDecoderContext pCtx, int32_t iParseResult);
bool Tab5ReconWait (PWelsDecoderContext pCtx, int32_t iMbIndex, int32_t iSerialTotal);
void Tab5ReconSettle (PWelsDecoderContext pCtx);
bool Tab5ReconTakeResult (int32_t* pResult);
bool Tab5ReconExpandLater (PWelsDecoderContext pCtx);
uint32_t Tab5ReconLayerSet (PWelsDecoderContext pCtx);
#else
static inline void Tab5ReconAllow (bool) {}
static inline void Tab5ReconBegin (PWelsDecoderContext) {}
static inline void Tab5ReconPublish (int32_t) {}
static inline void Tab5ReconFinish (PWelsDecoderContext, int32_t) {}
static inline bool Tab5ReconWait (PWelsDecoderContext, int32_t iMbIndex, int32_t iSerialTotal) { return iMbIndex < iSerialTotal; }
static inline void Tab5ReconSettle (PWelsDecoderContext) {}
static inline bool Tab5ReconTakeResult (int32_t*) { return false; }
static inline bool Tab5ReconExpandLater (PWelsDecoderContext) { return false; }
static inline uint32_t Tab5ReconLayerSet (PWelsDecoderContext) { return 0; }
#endif
} // namespace WelsDec
#endif
