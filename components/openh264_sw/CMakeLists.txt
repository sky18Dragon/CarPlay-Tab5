# Portable Cisco OpenH264 v2.6.0 decoder. Fetch with scripts/fetch_openh264.py.
# Fails when upstream no longer matches, instead of silently dropping a change.
macro(tab5_replace variable match replacement)
    string(FIND "${${variable}}" "${match}" tab5_offset)
    if(tab5_offset EQUAL -1)
        message(FATAL_ERROR "OpenH264 patch no longer matches: ${match}")
    endif()
    string(REPLACE "${match}" "${replacement}" ${variable} "${${variable}}")
endmacro()
if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(REQUIRES pthread heap esp_timer)
    return()
endif()
set(root "${CMAKE_CURRENT_LIST_DIR}/../../external/openh264")
if(NOT EXISTS "${root}/codec/decoder/plus/src/welsDecoderExt.cpp")
    message(FATAL_ERROR "Run the shared Python environment with scripts/fetch_openh264.py first")
endif()
file(GLOB common "${root}/codec/common/src/*.cpp")
file(GLOB decoder "${root}/codec/decoder/core/src/*.cpp" "${root}/codec/decoder/plus/src/*.cpp")
# 32-bit CABAC engine: see cabac_bin_inline.h. Only the residual loops inline it;
# the other sixty call sites would just crowd the 16 KB instruction cache.
list(REMOVE_ITEM decoder "${root}/codec/decoder/core/src/cabac_decoder.cpp")
list(APPEND decoder "${CMAKE_CURRENT_LIST_DIR}/cabac_decoder_esp.cpp")
# Generate two ESP-specific translations, retaining upstream copyright notices.
set(thread "${root}/codec/common/src/WelsThreadLib.cpp")
set(memory "${root}/codec/common/src/memory_align.cpp")
list(REMOVE_ITEM common "${thread}" "${memory}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${thread}" "${memory}")
file(READ "${thread}" code)
string(REPLACE "#elif !defined(__Fuchsia__)" "#elif !defined(__Fuchsia__) && !defined(ESP_PLATFORM)" code "${code}")
string(REPLACE "#ifdef ANDROID_NDK\n  pInfo" "#ifdef ESP_PLATFORM\n  pInfo->ProcessorCount = 1;\n  return WELS_THREAD_ERROR_OK;\n#elif defined(ANDROID_NDK)\n  pInfo" code "${code}")
string(REPLACE "err = pthread_attr_setscope (&at, PTHREAD_SCOPE_SYSTEM);" "/* ESP-IDF uses its default pthread scheduling scope. */" code "${code}")
string(REPLACE "err = pthread_attr_setschedpolicy (&at, SCHED_FIFO);" "/* ESP-IDF pthread scheduling is configured by esp_pthread. */" code "${code}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/WelsThreadLib_esp.cpp" "${code}")
file(READ "${memory}" code)
string(REPLACE "#include <stdlib.h>" "#include <stdlib.h>\n#include \"esp_heap_caps.h\"" code "${code}")
string(REPLACE "malloc (kiActualRequestedSize)" "heap_caps_malloc (kiActualRequestedSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)" code "${code}")
string(REPLACE "if (NULL == pBuf)\n    return NULL;" "if (NULL == pBuf) { printf(\"OpenH264 allocation failed bytes=%lu tag=%s free_psram=%u\\n\", (unsigned long)kiActualRequestedSize, kpTag, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM)); return NULL; }" code "${code}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/memory_align_esp.cpp" "${code}")
# Two sets of macroblock arrays, so the next frame can be parsed while this one is
# still being reconstructed. Headers include each other by directory, so the
# changed one needs the whole directory beside it.
set(core_inc "${CMAKE_CURRENT_BINARY_DIR}/core_inc")
file(GLOB core_headers "${root}/codec/decoder/core/inc/*.h")
list(REMOVE_ITEM core_headers "${root}/codec/decoder/core/inc/wels_const.h")
file(COPY ${core_headers} DESTINATION "${core_inc}")
file(READ "${root}/codec/decoder/core/inc/wels_const.h" code)
tab5_replace(code "#define LAYER_NUM_EXCHANGEABLE          1" "#define LAYER_NUM_EXCHANGEABLE          2")
file(WRITE "${core_inc}/wels_const.h" "${code}")
set(core "${root}/codec/decoder/core/src/decoder_core.cpp")
list(REMOVE_ITEM decoder "${core}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${core}")
file(READ "${core}" code)
# Xtensa's int32_t is long: match the existing header declarations exactly.
string(REPLACE "ExpandBsBuffer (PWelsDecoderContext pCtx, const int kiSrcLen)" "ExpandBsBuffer (PWelsDecoderContext pCtx, const int32_t kiSrcLen)" code "${code}")
string(REPLACE "ExpandBsLenBuffer (PWelsDecoderContext pCtx, const int kiCurrLen)" "ExpandBsLenBuffer (PWelsDecoderContext pCtx, const int32_t kiCurrLen)" code "${code}")
# Start small; upstream CheckBsBuffer/ExpandBsBuffer grows this safely as needed.
string(REPLACE "pCtx->iMaxBsBufferSizeInByte = MIN_ACCESS_UNIT_CAPACITY * MAX_BUFFERED_NUM;" "pCtx->iMaxBsBufferSizeInByte = 256 * 1024;" code "${code}")
# This decoder rejects field pictures. For frame pictures LongTermPicNum equals
# LongTermFrameIdx (H.264 8.2.4.1). MMCO 3/6 has no long_term_pic_num syntax;
# leaving its separate field at zero breaks nonzero LTR indices used by iOS.
string(REPLACE "kpRefMarking->sMmcoRef[iIdx].iLongTermFrameIdx = uiCode;"
    "kpRefMarking->sMmcoRef[iIdx].iLongTermFrameIdx = uiCode;\n          kpRefMarking->sMmcoRef[iIdx].uiLongTermPicNum = uiCode;" code "${code}")
