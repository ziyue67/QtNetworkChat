# 发布、镜像与上线边界

当前项目用 CMake 构建 C++/Qt 程序，实际目标是 `QtNetworkChat`、`qqnt_server`、
`qqnt_engine` 和 `sqlite_to_postgres_migrator`。本文件替换早期错误的 Rust/npm、
NSIS 和旧分支验收说明；那些目标与命令不属于当前仓库。

## 交付来源

| 产物 | 来源 | 验证方式 |
|---|---|---|
| Windows x64 setup.exe | [Windows Build](../.github/workflows/windows-build.yml)，Inno Setup | Qt 构建、专项 CTest、GUI 截图、tag/手动构建时安装与启动烟测 |
| Ubuntu 24.04 amd64 deb | [Linux Build](../.github/workflows/linux-build.yml) | Qt 构建、专项 CTest、GUI 截图、包元数据与 tag/手动构建时安装烟测 |
| GHCR 服务端镜像 | [Container Image](../.github/workflows/container.yml) | 服务端编译与镜像构建、推送 sha-*、latest、版本 tag；不执行容器业务烟测 |
| 服务器运行状态 | [部署文档](../deploy/README.md) | 服务器读取实际镜像/健康状态，另行验证公网 TLS/WS 与应用协议 |

桌面 `v1.1.5` 来自源码 `290c4943a6d17b58b966282686cc95737bab6a66`。
已于 2026-10-04 23:49:04 +08:00 [正式发布](https://github.com/ziyue67/QtNetworkChat/releases/tag/v1.1.5)。
两平台 tag CI 已通过构建、19 项专项、真实系统选择器和安装启动；Linux 另
通过真实 PG16/Redis7 与引擎连续五轮复跑。发布附件只提供 exe 和 deb；GitHub 自动生成的源码包属于
平台功能。Windows 安装程序未签名，安装启动烟测没有验证
SmartScreen 信任或签名证书。

| 附件 | 字节数 | GitHub SHA-256 |
|---|---:|---|
| `QtNetworkChat-1.1.5-win-x64-setup.exe` | 21,218,367 | `b3f2ab77d036280a9b05ac8863067b8e750b881e134475aa8544559385653a5f` |
| `QtNetworkChat-1.1.5-linux-amd64.deb` | 1,983,998 | `fc52d5b1b8254823c6e0978794443e2dc1d6e7305621de2cc3ced4239a47614d` |

发布来源为同一源码的 [Windows tag run](https://github.com/ziyue67/QtNetworkChat/actions/runs/37212688308)
与 [Linux tag run](https://github.com/ziyue67/QtNetworkChat/actions/runs/37212688307)。
本轮取消了已被新提交替代、仍运行的旧 Windows 构建，防止旧产物覆盖附件。
公开后独立 [Windows Release Smoke](https://github.com/ziyue67/QtNetworkChat/actions/runs/37214462329)
也成功：从 Release 下载 exe，核对 GitHub SHA-256 和 NotSigned，在未安装
Qt SDK 的 runner 上安装、启动 8 秒并卸载。这没有验证 SmartScreen 信任。

代码提交到 main 会运行 CI，并在容器 workflow 匹配变更路径时发布服务端镜像。
版本 tag 才是桌面正式发布入口。更新代码、发布镜像、重发桌面 Release、升级
线上容器是四个不同事件。

## 当前整改与运维

本轮 E2E/窗口/Redis 拆分、GUI 流程与脚本整合见
[整改记录](refactoring-closeout.md)；本机全量与 CI 专项的覆盖边界见
[测试文档](testing-coverage.md)。不能把旧的 97-test 数字当成当前验证结果。

2026-10-04 已将生产服务升级并固定为 `sha-dd0becc571c9`，公网
域名为 `qt.ziyuexc.top`。现场检查了运行镜像 revision/digest、容器健康、严格
TLS/HTTP 101、注册/登录、私聊/离线重放与真实 PostgreSQL/Redis；旧服务保持
原容器和启动时间。配置、数据与旧镜像已备份，数据库在临时库恢复验证通过。
具体证据与回滚命令见 [上线记录](production-deployment-2026-10-04.md)。
桌面安装包发布结果见 [计划状态](plan-completion-status.md)，服务器更新没有
自动重建安装包。
桌面发布版与运行容器的服务端业务源码一致，后续提交只更改 Windows 原生测试
驱动和日志/CI 设置；线上已包含同账号交叠重连的路由修复。

Windows 编译一度触及 30 分钟限时；维护工作流现支持 `/MP2` 编译、45 分钟
构建限时和实时日志，并可按指定源码 tag 重建。工作流与应用源码可明确追踪：

```bash
gh workflow run windows-build.yml --repo ziyue67/QtNetworkChat --ref main \
  -f source_ref=v1.1.5
```

该入口记录实际 checkout 的 SHA，核对源码 tag 与安装包版本，保留生产 E2E、
GUI/原生和安装启动检查，通过后直接把 exe 上传到对应 Release。它不移动 tag，
不把 main 的不同应用源码混入指定版本。
