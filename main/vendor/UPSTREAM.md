# iAP2 源码来源

本项目直接移植自 [xmz28/Carplay-S3 固定提交](https://github.com/xmz28/Carplay-S3/tree/79a9f169f275b283f2909c5a326ed03c716c0d02)。
上游核心保留 DiPlay / ESP32 CarPlay Web Relay 贡献者版权及 GPL-3.0-or-later 声明。

Tab5 适配使用 ESP32-P4/ESP-IDF，保留链路边界、ACK、重传与解析测试；认证客户端通过 components/auth_port 接口请求证书和签名。
当前工程没有注册真实认证 provider，没有复制或嵌入认证私钥/证书，也没有发布固件至 Release。
主机模拟 provider 仅用于测试，不能用于真实手机认证。具体本地改动和许可证见 ../../THIRD_PARTY_NOTICES.md。
