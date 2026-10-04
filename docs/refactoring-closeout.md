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
| `src/mainwindow.cpp` | 6,442 行 | 2,539 行 |
| `src/server.cpp` | 2,963 行 | 760 行 |

行数下降来自职责迁移，功能代码仍在仓库中。E2E 报告图保留原有字段和流程，
总逻辑复杂度没有因分文件消失。窗口和服务端仍共享原来的对象状态；新增文件
不是第二套窗口/服务端。CMake 和 qmake 均注册了拆出的源文件。

后续收尾把输入框职责和联系人职责分别移到 `mainwindow_composer.cpp`（141 行）
和 `mainwindow_contacts.cpp`（245 行）。E2E 报告在 6 个单元约 141 处复用精确
相同的 provider 身份与禁止密钥导出字段；计算字段仍显式填写，8 处生产编译
分支改用共同判断。修复 E2E 已有会话需轮换时的明文退回，以及窗口销毁时
Client 回调仍触碰已销毁 UI 的生命周期问题。回归不能只看主文件行数。

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

对话框自动化同时检查“出现”和“关闭”，排队调用 `accept()` 不代表文件
已经被选择器接受。文件测试先切换到临时文件的目录，再在文件名输入框键入
文件名并点击“打开”；对话框
未关闭时 5 秒内拒绝并报告失败。GUI 子进程另设 75 秒限时，外层 CTest 为
90 秒，超时前保留阶段日志，避免只能得到没有卡点信息的超时结果。

## 流程测试发现的实际修复

- 首次登录默认处于公共群，但群资料入口未显示。会话界面刷新现在同步群资料
  与精华消息按钮的可见性，私聊仍隐藏群控件。
- 群设置选择器的颜色模板缺少第 6 个占位符，造成颜色错位和 QString 警告。
  现在补齐模板，测试对该类警告按失败处理。
- `HistoryService` 原先忽略 `QTNETWORKCHAT_APPDATA_DIR`，历史库与其它运行
  数据可能使用不同目录。数据库与旧文本历史现在遵守显式目录；未配置时仍
  使用原来的 Qt 默认目录。此修复不自动迁移旧目录的数据。
- `ClientStorage` / `LocalFileManager` 的组织名构造函数固定使用原生设置，
  没有遵守测试选择的 INI 格式。两者现在显式使用 `QSettings::defaultFormat()`，
  使 GUI 测试的存储/下载配置留在各自临时目录。普通应用默认格式仍为原生格式。
  回归同时启动默认和生产两个 GUI 测试，确认文件没有混入另一个测试目录。

## 自己复验

本机验证环境为 Linux、Qt 6.10.2、GCC 15.2.0、CMake 4.2.3。
默认目录 `build-dev-check` 使用 Release、两项生产选项关闭；生产目录
`build-production-check` 使用 Debug、两项生产选项开启。生产专项还设置了
外部 `QTNETWORKCHAT_TRANSPORT=wss`、Noto 中文字体和 `QT_SCALE_FACTOR=2`，
本地网络 fixture 显式使用自己的 TCP 服务端，不能据此称为公网 WSS 验证。

2026-10-04 的实际结果：

| 检查 | 结果 | 范围 |
|---|---|---|
| 默认完整构建与 CTest | 构建成功；最终全量 58 项：56 实际通过、2 跳过、0 失败（280.88 秒） | 包含旧连接路由修复后完整重跑，不将跳过算作执行通过 |
| 链接 OpenSSL 的生产配置 | 完整构建成功；最终全量 57 项：55 实际通过、2 跳过、0 失败（302.81 秒） | 包含原三个生产配置失败项和引擎；两个真实服务另有 CI 专项 |
| 引擎重复与 CPU 负载 | 默认 12 次；生产诊断版 20 次、40 次；CPU 负载下 15 次均通过 | 负载复跑 94.91 秒；此前曾复现一次发送端断言失败，原始根因未最终确定 |
| 实际 TLS/WSS 登录时序 | 十个成功/拒绝场景通过 | 错误信任收到零登录帧，正确信任恰好一个 |
| 实际 Redis 8.0.5 | 跨实例专项通过（3.30 秒） | 两个 Server、Pub/Sub、普通/对象文件、离线换实例重放 |
| Linux GTK 原生弹窗 | 最新五项选择器及结果断言通过（8.144 秒） | Xvfb/X11，隔离宿主输入法；不是关闭原生弹窗后测试替代控件 |
| v1.1.5 Windows tag CI | 构建、19 项回归（116.01 秒）、原生五流程（9.04 秒）、exe 安装启动通过 | [37212688308](https://github.com/ziyue67/QtNetworkChat/actions/runs/37212688308)，源码 290c494 |
| v1.1.5 Linux tag CI | 构建、19 项回归、引擎五轮、真实 PG16/Redis7、GTK 原生和 deb 安装启动通过 | [37212688307](https://github.com/ziyue67/QtNetworkChat/actions/runs/37212688307)，源码 290c494 |
| 公开 exe 独立验收 | 下载/哈希/NotSigned、无 Qt SDK 安装、启动 8 秒和卸载通过 | [37214462329](https://github.com/ziyue67/QtNetworkChat/actions/runs/37214462329)，不等于 SmartScreen 信任验证 |
| PowerShell 7 脚本维护 | 69 个文件解析与路径/哈希/真实调用方/README 回归通过 | 本地临时证据，不注册计划任务、不对外发布 |
| qmake6 | 配置成功，生成 Makefile，新增源码已注册 | 本轮未完整编译 qmake 目标；完整编译使用 CMake |
| 工作流 YAML | 4 个文件语法解析通过 | 远端运行结果须按提交 SHA 查看 Actions |

真实 PostgreSQL 和 Redis 两项默认返回 77，CTest 标记跳过，不能算作实际执行。
Linux CI 提供独立 PG16/Redis7 服务并启用完整专项且通过；Windows CI 另外运行原生
选择器。最终远端结果、安装包和上线版本见 [计划状态](plan-completion-status.md)。

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

主流程与扩展 GUI 场景覆盖当前菜单接线、好友/群审批、截图和上传取消/重发。
这些是列明场景的自动验收，不是所有系统通知策略、主题/DPI、多显示器或每个
托盘/头像组合的人工认证；群成员菜单的信号检查也不是每项服务器变更。
详细证据见 [续修记录](desktop-storage-closeout.md)。

运行容器的最终版本、备份、回滚和验证边界见
[上线实测](production-deployment-2026-10-04.md)；桌面版本和全部收尾范围见
[计划当前状态](plan-completion-status.md)。
真实 Qt 基础、项目讲解和 AI 辅助范围说明仍需项目所有者亲自准备。
