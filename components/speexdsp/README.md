# speexdsp (subset)

Echo canceller (`mdf.c`) and preprocessor (`preprocess.c`, used for residual echo
suppression) from [xiph/speexdsp](https://github.com/xiph/speexdsp), commit
`7a158783df74efe7c2d1c6ee8363c1e695c71226`, BSD-3-Clause (`COPYING`). Unmodified
except for the generated `speexdsp_config_types.h`; `speex_alloc_psram.h` is
force-included to put SpeexDSP's allocations in PSRAM through its documented
`OVERRIDE_SPEEX_*` hooks.

Used when `CONFIG_TAB5_ECHO_SPEEX` is selected (see `main/Kconfig.projbuild`).
