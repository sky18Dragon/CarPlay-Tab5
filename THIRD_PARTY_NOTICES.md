# Third-party source

`main/usbmux.cpp`, `main/usbmux.h`, `main/lockdown_reply.h`, `main/mux_channels.cpp`, `main/mux_channels.h`,
`main/lockdown_client.cpp`, `main/lockdown_client.h`, and `main/lockdown_xml.h` are adapted from
[xmz28/Carplay-S3](https://github.com/xmz28/Carplay-S3/tree/79a9f169f275b283f2909c5a326ed03c716c0d02),
commit 79a9f169f275b283f2909c5a326ed03c716c0d02. Source files specify GPL-3.0-or-later;
license text is retained at `licenses/Carplay-S3-GPL-3.0.txt`.

Tab5 modifications: accept 64/512-byte bulk endpoints, expose a C ABI and periodic
status, rename the client label, add single-phone pairing persistence in the existing
NVS namespace `tab5_pair` without erase/migration, and retain TLS through the CarKit/iAP2 session. Multi-stream transport is included
for the pairing worker. CarKit/iAP2 progression uses an explicit auth provider. XML helper routines are extracted for host tests.
No external authentication identities or release firmware are imported. The pairing
build has been flashed with explicit user authorization; pairing, TLS QueryType and
reboot identity reuse have been verified on the connected phone.

The resulting firmware includes GPL-covered code; redistribution must comply with
applicable GPL source/license obligations. This notice does not relicense third-party
BSP/SDK libraries, whose own notices remain in their packages.


## iAP2 link diagnostic

`main/vendor/iap2_core.c` and `.h`, `main/iap2_client.h`, and
`tests/iap2_core_test.c` are copied from the same pinned Carplay-S3 commit.
The core retains the DiPlay contributors and ESP32 CarPlay Web Relay contributors
copyright notices and GPL-3.0-or-later terms. Upstream provenance is retained in
`main/vendor/UPSTREAM.md`. `main/iap2_client.cpp` now adapts the upstream authentication, identification and
media-session control flow through a provider API; no real credentials are included. The CarKit StartService block is adapted from the
upstream Lockdown client, with validation of the optional SSL flag.

## 离线协议核心（2026-11-06）

`main/core/ntb16.h`、`main/core/airplay_rtsp_codec.c/h` 和
`tests/test_ntb16.cpp` 移植自同一固定 Carplay-S3 提交
`79a9f169f275b283f2909c5a326ed03c716c0d02`，按 GPL-3.0-or-later 保留，
详见 `main/core/UPSTREAM.md`。

## OpenH264 与合成视频测试（2026-11-06）

Cisco OpenH264 v2.6.0（commit `652bdb7719f30b52b08e506645a7322ff1b2cc6f`）
按 BSD-2-Clause 使用；许可证在 `components/openh264_sw/LICENSE`。
下载工具、组件 CMake 适配和 `tests/assets/high_fixture.bin`、`high_ltr.bin`、
`high_ltr_crc.h` 源于上述固定 Carplay-S3 提交，保留其 GPL-3.0-or-later
项目来源。测试素材是合成码流，不含用户媒体。参考值来自上游 FFmpeg 校验；
本项目另外执行 OpenH264 解码逐帧比较，不把参考值本身当作通过证据。

## NCM 与 AirPlay 协议组件（开发集成中）

`main/ncm_host.cpp/h`、`components/airplay_*`（control、timing、screen、
media_bridge、session、pairing、rtsp）、`components/net_port`、`time_port`、
`media_bridge` 和 `auth_port` 来源于同一固定 Carplay-S3 提交
`79a9f169f275b283f2909c5a326ed03c716c0d02`，保留 GPL-3.0-or-later 声明。
未复制软件认证私钥或证书。auth_port 未注册 provider 时明确返回不支持。
本地 NCM 改用 CDC Union 关联，并支持高速端点，修正分配失败回收和数据端点停止。
协议组件复制和编译不等于服务已启动；运行路径及验证状态以执行记录为准。

## AirPlay 本地身份及 Ed25519

`airplay_identity_port`、`airplay_pairing_store`、`airplay_software_provider`
源于前述固定 Carplay-S3 提交。模板 plist 不含认证私钥。
`components/ed25519` 保留 Orson Peters 的 zlib 风格许可证
（`components/ed25519/LICENSE`），原始版权及来源文件保留。
本地改动将 AirPlay 接收器身份与控制器记录分别放入现有 NVS 的
`tab5_ap_id`、`tab5_ap_pair` 命名空间，与 Lockdown `tab5_pair` 分离。

`tests/test_airplay_bplist.c` 与 `tests/test_airplay_screen_codec.c` 亦移植自固定
Carplay-S3 提交，用于对本项目所编译的协议源码运行主机边界测试。
`configure_tab5_info.py` 是本地新增的能力模板规范化与检查脚本。

完整 `main/iap2_client.cpp` 控制流程和 `main/core/iap2_params.h`、
`tests/test_iap2_params.cpp` 来自同一固定上游提交；本地改为 Tab5 身份、
显式 NCM 接口配置、通用认证 provider 和本地服务生命周期，不复制认证凭据。

## 可选 iAP2 软件认证 provider

components/auth_software_provider 源于上述固定 Carplay-S3 提交 src/vendor/auth_software_provider.c/h，保留 GPL-3.0-or-later。本地增加可选构建、签名互斥锁、敏感临时缓冲清零和实际 raw 签名验证自检。实验身份材料独立保存于忽略目录，不作为 GPL 源码再许可。来源和构建边界见 docs/软件认证接入.md。

`components/openh264_sw/cabac_decoder_esp.cpp` and `cabac_bin_inline.h` are modified copies of OpenH264 v2.6.0 `cabac_decoder.cpp` (BSD-2-Clause), changed to use 32-bit arithmetic.
