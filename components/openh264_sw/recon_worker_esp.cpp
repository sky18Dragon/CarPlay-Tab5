// SPDX-License-Identifier: GPL-3.0-or-later
#include "recon_worker_esp.h"
#include "decode_slice.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tab5_recon_queue.h"
#include <atomic>
#include <stddef.h>
#include <string.h>

namespace WelsDec {
namespace {
constexpr BaseType_t kCore = 0;        // Parsing runs on core 1.
constexpr UBaseType_t kPriority = 4;   // The same as the decoder task.
constexpr uint32_t kStackBytes = 12288;
constexpr uint32_t kStages = 2;        // The frame being finished and the one being parsed.
constexpr UBaseType_t kJobs = 8;       // Three per frame: reconstruct, expand, deliver.

// One reconstruction, from the copy of the state that it needs.
struct Stage {
  SWelsDecoderContext* pContext;
  SDqLayer* pLayer;
  SemaphoreHandle_t hFree;
  std::atomic<int32_t> iPublished{0};
  std::atomic<bool> bFinished{false};
  bool bDeferred;   // Nobody waits for this result, so a failure must be remembered.
  int32_t iNotifyEvery;
  int32_t iResult;
};
enum JobKind : uint8_t { kJobRecon, kJobExpand, kJobWork, kJobDrain };
struct Job {
  JobKind eKind;
  Stage* pStage;
  PWelsDecoderContext pCtx;
  PPicture pPicture;
  tab5_recon_work pWork;
  uint8_t uiPayload[TAB5_RECON_PAYLOAD_BYTES];
};

// Only one decoder runs at a time, and a slice always ends in Tab5ReconFinish.
Stage sStages[kStages];
Stage* sOwner;     // Parsing side: the stage of the slice being parsed.
Stage* sCurrent;   // Worker side: the stage being reconstructed.
uint32_t sNextStage, sLayerSet;
TaskHandle_t sTask;
QueueHandle_t sJobQueue;
SemaphoreHandle_t sDrained;
std::atomic<bool> sFailed{false};
int32_t sResult;
bool sAllowed, sResultReady;

void RunStage (Stage& sStage) {
  sStage.iResult = ERR_NONE;
  if (!sFailed.load (std::memory_order_relaxed)) {
    sCurrent = &sStage;
    sStage.iResult = WelsTargetSliceConstruction (sStage.pContext);
    sCurrent = nullptr;
    if (sStage.iResult != ERR_NONE && sStage.bDeferred)
      sFailed.store (true, std::memory_order_relaxed);
  }
  xSemaphoreGive (sStage.hFree);
}

void ReconTask (void*) {
  Job sJob;
  for (;;) {
    xQueueReceive (sJobQueue, &sJob, portMAX_DELAY);
    const bool bSkip = sFailed.load (std::memory_order_relaxed);
    switch (sJob.eKind) {
    case kJobRecon:
      RunStage (*sJob.pStage);
      break;
    case kJobExpand:
      if (!bSkip)
        ExpandReferencingPicture (sJob.pPicture->pData, sJob.pPicture->iWidthInPixel, sJob.pPicture->iHeightInPixel,
                                  sJob.pPicture->iLinesize, sJob.pCtx->sExpandPicFunc.pfExpandLumaPicture,
                                  sJob.pCtx->sExpandPicFunc.pfExpandChromaPicture);
      break;
    case kJobWork:
      if (!bSkip)
        sJob.pWork (sJob.uiPayload);
      break;
    case kJobDrain:
      xSemaphoreGive (sDrained);
      break;
    }
  }
}

bool EnsureWorker() {
  if (sTask)
    return true;
  for (Stage& sStage : sStages) {
    if (!sStage.pContext)
      sStage.pContext = static_cast<SWelsDecoderContext*> (heap_caps_malloc (sizeof (SWelsDecoderContext), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!sStage.pLayer)
      sStage.pLayer = static_cast<SDqLayer*> (heap_caps_malloc (sizeof (SDqLayer), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!sStage.hFree) {
      sStage.hFree = xSemaphoreCreateBinary();
      if (sStage.hFree)
        xSemaphoreGive (sStage.hFree);
    }
    if (!sStage.pContext || !sStage.pLayer || !sStage.hFree)
      return false;
  }
  if (!sDrained)
    sDrained = xSemaphoreCreateBinary();
  if (!sJobQueue)
    sJobQueue = xQueueCreate (kJobs, sizeof (Job));
  if (!sDrained || !sJobQueue)
    return false;
  return xTaskCreatePinnedToCore (ReconTask, "h264_recon", kStackBytes, nullptr, kPriority, &sTask, kCore) == pdPASS;
}

void Post (const Job& sJob) {
  xQueueSend (sJobQueue, &sJob, portMAX_DELAY);
}

void Drain() {
  if (!sTask)
    return;
  Job sJob = {};
  sJob.eKind = kJobDrain;
  Post (sJob);
  xSemaphoreTake (sDrained, portMAX_DELAY);
}

// The context is 565 KB, nearly all of it parameter sets and parsing tables that
// reconstruction reaches through pointers into the original. A whole copy would cost
// 8 ms per frame.
void CopyContext (SWelsDecoderContext* pTo, const SWelsDecoderContext* pFrom) {
  struct Range { size_t uiBegin, uiEnd; };
  constexpr size_t kSets = offsetof (SWelsDecoderContext, sSpsPpsCtx);
  constexpr size_t kCabac = offsetof (SWelsDecoderContext, sWelsCabacContexts);
  constexpr size_t kAfterCabac = kCabac + sizeof (pFrom->sWelsCabacContexts);
  constexpr size_t kDequant = offsetof (SWelsDecoderContext, pDequant_coeff_buffer4x4);
  constexpr size_t kAfterDequant = offsetof (SWelsDecoderContext, pDequant_coeff_buffer8x8) + sizeof (pFrom->pDequant_coeff_buffer8x8);
  const Range kRanges[] = {
    {0, kSets},
    {kSets + sizeof (pFrom->sSpsPpsCtx), kCabac},
    {kAfterCabac, kDequant},
    {kAfterDequant, sizeof (SWelsDecoderContext)},
  };
  for (const Range& sRange : kRanges)
    memcpy (reinterpret_cast<uint8_t*> (pTo) + sRange.uiBegin, reinterpret_cast<const uint8_t*> (pFrom) + sRange.uiBegin, sRange.uiEnd - sRange.uiBegin);
  pTo->sSpsPpsCtx.bAvcBasedFlag = pFrom->sSpsPpsCtx.bAvcBasedFlag;
}
} // namespace

void Tab5ReconAllow (bool bAllowed) {
  sAllowed = bAllowed;
}

void Tab5ReconBegin (PWelsDecoderContext pCtx) {
  sOwner = nullptr;
  sResultReady = false;
  PDqLayer pLayer = pCtx->pCurDqLayer;
  PSliceHeader pHeader = &pLayer->sLayerInfo.sSliceInLayer.sSliceHeaderExt.sSliceHeader;
  const bool bPlainSlice = pHeader->eSliceType == I_SLICE || pHeader->eSliceType == P_SLICE;
  if (!sAllowed || !bPlainSlice || pHeader->pPps->uiNumSliceGroups != 1 || pCtx->pParam->bParseOnly || !EnsureWorker()) {
    Drain();   // Upstream's serial path reads the pictures that jobs are writing.
    return;
  }
  Stage& sStage = sStages[sNextStage];
  sNextStage = (sNextStage + 1) % kStages;
  xSemaphoreTake (sStage.hFree, portMAX_DELAY);
  memcpy (sStage.pLayer, pLayer, sizeof (SDqLayer));
  CopyContext (sStage.pContext, pCtx);
  sStage.pContext->pCurDqLayer = sStage.pLayer;
  sStage.iPublished.store (0, std::memory_order_relaxed);
  sStage.bFinished.store (false, std::memory_order_relaxed);
  sStage.bDeferred = false;
  sStage.iNotifyEvery = pLayer->iMbWidth;
  sOwner = &sStage;
  Job sJob = {};
  sJob.eKind = kJobRecon;
  sJob.pStage = &sStage;
  Post (sJob);
}

void Tab5ReconPublish (int32_t iParsedMb) {
  if (!sOwner)
    return;
  sOwner->iPublished.store (iParsedMb, std::memory_order_release);
  if (iParsedMb % sOwner->iNotifyEvery == 0)
    xTaskNotifyGive (sTask);
}

bool Tab5ReconWait (PWelsDecoderContext pCtx, int32_t iMbIndex, int32_t iSerialTotal) {
  if (!sCurrent || pCtx != sCurrent->pContext)
    return iMbIndex < iSerialTotal;
  for (;;) {
    const bool bFinished = sCurrent->bFinished.load (std::memory_order_acquire);
    if (iMbIndex < sCurrent->iPublished.load (std::memory_order_acquire))
      return true;
    if (bFinished)
      return false;
    ulTaskNotifyTake (pdTRUE, portMAX_DELAY);
  }
}

void Tab5ReconSettle (PWelsDecoderContext pCtx) {
  if (!sCurrent || pCtx != sCurrent->pContext)
    return;
  // The loop also ends at the last macroblock of the picture, possibly before
  // parsing has returned, and deblocking needs the final count from the slice.
  while (!sCurrent->bFinished.load (std::memory_order_acquire)) {
    ulTaskNotifyTake (pdTRUE, portMAX_DELAY);
  }
  sCurrent->pLayer->sLayerInfo.sSliceInLayer.iTotalMbInCurSlice = sCurrent->iPublished.load (std::memory_order_acquire);
}

void Tab5ReconFinish (PWelsDecoderContext pCtx, int32_t iParseResult) {
  Stage* pStage = sOwner;
  if (!pStage)
    return;
  sOwner = nullptr;
  PDqLayer pLayer = pCtx->pCurDqLayer;
  const SSlice& sSlice = pLayer->sLayerInfo.sSliceInLayer;
  const bool bWholePicture = iParseResult == ERR_NONE && sSlice.sSliceHeaderExt.sSliceHeader.iFirstMbInSlice == 0
                             && sSlice.iTotalMbInCurSlice == pLayer->iMbWidth * pLayer->iMbHeight;
  pStage->bDeferred = bWholePicture;
  pStage->bFinished.store (true, std::memory_order_release);
  xTaskNotifyGive (sTask);
  if (bWholePicture) {
    // Every macroblock will be reconstructed once, which is all that upstream's
    // bookkeeping for the rest of this picture asks about.
    pCtx->iTotalNumMbRec = sSlice.iTotalMbInCurSlice;
    sResult = ERR_NONE;
    sResultReady = true;
    return;
  }
  Drain();
  // Without error concealment upstream abandons a slice that failed to parse before
  // reconstructing it, and later decisions depend on that count staying unchanged.
  if (iParseResult != ERR_NONE && pCtx->pParam->eEcActiveIdc == ERROR_CON_DISABLE)
    return;
  pCtx->iTotalNumMbRec = pStage->pContext->iTotalNumMbRec;
  pCtx->iErrorCode |= pStage->pContext->iErrorCode;
  pLayer->iMbX = pStage->pLayer->iMbX;
  pLayer->iMbY = pStage->pLayer->iMbY;
  pLayer->iMbXyIndex = pStage->pLayer->iMbXyIndex;
  pLayer->iLumaStride = pStage->pLayer->iLumaStride;
  pLayer->iChromaStride = pStage->pLayer->iChromaStride;
  memcpy (pLayer->pPred, pStage->pLayer->pPred, sizeof (pLayer->pPred));
  sResult = pStage->iResult;
  sResultReady = true;
}

bool Tab5ReconTakeResult (int32_t* pResult) {
  if (!sResultReady)
    return false;
  sResultReady = false;
  *pResult = sResult;
  return true;
}

bool Tab5ReconExpandLater (PWelsDecoderContext pCtx) {
  if (!sTask)
    return false;
  Job sJob = {};
  sJob.eKind = kJobExpand;
  sJob.pCtx = pCtx;
  sJob.pPicture = pCtx->pDec;
  Post (sJob);
  return true;
}

uint32_t Tab5ReconLayerSet (PWelsDecoderContext pCtx) {
  if (pCtx->iTotalNumMbRec == 0)   // Otherwise this continues a picture.
    sLayerSet = (sLayerSet + 1) % LAYER_NUM_EXCHANGEABLE;
  return sLayerSet;
}
} // namespace WelsDec

bool tab5_recon_post (tab5_recon_work pWork, const void* pPayload, size_t uiBytes) {
  using namespace WelsDec;
  if (uiBytes > TAB5_RECON_PAYLOAD_BYTES || !EnsureWorker())
    return false;
  Job sJob = {};
  sJob.eKind = kJobWork;
  sJob.pWork = pWork;
  memcpy (sJob.uiPayload, pPayload, uiBytes);
  Post (sJob);
  return true;
}

void tab5_recon_drain (void) {
  WelsDec::Drain();
}

bool tab5_recon_failed (void) {
  return WelsDec::sFailed.load (std::memory_order_relaxed);
}

void tab5_recon_clear_failure (void) {
  WelsDec::sFailed.store (false, std::memory_order_relaxed);
}
