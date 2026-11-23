# CarPlay-Tab5 · v1.0.0

[English](README.md) | 简体中文

运行在 M5Stack Tab5（ESP32-P4）上的有线 CarPlay 接收端。iPhone 接 USB-A，Tab5 自行处理配对、音视频、触摸和设置页面。视频由 OpenH264 软件解码。

本项目是独立实验项目，未获 Apple 认证。**当前仓库附带公开共享的实验性认证材料；带认证固件中的私钥可被提取，不能视为保密身份。无认证构建不能进入 CarPlay。** 详见[软件认证说明](docs/软件认证接入.md)。

## 功能

- USBMUX / Lockdown 配对、TLS、iAP2 认证与有线 CarPlay 会话。
- 890×240、目标 16 fps 的视频输出，PPA 缩放到 1280×720；支持触摸操作。可选更清晰的 1024×360 和 1024×576，帧率更低。
- 音乐、导航语音混音及压低音乐、麦克风上行；Siri 已有实机使用验证。
- 通话回声消除（构建时选择 ESP-SR 或 SpeexDSP，见下文）。
- CarPlay 的「Tab5 设置」入口：音量、亮度、麦克风音量、画质、画面锐化、未连接时自动熄屏、右舵（CarPlay 程序坞在右侧），均会保存。
- 等待页和设置页显示电量；低电量时在 CarPlay 画面上显示电池图标；电量耗尽时提示后自动关机。
- 等待连接页的 **Clear pairing** 按钮：二次确认，拔线并退出旧会话后，清除手机信任记录和 AirPlay 配对，不影响其他设置。
- USB 通道错误处理、TX / RX 停滞检测、端口断电恢复与独立 15 秒任务进度监护。
- SD 卡运行日志和异常记录；IP2326 充电控制。

版本、升级步骤和测试边界见 [v1.0.0 发布说明](docs/releases/v1.0.0.md)。

## 硬件与换机

本项目基于**搭载 ST7121 屏幕驱动 IC 的 M5Stack Tab5** 开发和验证。目标配置为 **ESP32-P4 v1.x（360 MHz）/ 16 MiB Flash / 32 MiB PSRAM**。未验证 P4 v3 或其他板卡。USB-C 用于电脑/供电，USB-A 接手机。

换手机建议：**拔下旧手机 → Clear pairing → 确认清理 → 重启 Tab5 → 连接已解锁的新手机并确认信任**。iPhone 17 Pro / iOS 26.5 曾出现 Clear 后仍无法连接、仅重启 Tab5 后成功；原因尚未定位，v1.0.0 不宣称已修复。当前配对空间有限，不支持无限保存多部手机。

## 构建

使用 **ESP-IDF v5.5.3**；其他依赖由 `dependencies.lock` 固定。

```sh
source <esp-idf-v5.5.3>/export.sh
python3 scripts/fetch_openh264.py
./scripts/build.sh                      # 明确关闭实验认证，开发构建
./scripts/build.sh --software-auth      # 使用仓库附带的 secrets/mfi 实验身份
```

当前仓库已附带 [secrets/mfi](secrets/mfi/README.md) 中的 `identity.pk8`、`certificate.p7b`、`certificate.der`，克隆当前 main 即可取得；获取与校验步骤见[软件认证接入](docs/软件认证接入.md)。v1.0.0 首发标签/附件保持原快照，不含这些材料。认证选项会被 CMake 缓存，构建脚本始终显式设置 ON/OFF。

### 回声消除引擎

`idf.py menuconfig` → **CarPlay Tab5** → **Echo cancellation for phone calls**：

- **ESP-SR**（默认）：乐鑫 `espressif/esp-sr` 的全双工回声消除，仅在选择时由组件管理器下载。效果最好，但库为乐鑫许可证下的闭源二进制，使用它的固件不能仅按 GPL 分发。
- **SpeexDSP**：`components/speexdsp` 中的 BSD 许可实现，适合需要分享的构建。
- **None**：关闭。

选择 SpeexDSP 或 None 构建会改写 `dependencies.lock`、去掉 ESP-SR，请勿提交该改动。

发布构建使用独立目录，从已跟踪的 `sdkconfig.defaults` 重新生成配置：

```sh
python scripts/release.py --auth local  # 本地可用的实验认证固件
python scripts/release.py --auth none   # 不包含身份、不能进入 CarPlay 的开发固件
```

产物位于 `artifacts/releases/v1.0.0/{local-auth,no-auth}/`，包含应用 BIN、全量 BIN、版本信息、配置、许可证、manifest 和 SHA256SUMS。`version.txt` 是固件版本来源。保留已验证的编译与性能参数，本版不引入新的全局优化档位。

