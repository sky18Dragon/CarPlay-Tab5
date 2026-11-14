# Contributing

Create a feature branch and open a pull request to `main`. Direct pushes,
force pushes and deletion of `main` are blocked, including for administrators.
Resolve review conversations, update the branch with current `main`, and pass
`Required CI` before merging. Use squash merge; merged feature branches are
automatically deleted.

The repository currently has one maintainer, so a second person's approval is
not mandatory. Add required approvals when another reviewer is available.

## Local CLI checks

```sh
./scripts/check_cli.sh
./scripts/test_host.sh
# Optional: check all commits against the PR base
BASE_SHA=origin/main ./scripts/check_cli.sh
```

CI validates Python/shell syntax, changed-line whitespace and Actions workflows,
then runs the host suite with warnings treated as errors and ASan/UBSan. A
separate ESP-IDF v5.5.3 job fetches verified OpenH264 sources, builds ESP32-P4
firmware, and runs decoder and crypto regressions. All jobs run on every PR,
including documentation-only changes; failed, cancelled or skipped jobs fail
the required aggregate gate. Workflows use read-only repository permissions
and do not receive local identity files or other secrets.

Firmware build instructions are in README.md. Passing CI does not establish
USB, CarPlay, audio/video or touchscreen behavior on real hardware. Record
hardware results and remaining limitations in the PR.

## 中文说明

所有变更先推送到功能分支，再通过 PR 合入 main。main 禁止直接推送、强推和删除，
管理员同样受约束。合并前须解决评审对话、同步最新 main，并通过 Required CI。
当前只有一位维护者，不强制第二人批准；增加协作者后可再启用审批要求。

本地运行上面的 CLI 和主机测试命令。CI 还会校验工作流、编译固件并执行解码与
加密回归。只能使用 squash 合并，合并后自动删除功能分支。
CI 通过不代表真机验证通过，请在 PR 中说明真机测试结果和未验证项目。
