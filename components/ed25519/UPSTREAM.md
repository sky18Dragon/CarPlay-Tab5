# Ed25519 实现来源

本组件引入以下项目的可移植 Ed25519 实现：
[`orlp/ed25519`](https://github.com/orlp/ed25519)，固定版本为
`b1f19fab4aebe607805620d25a5e42566ce46a0e`。

上游 C 源码保持原样，采用 zlib 许可证，见 [LICENSE.txt](LICENSE.txt)。
组件仅使用密钥生成、签名和验签源码；seed 通过平台适配层使用 ESP-IDF 硬件随机源生成。
上游原始说明保留在 [UPSTREAM_README.md](UPSTREAM_README.md)。