# Parallel reconstruction: see recon_worker_esp.h.
string(REPLACE "int32_t  iRet = WelsTargetSliceConstruction (pCtx);" "int32_t  iRet;\n  if (!Tab5ReconTakeResult (&iRet))\n    iRet = WelsTargetSliceConstruction (pCtx);" code "${code}")
string(REPLACE "iRet = WelsDecodeSlice (pCtx, bFreshSliceAvailable, pNalCur);" "Tab5ReconAllow (bReconstructSlice);\n          iRet = WelsDecodeSlice (pCtx, bFreshSliceAvailable, pNalCur);" code "${code}")
string(REPLACE "#include \"decoder_core.h\"" "#include \"decoder_core.h\"\n#include \"recon_worker_esp.h\"" code "${code}")
# Parsing alternates between the two macroblock data sets.
string(REGEX REPLACE "pCtx->sMb\\.([A-Za-z0-9]+)\\[0\\]" "pCtx->sMb.\\1[uiSet]" code "${code}")
tab5_replace(code "  if (NULL != pCtx && NULL != pCurDq) {\n    pCurDq->pMbType " "  if (NULL != pCtx && NULL != pCurDq) {\n    const uint32_t uiSet = Tab5ReconLayerSet (pCtx);\n    pCurDq->pMbType ")
tab5_replace(code "for (int32_t i = 0; i < LAYER_NUM_EXCHANGEABLE; ++ i)\n        memset (pCtx->sMb.pSliceIdc[i], 0xff," "memset (pCtx->pCurDqLayer->pSliceIdc, 0xff,")
# Borders are copied from final pixels, so they follow reconstruction.
tab5_replace(code "if (!pCtx->pParam->bParseOnly)\n            ExpandReferencingPicture (" "if (!pCtx->pParam->bParseOnly && !Tab5ReconExpandLater (pCtx))\n            ExpandReferencingPicture (")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/decoder_core_esp.cpp" "${code}")
# FlushFrame passes NULL in single-thread mode. Upstream's no-reorder release
# misses that context and never decrements picture iRefCount, exhausting the DPB.
# Mirror the context lookup already used by ReleaseBufferedReadyPictureReorder.
set(parse "${root}/codec/decoder/core/src/parse_mb_syn_cabac.cpp")
list(REMOVE_ITEM decoder "${parse}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${parse}")
file(READ "${parse}" code)
string(REPLACE "#include \"parse_mb_syn_cabac.h\"" "#include \"parse_mb_syn_cabac.h\"\n#include \"cabac_bin_inline.h\"" code "${code}")
foreach(context "pMapCtx + iCtx" "pLastCtx + iCtx" "pOneCtx + c1")
    string(REPLACE "DecodeBinCabac (pCtx->pCabacDecEngine, ${context}, uiCode)" "DecodeBinCabacInline (pCtx->pCabacDecEngine, ${context}, uiCode)" code "${code}")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/parse_mb_syn_cabac_esp.cpp" "${code}")
