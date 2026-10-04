# 自动化入口与状态核查

当前自动化在 GitHub Actions 中运行，流程定义就是可复核的来源。本文说明触发
与职责；某个提交是否成功，必须查看对应提交的 run，不能把本文当作实时监控。

| 流程 | 触发 | 职责 |
|---|---|---|
| [Linux Build](../.github/workflows/linux-build.yml) | main、PR、v* tag、手动 | 生产 provider 配置编译、13 个专项 CTest、GUI 截图、脚本回归；tag/手动构建制作 deb |
| [Windows Build](../.github/workflows/windows-build.yml) | main、PR、v* tag、手动 | Qt/MSVC 编译、同一专项 CTest、GUI 字体/截图、脚本回归；tag/手动构建制作 Inno Setup 安装包 |
| [Container Image](../.github/workflows/container.yml) | 匹配代码/部署路径的 main 或 tag、手动 | 编译服务端并构建镜像，发布至 GHCR；不执行容器业务烟测 |
| [Windows Release Smoke](../.github/workflows/windows-release-smoke.yml) | 以流程文件的触发和参数为准 | 下载已发布安装包，在 Windows 上做安装/启动/卸载验收 |

本机 Linux 默认完整套件包含 55 个 CTest；Windows 另注册平台脚本检查，
上述 CI 列表是专项子集。脚本回归是单独
步骤，解析所有 69 个 PowerShell 文件，并验证路径、哈希、实际证据生成和
README 索引，详见 [脚本维护](script-maintenance.md)。

```bash
gh run list --repo ziyue67/QtNetworkChat --branch main --limit 10
gh run view <run-id> --repo ziyue67/QtNetworkChat
```

核对 run 的 headSha 与目标提交是否一致，读取 conclusion 与失败日志。镜像还
需核对 revision 标签和 digest；线上服务还需读取实际运行的镜像。成功的工作流
不会自动升级固定标签的生产容器。

`write-automation-status.ps1`、证据包和计划任务脚本保留原有报告接口。它们生成
本地档案或运维配置，不能替代 GitHub run、真实对象存储或服务器的现场证据。
当前整改结果见 [整改记录](refactoring-closeout.md)，发布边界见
[发布说明](release-closeout.md)。
