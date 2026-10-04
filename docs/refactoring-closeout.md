# E2E、窗口、Redis 路由与 GUI 整改记录

本文记录 2026-10-04 在 `ddd54b253b2606d5c2d5f9798593ffa0f9ac75f2` 基础上
继续完成的四项整改。代码位置、实际验证和未覆盖的范围分别列出，旧计划里的
历史行数/测试数不能作为当前状态。

## 四项交付

| 项目 | 本轮结果 | 阅读入口 |
|---|---|---|
| E2E 状态层 | 选择/缓存、合同、provider 表、probe、调用报告、交接与验收拆成 7 个编译单元，保留现有公共 API 和报告格式 | [E2E 说明](e2e-hardening-status.md) |
| 窗口与 Redis 路由 | 窗口初始化、文件传输、历史/收藏、消息操作独立；Redis 普通路由、大文件对象路由、群/好友查询独立 | [模块结构](architecture.md) |
| GUI 主流程 | 新增同一桌面库上的真实网络流程测试，包括设置持久化、权限、取消、消息/文件和重连 | [流程测试代码](../tests/mainwindow_workflow_test.cpp) |
| 脚本整合 | 25 个调用方加载公共路径/哈希函数，删除 43 份重复实现，增加全量解析与真实调用方回归 | [脚本入口](../scripts/README.md) |

| 主文件 | 本轮之前 | 本轮之后 |
|---|---:|---:|
| `src/e2eenvelope.cpp` | 11,624 行 | 426 行 |
| `src/mainwindow.cpp` | 6,442 行 | 2,901 行 |
| `src/server.cpp` | 2,963 行 | 760 行 |

行数下降来自职责迁移，功能代码仍在仓库中。E2E 报告图保留原有字段和流程，
总逻辑复杂度没有因分文件消失。窗口和服务端仍共享原来的对象状态；新增文件
不是第二套窗口/服务端。CMake 和 qmake 均注册了拆出的源文件。

## GUI 流程究竟验证了什么

`MainWindowNetworkWorkflow` 使用本机随机端口的真实 TCP 服务端、内置 Redis
测试替身、临时 SQLite/下载目录和两个临时账号。测试和安装版客户端复用
`qtnetworkchat_desktop`，没有另写一套窗口业务逻辑。

1. 通过协议创建账号，再操作登录表单取得凭据并完成真实登录。
2. 首次进入公共群，直接点击群资料按钮；修改群名、保存发言限制。
3. 核对另一客户端的群快照，并直接查询 SQLite，确认保存发生在服务端。
4. 检查普通群成员没有管理编辑入口，取消群名修改不改变已保存值。
5. 点击发送聊天消息，等待本次接收信号，并检查接收窗口的消息卡片。
6. 取消真实文件选择器，不发送文件；随后发送 600 KiB 文件，验证分片接收、
   带时间戳文件名的自动保存、逐字节一致和接收卡片。
7. 捕获群设置、接收文件和深色主窗口截图。
8. 断线时保留草稿，重连后读取已保存的群设置并发送该草稿。

截图生成在测试程序旁，Linux/Windows Actions 上传 `mainwindow_workflow_*.png`。
它们来自隔离测试账号，不是公网生产账号。

## 流程测试发现的实际修复

- 首次登录默认处于公共群，但群资料入口未显示。会话界面刷新现在同步群资料
  与精华消息按钮的可见性，私聊仍隐藏群控件。
- 群设置选择器的颜色模板缺少第 6 个占位符，造成颜色错位和 QString 警告。
  现在补齐模板，测试对该类警告按失败处理。
- `HistoryService` 原先忽略 `QTNETWORKCHAT_APPDATA_DIR`，历史库与其它运行
  数据可能使用不同目录。数据库与旧文本历史现在遵守显式目录；未配置时仍
  使用原来的 Qt 默认目录。此修复不自动迁移旧目录的数据。

## 自己复验