set(slice "${root}/codec/decoder/core/src/decode_slice.cpp")
list(REMOVE_ITEM decoder "${slice}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${slice}")
file(READ "${slice}" code)
string(REPLACE "#include \"decode_slice.h\"" "#include \"decode_slice.h\"\n#include \"recon_worker_esp.h\"" code "${code}")
string(REPLACE "    if (iCountNumMb >= iTotalNumMb) {\n      break;\n    }\n\n    if (!pCtx->pParam->bParseOnly) { //for parse only, actual recon MB unnecessary" "    if (!Tab5ReconWait (pCtx, iCountNumMb, iTotalNumMb)) {\n      break;\n    }\n\n    if (!pCtx->pParam->bParseOnly) { //for parse only, actual recon MB unnecessary" code "${code}")
string(REPLACE "  pCtx->pDec->iWidthInPixel  = iCurLayerWidth;\n  pCtx->pDec->iHeightInPixel = iCurLayerHeight;\n\n  if ((pCurSlice->eSliceType != I_SLICE)" "  Tab5ReconSettle (pCtx);\n  pCtx->pDec->iWidthInPixel  = iCurLayerWidth;\n  pCtx->pDec->iHeightInPixel = iCurLayerHeight;\n\n  if ((pCurSlice->eSliceType != I_SLICE)" code "${code}")
string(REPLACE "int32_t WelsDecodeSlice (PWelsDecoderContext pCtx, bool bFirstSliceInLayer, PNalUnit pNalCur) {" "static int32_t WelsDecodeSliceParse (PWelsDecoderContext pCtx, bool bFirstSliceInLayer, PNalUnit pNalCur) {" code "${code}")
string(REPLACE "  pCurDqLayer->iMbXyIndex = iNextMbXyIndex;\n\n  do {\n    if ((-1 == iNextMbXyIndex) || (iNextMbXyIndex >= kiCountNumMb)) { // slice group boundary or end of a frame\n      break;\n    }\n\n    pCurDqLayer->pSliceIdc[iNextMbXyIndex] = iSliceIdc;\n    pCtx->bMbRefConcealed = false;" "  pCurDqLayer->iMbXyIndex = iNextMbXyIndex;\n\n  Tab5ReconBegin (pCtx);\n  do {\n    if ((-1 == iNextMbXyIndex) || (iNextMbXyIndex >= kiCountNumMb)) { // slice group boundary or end of a frame\n      break;\n    }\n\n    pCurDqLayer->pSliceIdc[iNextMbXyIndex] = iSliceIdc;\n    pCtx->bMbRefConcealed = false;" code "${code}")
string(REPLACE "    ++pSlice->iTotalMbInCurSlice;\n    if (uiEosFlag) { //end of slice\n      break;\n    }\n    if (pSliceHeader->pPps->uiNumSliceGroups > 1) {\n      iNextMbXyIndex = FmoNextMb (pFmo, iNextMbXyIndex);" "    ++pSlice->iTotalMbInCurSlice;\n    Tab5ReconPublish (pSlice->iTotalMbInCurSlice);\n    if (uiEosFlag) { //end of slice\n      break;\n    }\n    if (pSliceHeader->pPps->uiNumSliceGroups > 1) {\n      iNextMbXyIndex = FmoNextMb (pFmo, iNextMbXyIndex);" code "${code}")
string(APPEND code "\nnamespace WelsDec {\nint32_t WelsDecodeSlice (PWelsDecoderContext pCtx, bool bFirstSliceInLayer, PNalUnit pNalCur) {\n  const int32_t iRet = WelsDecodeSliceParse (pCtx, bFirstSliceInLayer, pNalCur);\n  Tab5ReconFinish (pCtx, iRet);\n  return iRet;\n}\n}\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/decode_slice_esp.cpp" "${code}")
set(plus "${root}/codec/decoder/plus/src/welsDecoderExt.cpp")
list(REMOVE_ITEM decoder "${plus}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${plus}")
file(READ "${plus}" code)
set(no_reorder "void CWelsDecoder::ReleaseBufferedReadyPictureNoReorder(PWelsDecoderContext pCtx, unsigned char** ppDst, SBufferInfo* pDstInfo)\n{")
string(FIND "${code}" "${no_reorder}" release_offset)
if(release_offset EQUAL -1)
    message(FATAL_ERROR "OpenH264 no-reorder patch no longer matches upstream")
endif()
string(REPLACE "${no_reorder}" "${no_reorder}\n  if (pCtx == NULL && m_iThreadCount <= 1) pCtx = m_pDecThrCtx[0].pCtx;" code "${code}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/welsDecoderExt_esp.cpp" "${code}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version_gen.h" "#define VERSION_NUMBER \"2.6.0 Tab5 portable\"\n")
idf_component_register(SRCS ${common} ${decoder}
    "${CMAKE_CURRENT_BINARY_DIR}/WelsThreadLib_esp.cpp" "${CMAKE_CURRENT_BINARY_DIR}/memory_align_esp.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/decoder_core_esp.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/welsDecoderExt_esp.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/parse_mb_syn_cabac_esp.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/decode_slice_esp.cpp"
    "recon_worker_esp.cpp"
    INCLUDE_DIRS "${root}/codec/api" "${root}/codec/api/wels" "include"
    PRIV_INCLUDE_DIRS "${root}/codec/common/inc" "${core_inc}" "${root}/codec/decoder/plus/inc" "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_LIST_DIR}"
    REQUIRES pthread heap esp_timer)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -fno-strict-aliasing -Wno-error -Wno-error=format -Wno-unused-function -Wno-unused-variable)
target_compile_definitions(${COMPONENT_LIB} PRIVATE GENERATED_VERSION_HEADER=1)
