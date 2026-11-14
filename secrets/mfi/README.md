# 实验性软件认证材料

这三份材料随仓库提交，供可选 iAP2 软件认证构建使用：

| 文件 | 用途 |
|---|---|
| [identity.pk8](identity.pk8) | PKCS#8 P-256 私钥；这是公开共享的实验身份，不是保密密钥 |
| [certificate.p7b](certificate.p7b) | 手机认证使用的 PKCS#7 证书容器 |
| [certificate.der](certificate.der) | 从上述容器提取的 DER 证书，用于公私钥匹配与签名自检 |

克隆当前仓库即可取得；单独下载时须保持文件名及 `secrets/mfi/` 目录结构。不要将网页 HTML 保存为密钥文件。文件大小与 SHA-256 见 [SHA256SUMS](SHA256SUMS)。

在仓库根目录运行：

```sh
(cd secrets/mfi && shasum -a 256 -c SHA256SUMS)
source <esp-idf-v5.5.3>/export.sh
python3 scripts/fetch_openh264.py
./scripts/build.sh --software-auth
```

材料来源于 DiPlay v0.2.21 官方 Release APK 的 `assets/offline-mfi`；APK SHA-256、溯源边界、构建和实机证据见[软件认证接入](../../docs/软件认证接入.md)。证书 DER 由该 PKCS#7 容器提取。材料未因本项目 GPL 源码许可而被重新许可；附带材料不代表 Apple/MFi 授权或认证，也不保证各机型/iOS 持续接受该身份。

本目录名称沿用现有构建路径。这里列出的身份已经公开，不能用于需要私钥保密性的生产身份或作为设备独有身份。其他个人/设备凭据、NVS 配对记录和日志不属于这些文件，仍不应提交。
