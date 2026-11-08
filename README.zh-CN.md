# CarPlay-Tab5

[English](README.md) | 简体中文

在 M5Stack Tab5（ESP32-P4）上运行的有线 Apple CarPlay 接收端。iPhone 插在 Tab5 的 USB-A 口上，视频、触摸和 CarPlay 会话全部在 ESP32-P4 上处理。芯片没有硬件 H.264 解码器，视频用 OpenH264 软件解码。

> **实验性项目。** 本项目用于个人学习和研究，与 Apple 无关，也未经 Apple 认可。通过 iAP2 认证需要一套身份材料，这套材料**不在**本仓库中（见[认证](#认证)）。

## 当前状态

当前固件（2026-11-08）：890×240，16 帧/秒，支持电池充电、CarPlay 音频和麦克风。

| 项目 | 状态 |
|---|---|
| USB 枚举、USBMUX、Lockdown 配对和 TLS | 测试用 iPhone 上可用 |
| iAP2 认证和身份识别，启动 CarPlay 会话 | 使用本地实验身份时可用 |
| H.264 视频（High Profile，CABAC） | 890×240 下约 15 帧/秒，10 分钟拖动浸泡 0 拒收 |
| 触摸（HID 触摸板） | 可用，点击位置已在实机上确认 |
| 物理拔插 | 约 6 秒恢复，内存无泄漏 |
| 电池充电和电池供电 | 约 950 mA 充电；拔掉 USB-C 后能继续运行 |
| 音乐从 Tab5 喇叭播放 | 可用：48 kHz 立体声 PCM，测试中 0 欠载、0 丢包 |
| Siri（Tab5 麦克风和喇叭） | 可用，实测 Siri 能听懂并回答 |
| 导航提示、电话 | 未在实机验证 |

890×240@16 下 10 分钟模拟拖动的实测：

| 显示间隔 P50 | P90 | P95 | P99 | ≥ 200 ms |
|---|---|---|---|---|
| 66–67 ms | 81–84 ms | 91–99 ms | 119–137 ms | 0 次 |

## 硬件

- M5Stack Tab5，ESP32-P4 **v1.x 版本**。固件按 v3 以下版本编译（`CONFIG_ESP32P4_SELECTS_REV_LESS_V3`），主频 360 MHz，没有超频。
- iPhone，用线接到 Tab5 的 **USB-A**（主机）口。
- USB-C 用于供电和串口。NP-F550 电池可选。

## 工作原理

```text
iPhone ──USB──> NCM 网络 ──TCP──> RTSP / 屏幕流
                                    │
                                    ▼
             视频队列（6 帧）→ OpenH264
   核心 1：CABAC 解析 ── 帧级流水线 ── 核心 0：重建、去块、
                                       边界扩展、PIE 打包和锐化
                                    │
                                    ▼
        PPA：YUV→RGB，缩放 23/16 × 3，旋转 → MIPI-DSI 1280×720
```

- **非正方形像素布局。** 向手机申请 890×240 像素，但声明屏幕是 16:9。iOS 会按 890×500 排版，发来横向压缩的画面，再由 PPA 拉回全屏。像素少了，帧率更高，图标比例也不变。
- **帧级流水线。** 核心 1 解析下一帧时，核心 0 重建上一帧，用两套宏块数组和两份阶段上下文。遇到多 slice、出错等不常见情况，流水线先排空，再走上游原路径。
- **PIE SIMD。** 打包成 PPA 的 YUV420 格式和纵向亮度锐化，都用 ESP32-P4 的 PIE 指令实现（`main/core/pixel_pie.S`）。结果与 C 版本逐字节一致，锐化每帧约 0.6 ms。
- **L2 缓存自动预取**覆盖 PSRAM，每帧快约 7%。
- **音频。** 接收端必须同时提供麦克风，手机才会把 CarPlay 声音交给它（见 DiPlay issue #288），所以 `/info` 同时声明了 PCM 输入和输出。喇叭（ES8388）和麦克风（ES7210）共用一路 48 kHz 立体声全双工 I2S。`main/audio_output.c` 混合主音频和导航提示两路，导航提示时压低音乐。其他采样率由 `main/core/audio_resample.c` 转换。麦克风上行与下行一样，用 ChaCha20-Poly1305 加密。详见 `docs/音频输入输出方案.md`。
- **USB 主机修复。** ESP-IDF v5.5.3 在 USB 通道报错但未停止时会断言重启（espressif/esp-idf#19111）。`main/usb_hal_fix.c` 在链接阶段替换了这个函数。如果 NCM 链路仍然失败，`main/usb_probe.c` 会给 USB 口短暂断电，让手机重新枚举，每分钟最多 3 次。
- **充电。** `main/power_status.c` 通过第二颗 IO 扩展芯片打开 IP2326 充电器，板级支持包不会做这件事。它还每分钟从 INA226 读一次电池电压和电流并写入日志。

## 目录结构

| 路径 | 内容 |
|---|---|
| `main/` | 应用：USB 主机和 NCM、USBMUX 和 Lockdown、iAP2、视频输出、触摸/HID、音频、界面、电源 |
| `main/core/` | 与硬件无关、有主机测试的代码：会话、视频队列和解码封装、像素转换、触摸映射、RTSP 流 |
| `components/` | AirPlay 控制、RTSP、配对、屏幕流、时间同步、软件身份、认证接口、Ed25519、OpenH264 移植（`openh264_sw`） |
| `components/airplay_software_provider/assets/` | 每个视频档位一份 `/info` plist，内含 HID 触摸描述符 |
| `tests/` | 主机测试和测试数据 |
| `scripts/` | 构建、下载 OpenH264、主机/解码/加密测试脚本 |
| `tools/perf/` | 测量工具：日志统计、导出帧、临时源码钩子、回放和 CRC 校验 |
| `docs/` | 设计文档和完整开发记录 |

文档：
- [执行记录](docs/执行记录.md)：每次改动、测量数据，以及做过又撤回的尝试。
- [技术实现方案](docs/技术实现方案.md)
- [实机验收步骤](docs/实机验收.md)
- [软件认证接入](docs/软件认证接入.md)
- [音频输入输出方案](docs/音频输入输出方案.md)

## 构建

需要 ESP-IDF **v5.5.3**。其他组件版本由 `dependencies.lock` 固定（Tab5 BSP 1.3.2、LVGL 9.3.0）。

```sh
source <esp-idf-v5.5.3>/export.sh
python3 scripts/fetch_openh264.py      # 把固定版本的 OpenH264 下载到 external/
./scripts/build.sh                     # 或 idf.py build
```

视频高度由 `CONFIG_TAB5_VIDEO_PROFILE_HEIGHT` 决定，默认 240，即 890×240 档位。其他选项见 `main/Kconfig.projbuild`。

### 认证

手机要先通过 iAP2 认证才会启动 CarPlay。本项目可以嵌入一套实验性软件身份，构建方法：

1. 把 `identity.pk8`、`certificate.p7b`、`certificate.der` 放进 `secrets/mfi/`。这个目录已被 git 忽略，里面的内容不会发布。
2. 配置时加 `-DTAB5_SOFTWARE_AUTH=ON`。

没有这套身份，固件也能编译和运行，但手机会停在 `authentication_identity_missing`。这个选项会缓存在 `build/CMakeCache.txt`，重新配置后要检查一下。带身份构建出的固件里有可提取的密钥材料，不要分发。来源说明见 `docs/软件认证接入.md`。

### 烧录

只写应用分区，保留 NVS（里面存着手机配对记录和 AirPlay 身份）：

```sh
python -m esptool --chip esp32p4 -p <串口> write_flash 0x10000 build/carplay_tab5.bin
```

## 测试

```sh
./scripts/test_host.sh       # 主机单元测试（会话、USB、iAP2、RTSP、像素转换、触摸等）
./scripts/test_decoder.sh    # 用合成的 High Profile 测试流做解码回归（先编译固件）
./scripts/test_crypto.sh     # 用 ESP-IDF 自带的 Mbed TLS 源码做加密测试
```

`tools/perf/replay/` 的逐帧 CRC 回放用 iPhone 实录码流做过验证。实录码流里有手机屏幕内容，所以不公开。

## 遗留问题

- **帧率受手机限制。** iPhone 大约每 20 帧就有 1 帧晚到或被跳过。同样条件下，新旧解码器都只有 15.1–15.4 帧/秒，16 帧档没有达到 15.8 帧/秒的目标。240@17 能到 15.7 帧/秒，但大约每 10 分钟队列溢出一次，延迟也更高。
- **画面仍偏软。** 手机只发 240 行，PPA 用双线性纵向放大 3 倍。锐化有改善，但小字没有原生分辨率清楚。
- **240 档刚连上时可能多一次拒收或 IDR。** 以前的测试里，有时在头 1–2 秒内会多 1 次拒收和 IDR，表现为刚出画面时卡一下。最近一次拔插测试没有出现，原因未查。
- **USB-A 长时间断电后，iPhone 不会重新枚举。** 5 V 口长时间断电后，需要手动拔插手机。短时断电和正常拔插约 6 秒恢复。
- **USB 主机错误。** 负载高时导致重启的 ESP-IDF 断言已被拦截（见上文）。出错后自动重新枚举这条路径，还没在实机上触发过。
- **放音乐时视频变慢。** 自动滑动测试中约 13.6 帧/秒，不放音乐时约 15。帧间隔探针显示，手机传音频时视频发得更不均匀：跳帧和晚到都多了约 2.4 倍。原因不在 Tab5 的解码和 USB 接收：同时挂 4 个接收传输也没有改善。
- **音频限制。** 只声明了 PCM，没有 AAC 和 Opus。导航提示混音和电话还没测过，通话也还没有回声消除。喇叭音量固定为 30，麦克风增益固定为 30 dB。
- **电池。** 满电能用多久还没测。充电电流固定在 1000 mA 档。没有电量显示，也没有低电量关机。IP2326 的状态脚（P6）不能可靠反映是否在充电。
- **安全。** 没有启用 NVS 加密和 Flash 加密。AirPlay Pair-Setup 默认码是 `3939`。实验性 iAP2 身份可以从固件中提取。
- **兼容性。** 只在一台 iPhone（iOS 16）和一块 Tab5（P4 v1.3）上测过。只支持有线，没有实现无线 CarPlay。
- **真实的触摸到显示延迟**没有用相机测过，上面的延迟数据来自固件日志。
- **测量脚本只用于开发。** `tools/perf/add_*.py` 会临时修改源码，用完必须还原。负责烧录和抓日志的脚本依赖本机路径，没有放进仓库。

## 许可和致谢

本项目包含改编自 [xmz28/Carplay-S3](https://github.com/xmz28/Carplay-S3) 的代码，许可为 **GPL-3.0-or-later**，所以用本仓库构建的固件受 GPL 约束。其他部分保留各自的许可：OpenH264（BSD-2-Clause）、orlp/ed25519（zlib），以及 ESP-IDF、板级支持包和 LVGL 各自的许可。详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和 `licenses/`。

Apple、CarPlay、iPhone、AirPlay 是 Apple Inc. 的商标。
