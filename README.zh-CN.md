# CarPlay-Tab5 · v1.0.0

[English](README.md) | 简体中文

运行在 M5Stack Tab5（ESP32-P4）上的有线 CarPlay 接收端。iPhone 接 USB-A，Tab5 自行处理配对、音视频、触摸和设置页面。视频由 OpenH264 软件解码。

本项目是独立实验项目，未获 Apple 认证。**源码不包含认证身份；本地带认证固件包含可提取的实验性私钥，不能直接公开分发。无认证构建不能进入 CarPlay。** 详见[软件认证说明](docs/软件认证接入.md)。

## v1.0.0 功能

- USBMUX / Lockdown 配对、TLS、iAP2 认证与有线 CarPlay 会话。
- 890×240、目标 16 fps 的视频输出，PPA 缩放到 1280×720；支持触摸操作。
- 音乐、导航语音混音及压低音乐、麦克风上行；Siri 已有实机使用验证。
- CarPlay 的「Tab5 设置」入口提供音量、亮度调节，重启后保留。
- 等待连接页的 **Clear pairing** 按钮：二次确认，拔线并退出旧会话后，仅清除手机信任记录。
- USB 通道错误处理、TX / RX 停滞检测、端口断电恢复与独立 15 秒任务进度监护。
- SD 卡运行日志和异常记录；IP2326 充电控制与电池供电。

版本、升级步骤和测试边界见 [v1.0.0 发布说明](docs/releases/v1.0.0.md)。

## 硬件与换机

目标硬件为 **Tab5 / ESP32-P4 v1.x / 16 MiB Flash / 32 MiB PSRAM**，360 MHz。未验证 P4 v3 或其他板卡。USB-C 用于电脑/供电，USB-A 接手机。

换手机建议：**拔下旧手机 → Clear pairing → 确认清理 → 重启 Tab5 → 连接已解锁的新手机并确认信任**。iPhone 17 Pro / iOS 26.5 曾出现 Clear 后仍无法连接、仅重启 Tab5 后成功；原因尚未定位，v1.0.0 不宣称已修复。当前配对空间有限，不支持无限保存多部手机。

## 构建

使用 **ESP-IDF v5.5.3**；其他依赖由 `dependencies.lock` 固定。

```sh
source <esp-idf-v5.5.3>/export.sh
python3 scripts/fetch_openh264.py
./scripts/build.sh                      # 明确关闭实验认证，开发构建
./scripts/build.sh --software-auth      # 使用本机 secrets/mfi，不能公开该固件
```

带认证构建需要本机提供 `secrets/mfi/identity.pk8`、`certificate.p7b`、`certificate.der`；该目录被 Git 忽略。认证选项会被 CMake 缓存，构建脚本始终显式设置 ON/OFF。

发布构建使用独立目录，从已跟踪的 `sdkconfig.defaults` 重新生成配置：

```sh
python scripts/release.py --auth local  # 本地可用的实验认证固件
python scripts/release.py --auth none   # 不包含身份、不能进入 CarPlay 的开发固件
```

产物位于 `artifacts/releases/v1.0.0/{local-auth,no-auth}/`，包含应用 BIN、全量 BIN、版本信息、配置、许可证、manifest 和 SHA256SUMS。`version.txt` 是固件版本来源。保留已验证的编译与性能参数，本版不引入新的全局优化档位。

## 烧录

**升级已有设备只写应用区**，保留配对、音量、亮度及其他 NVS 资料：

```sh
python -m esptool --chip esp32p4 -p <串口> write_flash 0x10000 <版本对应的-app.bin>
```

`-full.bin` 从 **0x0** 烧录，仅用于新设备初始化；会覆盖引导程序、分区表及其覆盖范围内的旧配对/设置。不能将应用 BIN 烧到 0x0，也不能将全量 BIN 烧到 0x10000。发布分区表使用 24 KiB NVS、4 MiB factory；已有设备的不同 factory 容量不需要通过升级改写。

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

- iPhone 12 Pro Max / iOS 16.1.1 已验证清理、重新配对、视频和音乐。iPhone 17 Pro / iOS 26.5 只有用户确认的“重启 Tab5 后成功”，未取得失败日志。
- 既有约一小时音乐/导航日志中有一次非人为断开，之后自动恢复；不承诺零掉线。USB 故障恢复已完成受控注入验证，但真实线材/供电问题仍可能影响连接。
- 屏幕源画面只有 240 行，软件解码帧率有限；音乐播放时帧率可能下降。偶发视频内存不足拒收仍有记录。
- I2S 开流时可能出现已知 disable 日志噪声。冷启动 reset_reason 在该板上可能显示 other_watchdog，不能单独当成运行中看门狗故障。
- 仅支持有线和 PCM；电话完整流程、回声消除、多小时续航和低电量关机未完成验证。无无线 CarPlay。
- 未启用 Flash/NVS 加密；认证密钥、配对数据和本机采集日志不可随源码包上传。AirPlay Pair-Setup 默认码为 `3939`。

## 许可

包含改编自 [xmz28/Carplay-S3](https://github.com/xmz28/Carplay-S3) 的 GPL-3.0-or-later 代码；OpenH264、Ed25519、ESP-IDF、BSP 和 LVGL 保留各自许可。见 [LICENSE](LICENSE)、[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和 `licenses/`。认证材料本身不因本项目源码许可而获得重新分发授权。

Apple、CarPlay、iPhone、AirPlay 是 Apple Inc. 的商标。