本机验证环境为 Linux、Qt 6.10.2、GCC 15.2.0、CMake 4.2.3。
默认目录 `build-dev-check` 使用 Release、两项生产选项关闭；生产目录
`build-production-check` 使用 Debug、两项生产选项开启。生产专项还设置了
外部 `QTNETWORKCHAT_TRANSPORT=wss`、Noto 中文字体和 `QT_SCALE_FACTOR=2`，
本地网络 fixture 显式使用自己的 TCP 服务端，不能据此称为公网 WSS 验证。

2026-10-04 的实际结果：

| 检查 | 结果 | 范围 |
|---|---|---|
| 默认完整构建与 CTest | 构建成功；55/55 返回通过，277.28 秒 | 含本次 GUI、E2E、群/好友、Redis 跨实例、文件及引擎回归；真实 QPSQL 按默认条件跳过 |
| 链接 OpenSSL 的生产配置专项 | 4/4 通过，12.38 秒 | 可执行文件、真实生产 provider、2 倍缩放 GUI 主流程、引擎烟测 |
| PowerShell 7 脚本维护 | 69 个文件解析与路径/哈希/真实调用方/README 回归通过 | 本地临时证据，不注册计划任务、不对外发布 |
| qmake6 | 配置成功，生成 Makefile，新增源码已注册 | 本轮未完整编译 qmake 目标；完整编译使用 CMake |
| 工作流 YAML | 4 个文件语法解析通过 | 远端运行结果须按提交 SHA 查看 Actions |

`PostgresQpsqlProtocolSmoke` 只有设置 `QTNETWORKCHAT_RUN_REAL_QPSQL_TEST=1`
并提供 PostgreSQL 测试连接参数才执行真实数据库检查；默认 CTest 的“通过”
不能用于证明这一外部服务已经验证。Redis 网络测试使用仓库内测试替身，
也不能代替生产 Redis 的运维检查。

默认完整构建和测试：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

单独复验生产 provider 与 GUI：

```bash
cmake -S . -B build-production -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DQTNETWORKCHAT_E2E_ENABLE_PRODUCTION_CRYPTO=ON \
  -DQTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER=ON
cmake --build build-production --parallel 4 --target \
  QtNetworkChat mainwindow_workflow_test e2e_production_adapter_runtime_test \
  qqnt_engine qqnt_engine_smoke_test
QT_QPA_PLATFORM=offscreen QTNETWORKCHAT_TRANSPORT=wss QT_SCALE_FACTOR=2 \
  ctest --test-dir build-production --output-on-failure \
  -R '^(QtNetworkChatExecutableExists|MainWindowNetworkWorkflow|E2EProductionAdapterRuntime|QQNTEngineSmoke)$'
```

可额外设置 `QTNETWORKCHAT_TEST_FONT` 为本机 Noto CJK 字体文件的绝对路径。
Windows 使用 `--config Release` 和 `ctest -C Release`，环境变量使用 PowerShell
的 `$env:变量名` 语法，不能直接复制 Bash 的行首赋值。

只跑主流程和相关路由：

```bash
cmake --build build --parallel 4 --target mainwindow_workflow_test redis_cross_instance_chat_test
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure \
  -R '^(MainWindowNetworkWorkflow|RedisCrossInstanceChatRouting)$'
```

脚本检查：

```powershell
./scripts/test-script-maintenance.ps1
```

生产 provider 必须使用单独构建目录，并开启两项生产 CMake 选项，见
[E2E 说明](e2e-hardening-status.md)。CI 的专项检查与本机全量 CTest 是不同范围，
实际结果见 [测试覆盖](testing-coverage.md) 和对应 Actions run。

## 验证范围之外

主流程场景已经覆盖上述完整交互链路，但不是所有桌面行为的自动化认证。
审批、通知、截图工具、发送中取消/重试以及系统原生弹窗仍需要对应 GUI 场景
或人工验收；文件取消/重试协议已有独立回归测试。详见测试文档中的剩余缺口。

本轮更新仓库与 CI；桌面 Release 版本仍为 `v1.1.4`，公网服务仍固定
`v1.1.0`。新镜像发布和服务器升级是不同事件，不能写成已经上线。
真实 Qt 基础、项目讲解和 AI 辅助范围说明仍需项目所有者亲自准备。
