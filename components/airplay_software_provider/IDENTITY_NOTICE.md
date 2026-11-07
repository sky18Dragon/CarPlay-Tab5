# AirPlay 软件身份说明

该组件为开发测试提供 AirPlay receiver identity，不需要外置 MFi 芯片，也不把 MFi challenge 跳过或伪装成成功。iAP2 MFi challenge-response 与 AirPlay Pair-Setup/Pair-Verify 是独立流程。

## 身份与存储

- 首次启动时，组件使用 ESP-IDF 硬件随机源生成 32 字节 Ed25519 seed 和随机 pairing UUID，将它们保存到 `pairing` NVS 分区的 `airplay_id` namespace。后续启动读取同一份身份，以保持配对关系稳定。
- 公钥、device ID、`/info` binary plist 和 `_airplay._tcp` TXT 数据由该身份派生或生成；TXT 数据的生成不等于已有 mDNS 广播实测。当前 `/info` 声明主副两屏，默认 480×288 / 6 FPS，主屏支持 Touch HID；四档尺寸及帧率以 `assets/video_profiles.json` 为准，物理 LCD 为 800×480。
- Pair-Setup 默认码为 `3939`，属于源码中的开发默认值，可通过 Kconfig 的 `Wired AirPlay → Pair-Setup code`（`CONFIG_AIRPLAY_PAIRING_CODE`）修改。
- 当前固件没有启用 NVS encryption 或 Flash encryption。拿到设备或 Flash 镜像的人可以读取 NVS 中的 seed；这是实验实现，不适合公开部署或存放敏感身份。

## 协议实现与限制

- Pair-Setup 按 SRP-6a 和 Ed25519 accessory 签名处理配对；Pair-Verify 使用已配对 controller 公钥验签，并派生会话密钥。controller 公钥记录单独存入 NVS，最多保存 8 个。
- Ed25519 使用 `orlp/ed25519` 上游实现，revision `b1f19fab4aebe607805620d25a5e42566ce46a0e`，保留 zlib 许可证与上游版权信息，见 `components/ed25519/`。
- 该代码按 DiPlay 的协议行为构建，不代表 Apple 签发或批准的 AirPlay receiver 身份，也不保证其他 iOS 版本会接受。本工程已有同板、同一 iPhone 的 AirPlay 会话、真实视频、触摸与本地菜单历史实测，见 [验证记录](../../docs/验证记录.md)。这不构成通用兼容性、mDNS、真实音乐 PCM 接收、I2S 出声或麦克风上行的通过证明。

不要将实际使用的非默认 Pair-Setup 码、seed、私钥或设备镜像提交到公开仓库、问题报告或日志。若要部署到共享网络，应先更改默认配对码并为目标硬件评估 NVS/Flash 加密。iAP2 软件认证材料来自本机忽略目录 `secrets/mfi/`，嵌入后可从固件提取；它与本组件生成的 AirPlay 身份、板上 Lockdown 配对身份是三套不同材料。

完整 Release 固件按维护者要求嵌入与实测相同的 iAP2 实验身份，访客不需要导入认证文件；源码继续排除原材料。它不是本组件的 Ed25519 seed。材料与 DiPlay 本机输入一致，具体来源链见 [认证材料来源](../../docs/认证材料来源.md)。
