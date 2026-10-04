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

桌面 Release 目前保留 `v1.1.4`，发布附件只提供 exe 和 deb；GitHub 自动生成
的源码包属于平台功能。Windows 安装程序未签名，安装启动烟测没有验证
SmartScreen 信任或签名证书。

代码提交到 main 会运行 CI，并在容器 workflow 匹配变更路径时发布服务端镜像。
版本 tag 才是桌面正式发布入口。更新代码、发布镜像、重发桌面 Release、升级
线上容器是四个不同事件。

## 当前整改与运维

本轮 E2E/窗口/Redis 拆分、GUI 流程与脚本整合见
[整改记录](refactoring-closeout.md)；本机全量与 CI 专项的覆盖边界见
[测试文档](testing-coverage.md)。不能把旧的 97-test 数字当成当前验证结果。

2026-10-04 已将生产服务从 `v1.1.0` 升级并固定为 `sha-3eeb955b25a0`，公网
域名为 `qt.ziyuexc.top`。现场检查了运行镜像 revision/digest、容器健康、严格
TLS/HTTP 101、注册/登录、私聊/离线重放与真实 PostgreSQL/Redis；旧服务保持
原容器和启动时间。配置、数据与旧镜像已备份，数据库在临时库恢复验证通过。
具体证据与回滚命令见 [上线记录](production-deployment-2026-10-04.md)。
桌面安装包仍为 `v1.1.4`；发布最新 exe/deb 属于
[剩余收尾](plan-completion-status.md)，没有由本次服务器升级自动完成。
