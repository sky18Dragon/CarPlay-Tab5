p='components/openh264_sw/recon_worker_esp.cpp'; s=open(p).read()
def R(a,b):
    global s
    assert a in s,a
    s=s.replace(a,b,1)
R('namespace WelsDec {\nnamespace {\n','''#include "esp_timer.h"
extern "C" { volatile int64_t g_pipe[12]; }
#define PIPE_T() esp_timer_get_time()
namespace WelsDec {
namespace {
''')
R('''    xQueueReceive (sJobQueue, &sJob, portMAX_DELAY);
    const bool bSkip''','''    { int64_t t=PIPE_T(); xQueueReceive (sJobQueue, &sJob, portMAX_DELAY); g_pipe[0]+=PIPE_T()-t; }
    const bool bSkip''')
R('''    case kJobRecon:
      RunStage (*sJob.pStage);
      break;''','''    case kJobRecon:
      { int64_t t=PIPE_T(); RunStage (*sJob.pStage); g_pipe[1]+=PIPE_T()-t; }
      break;''')
R('''      if (!bSkip)
        ExpandReferencingPicture (sJob.pPicture->pData, sJob.pPicture->iWidthInPixel, sJob.pPicture->iHeightInPixel,
                                  sJob.pPicture->iLinesize, sJob.pCtx->sExpandPicFunc.pfExpandLumaPicture,
                                  sJob.pCtx->sExpandPicFunc.pfExpandChromaPicture);
      break;''','''      if (!bSkip) { int64_t t=PIPE_T();
        ExpandReferencingPicture (sJob.pPicture->pData, sJob.pPicture->iWidthInPixel, sJob.pPicture->iHeightInPixel,
                                  sJob.pPicture->iLinesize, sJob.pCtx->sExpandPicFunc.pfExpandLumaPicture,
                                  sJob.pCtx->sExpandPicFunc.pfExpandChromaPicture);
        g_pipe[2]+=PIPE_T()-t; }
      break;''')
R('''      if (!bSkip)
        sJob.pWork (sJob.uiPayload);
      break;''','''      if (!bSkip) { int64_t t=PIPE_T(); sJob.pWork (sJob.uiPayload); g_pipe[3]+=PIPE_T()-t; }
      break;''')
# blocked in wait
R('''    if (bFinished)
      return false;
    ulTaskNotifyTake (pdTRUE, portMAX_DELAY);
  }
}
''','''    if (bFinished)
      return false;
    { int64_t t=PIPE_T(); ulTaskNotifyTake (pdTRUE, portMAX_DELAY); g_pipe[4]+=PIPE_T()-t; }
  }
}
''')
R('''  while (!sCurrent->bFinished.load (std::memory_order_acquire)) {
    ulTaskNotifyTake (pdTRUE, portMAX_DELAY);
  }
''','''  int64_t t0=PIPE_T();
  while (!sCurrent->bFinished.load (std::memory_order_acquire)) {
    ulTaskNotifyTake (pdTRUE, portMAX_DELAY);
  }
  g_pipe[5]+=PIPE_T()-t0; g_pipe[6]-=PIPE_T();
''')
R('''    sStage.iResult = WelsTargetSliceConstruction (sStage.pContext);
''','''    sStage.iResult = WelsTargetSliceConstruction (sStage.pContext);
    g_pipe[6]+=PIPE_T();
''')
# parse side
R('''  xSemaphoreTake (sStage.hFree, portMAX_DELAY);
''','''  { int64_t t=PIPE_T(); xSemaphoreTake (sStage.hFree, portMAX_DELAY); g_pipe[7]+=PIPE_T()-t; g_pipe[8]-=PIPE_T(); }
''')
R('''  sOwner = &sStage;
  Job sJob = {};''','''  g_pipe[8]+=PIPE_T();
  sOwner = &sStage;
  Job sJob = {};''')
R('''  Post (sJob);
  xSemaphoreTake (sDrained, portMAX_DELAY);
''','''  Post (sJob);
  { int64_t t=PIPE_T(); xSemaphoreTake (sDrained, portMAX_DELAY); g_pipe[9]+=PIPE_T()-t; ++g_pipe[10]; }
''')
open(p,'w').write(s)
