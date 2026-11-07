# 来源及本地边界

`ntb16.h`、`airplay_rtsp_codec.c/h` 来自 `xmz28/Carplay-S3` commit
`79a9f169f275b283f2909c5a326ed03c716c0d02`，分别对应 `src/ntb16.h` 和
`components/airplay_rtsp/`。保留 GPL-3.0-or-later 与原版权声明。
`tests/test_ntb16.cpp` 同源（调整 main 链接声明以通过 macOS Clang）。
许可证见根目录 `licenses/Carplay-S3-GPL-3.0.txt`。

`session.c/h`、`rtsp_stream.c/h`、`touch.c/h` 为本项目新增模块。
当前协议核心与物理传输适配分开验证；独立测试通过不代表 NCM 已建立或
手机已经完成 RTSP 会话。RTSP 回调中的请求/正文均为短期视图，使用者必须
在回调返回前处理或复制。所有核心状态由单一任务持有。
