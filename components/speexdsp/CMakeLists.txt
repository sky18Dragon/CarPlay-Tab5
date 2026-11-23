# SpeexDSP acoustic echo canceller and preprocessor (BSD-3-Clause, see COPYING),
# from https://github.com/xiph/speexdsp at 7a158783df74efe7c2d1c6ee8363c1e695c71226.
# Only the files these two need. Floating point: the ESP32-P4 has a single-precision FPU.
idf_component_register(
    SRCS "libspeexdsp/mdf.c" "libspeexdsp/preprocess.c" "libspeexdsp/filterbank.c"
         "libspeexdsp/fftwrap.c" "libspeexdsp/kiss_fft.c" "libspeexdsp/kiss_fftr.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "libspeexdsp"
    PRIV_REQUIRES heap)
target_compile_definitions(${COMPONENT_LIB} PRIVATE FLOATING_POINT USE_KISS_FFT EXPORT=)
target_compile_options(${COMPONENT_LIB} PRIVATE "-include${CMAKE_CURRENT_LIST_DIR}/speex_alloc_psram.h" -O2 -Wno-unused-variable -Wno-unused-but-set-variable -Wno-sign-compare -Wno-shift-negative-value)