## 烧录

**首次在 Tab5 上安装**（出厂固件、M5Stack 启动器或其他项目）：从 **0x0** 烧录全量镜像。它会同时安装本项目的引导程序、分区表和应用，并清除其覆盖范围内的旧配对/设置。

```sh
python -m esptool --chip esp32p4 -p <串口> write_flash 0x0 <版本对应的-full.bin>
```

**已运行 CarPlay-Tab5 的设备升级时只写应用区**，保留配对、音量、亮度及其他 NVS 资料：

```sh
python -m esptool --chip esp32p4 -p <串口> write_flash 0x10000 <版本对应的-app.bin>
```

只写应用区的前提是设备上已经是本项目的引导程序。其他引导程序的分区布局可能不同，设备会在 `entry 0x4ff29ed0` 之后循环重启且屏幕无显示；此时从 0x0 烧录全量镜像即可恢复。不确定时先备份：`python -m esptool --chip esp32p4 -p <串口> read_flash 0 0x1000000 tab5-backup.bin`。

不能将应用 BIN 烧到 0x0，也不能将全量 BIN 烧到 0x10000。发布分区表使用 24 KiB NVS、4 MiB factory；已运行本项目的设备升级时不需要改写分区表。

## 测试与代码组织

```sh
./scripts/test_host.sh
./scripts/test_ui.sh
./scripts/test_crypto.sh
./scripts/test_decoder.sh build-release-local-auth
```

| 目录 | 内容 |
|---|---|
| `main/` | 正式应用、USB/协议、音视频、UI、电源及 SD 日志 |
| `main/core/` | 可在主机测试的会话、队列、像素、触摸及协议逻辑 |
| `components/` | AirPlay、认证接口、软件身份提供者与 OpenH264 移植 |
| `tests/` | 持续回归测试、硬件桩和合成码流，仅在主机构建 |
| `scripts/` | 构建、发布、依赖获取及测试入口 |
| `tools/perf/` | 离线日志分析、码流回放和 CRC 工具，不进入固件 |
| `docs/` | 设计、发布说明与[执行记录](docs/执行记录.md) |

v1.0.0 移除了离线演示、开机音频测试、烧录式配对清除维护模式、应用中的模拟显示分支，以及 `tools/perf/add_*.py` 一次性源码改写脚本。合成码流仅保留给解码回归，不再嵌入应用。开发日志中的旧命令属于历史记录，不应直接用于本版。

## 已知限制

- 固件同一时间只连接一部 iPhone，配对存储约可保存两部手机。配对更多手机时会移除其他手机的记录，它们下次连接需重新信任。仍可在等待页用 **Clear pairing** 手动清除全部记录。
- iPhone 12 Pro Max / iOS 16.1.1 已验证清理、重新配对、视频和音乐；iPhone Duo 已验证视频、触摸、音乐和导航。iPhone 17 Pro / iOS 26.5 曾在 Clear pairing 后需重启 Tab5；可能阻止新手机连接的两个原因已修复，但该情形尚未复测。
- 既有约一小时音乐/导航日志中有一次非人为断开，之后自动恢复；不承诺零掉线。USB 故障恢复已完成受控注入验证，但真实线材/供电问题仍可能影响连接。
- 软件解码限制清晰度和帧率；音乐播放时帧率可能下降。偶发视频内存不足拒收仍有记录。
- 仅支持有线和 PCM。无无线 CarPlay：首次连接需要经典蓝牙，而 Tab5 的 ESP32-C6 不支持。
- 尚未实机验证：真实通话中的回声消除、低电量自动关机（若关机脉冲无效则进入深度睡眠）、电池续航。
- CarPlay 的浅色/深色外观在 CarPlay 自身的「设置 → 外观」中调整，Tab5 不提供此项。
- 未启用 Flash/NVS 加密；仓库附带的实验身份已公开；其他私钥、配对数据和本机采集日志仍不得随源码上传。AirPlay Pair-Setup 默认码为 `3939`。

## 许可

包含改编自 [xmz28/Carplay-S3](https://github.com/xmz28/Carplay-S3) 的 GPL-3.0-or-later 代码；OpenH264、Ed25519、ESP-IDF、BSP 和 LVGL 保留各自许可。见 [LICENSE](LICENSE)、[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和 `licenses/`。认证材料本身不因本项目源码许可而获得重新分发授权。

Apple、CarPlay、iPhone、AirPlay 是 Apple Inc. 的商标。
