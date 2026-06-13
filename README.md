# QtNetworkChat

QtNetworkChat 是一个基于 C++ 和 Qt Widgets 开发的 QQ 风格局域网即时通讯软件，支持账号注册登录、好友搜索与添加、群聊、私聊、文件发送、聊天记录、离线消息和系统托盘提醒。项目适合用于 Qt 网络编程、TCP 通信、桌面客户端开发和即时通讯原型学习。

## 功能特性

- QQ 风格登录和注册界面
- 随机 QQ 号注册
- 账号密码登录
- 新账号密码使用带盐 PBKDF2-SHA256 KDF 保存；旧 SHA-256 派生账号会在成功登录后自动升级
- SQLite 本地账号数据库
- 创建服务器和加入服务器
- TCP Socket 局域网通信
- JSON 消息协议
- 私聊端到端加密已覆盖身份签名协商、信任 pin、默认信任门禁、历史状态治理、私聊文件加密、Redis/object routing 证据和 production crypto fail-closed 状态；详细边界见 [E2E hardening status](docs/e2e-hardening-status.md)。
- 可选 Redis 在线状态服务，为高并发和多服务实例部署提供 presence 与在线列表共享基础
- 服务端可向 Redis Pub/Sub 发布本实例聊天事件，为跨实例消息路由打基础
- Redis Pub/Sub 已接入服务端远端聊天事件消费，按实例 ID 去重后转发给本实例在线用户
- 跨实例私聊收件人在线时不会在原服务实例重复写入离线队列
- 小文件/图片可通过 Redis Pub/Sub 路由到远端服务实例的在线收件人；大文件或编码后的 Redis 事件体超过 1 MB 时会回落源实例离线队列，并可在启用对象路由后发布 `large_file_offer` 元数据事件，由远端在线收件人实例校验对象后分片下发，成功 ACK 后通知源实例清理兜底队列和对象，失败时发布 `large_file_failed` 并保留源实例兜底；服务端会输出 `redis_large_file_route` 结构化日志用于统计 offer/claim/delivered/failed/cleanup，并带 `storeType`、`operation` 等安全维度便于 S3/MinIO 运维聚合；远端下发阶段失败会额外输出 `event=offer_delivery operation=deliver` 和固定 reason 桶，区分对象读取、接收端断开、分片拒绝与 ACK 超时
- 群聊广播
- 服务端保存基础群组、群成员和群公告表
- 登录后同步服务端公共群公告、群主和成员角色快照
- 公共群公告更新由服务端校验群主/管理员权限并同步群成员
- 服务端群成员添加/移出接口会校验群主/管理员权限并同步成员快照
- 公共群成员面板支持群主/管理员按 QQ 邀请或右键移出成员，群主可右键设置或取消管理员
- 服务端会限制管理员越权设置管理员、移出其他管理员或修改群主角色
- 服务端会阻止已移出公共群的账号继续发送公共群消息或文件
- 服务端记录公共群移出状态，避免被移出账号重登后自动重新入群；群快照会同步只读移出记录，客户端可继续查看本机历史并提示移出人/时间
- 服务端记录公共群公告、成员变更、拒绝操作 reason 和群文件动作审计事件，并随群快照同步最近审计记录；客户端群成员面板会展示最近审计摘要
- 客户端会提示公共群被移出/重新加入状态，并在被移出时禁用公共群发送入口、标记公共群历史只读
- 服务端支持创建私有群、邀请/移出成员、私有群定向消息、私有群文件路由、非成员不可见与发送 fail-closed；群快照会输出 `groupType`、`membershipState`、`historyPolicy`、`historyVisibility`、`historyReadOnly`、`historyRetainedAfterRemoval`、`filePolicy`、`canSend`、`canSendFiles` 和 `canReadHistory`，移出成员保留只读历史标记并失去发送/文件权限，私有群文件发送/拒绝会写入群审计
- 好友私聊
- QQ 号搜索用户
- 好友申请、同意、拒绝
- 在线好友和离线好友列表
- 发送普通文件、图片、视频等媒体文件
- 文件和图片支持分片传输、进度提示、取消发送、ACK 已接收字节回传、服务端/客户端断点续传状态查询、发送端查询并校验续传状态后按最早缺失分片续传、跳过服务端已确认分片、发送端本地保存并通过菜单入口恢复/清除续传元数据，恢复失败时会保留记录并提示稍后重试或清除；端到端加密私聊文件会优先保存本机 same-wire 密文缓存、信封头、明文/密文 size/hash 和 key 证据，只有缓存、filesystem ObjectStore 密文读回、显式 reviewed S3 对象密文读回，或显式 reviewed offline mirror 密文读回的 hash/size、信封 `ciphertextSha256`、会话 key id/fingerprint、服务端续传状态全部匹配时才自动续传同一加密 wire payload，缓存缺失、对象根目录缺失、对象校验失败、S3/offline 未显式 reviewed 或证据不匹配会 fail-closed 降级为重新发送/清除；过期恢复状态自动清理、ACK 超时后自动查询续传状态、ACK 超时重试、临时拒绝重试和完整性校验
- 取消文件/图片发送时会通知服务端清理未完成分片上传
- 服务端可在同一发送者断开重连后短期保留未完成分片状态并响应续传查询
- 离线私聊消息保存和登录后推送
- 离线文件落盘保存附件数据，用户登录后异步流式读取并按分片下发，成功回放后清理队列和附件
- 离线文件队列保存失败时自动回滚本次附件落盘
- 离线附件默认限制 512 MB，可通过 `QTNETWORKCHAT_OFFLINE_ATTACHMENT_QUOTA_MB` 调整；离线附件默认保留 14 天，可通过 `QTNETWORKCHAT_OFFLINE_ATTACHMENT_TTL_DAYS` 调整；离线附件续传进度默认保留 24 小时，可通过 `QTNETWORKCHAT_OFFLINE_RESUME_TTL_HOURS` 调整
- 聊天记录本地持久化
- 清空当前聊天记录
- Enter 发送，Ctrl+Enter 换行
- 未读消息提醒
- 系统托盘提醒
- 自动重连和心跳保活
- CTest 覆盖消息序列化、账号密码 KDF、文件分片/续传/离线附件、公共群/私有群权限边界、Redis/object routing、E2E、PostgreSQL plan 和自动化状态；完整矩阵见 [Testing coverage](docs/testing-coverage.md)。
- CTest 覆盖客户端登录凭据本地存储安全：记住登录只保存账号、昵称和记住标志，不保存明文密码；旧 SQLite/QSettings 明文密码会在加载或迁移时清理
- CTest 覆盖 TLS 证书 SHA-256 指纹固定配置、端到端加密最小协议 envelope/key agreement 序列化、文本加解密、篡改认证失败、缺少会话密钥 fail-closed、身份公告/指纹固定/信任 mismatch、会话状态/消息计数/轮换门禁、轮换请求/响应控制面、服务端透明转发和接收端解密链路、文件传输失败 reason 到用户可读状态/诊断文本的映射，以及 S3 稳定化证据 JSON/Markdown/alert 输出
- CTest 覆盖 Redis RESP 命令编码、响应解析、presence 命令流、Pub/Sub 发布/订阅基础、断线重订阅、跨实例群聊/私聊/小文件/小图片路由、跨实例 E2E 身份公告与签名轮换控制面、订阅侧超大文件/图片事件拒收、编码后超限的 Redis file/image 事件拒收、缺少 senderId、非聊天 eventType、空消息或未知类型的 Redis 事件拒收、大文件和编码后超限文件不经 Pub/Sub 并离线兜底、源实例写入 ObjectStore 后发布带 `storeType` 的 `large_file_offer` 元数据、远端实例仅在本地 ObjectStore 类型与 offer 匹配时认领 offer 并校验对象后分片下发、E2E 大文件对象路由只发布 envelope 头部和明文 size/hash 证据且接收端本地解密、完整 ACK 后发布 `large_file_delivered` 并清理源实例兜底状态、源实例只读输出 `delivered_reconcile` cleaned/retained 日志、远端对象下发阶段以 `offer_delivery operation=deliver` 输出固定 reason 失败日志、对象缺失等失败路径发布 `large_file_failed` 且源实例保留离线兜底、非法 objectKey/分片元数据不下发、offer storeType 不支持或与本地配置不匹配时固定 reason 拒绝且不 claim、非 filesystem store 不消费、过期未 delivered 对象可由 ObjectStore TTL 清理且离线兜底仍可回放、私聊文本和小文件发布失败离线兜底、Redis 不可用降级登录
- CTest 覆盖 Windows 打包 manifest：验证版本号命名目录/ZIP、manifest 格式、运行时依赖检查字段，以及 exe/README/manifest 均进入压缩包
- CTest 覆盖 filesystem ObjectStore 的安全 objectKey 生成、路径穿越拒绝、hash/size 校验、TTL 清理、S3 配置/URL/Signature V4 纯函数、不联网 Qt Network 请求构造、S3 对象方法白名单、HTTP 状态分类、请求超时配置、transfer timeout 写入、可选 session token 签名头、显式启用开关、请求结果归一化、失败 reason 聚合、错误脱敏、注入式 PUT/GET/HEAD/DELETE 执行边界、GET 响应体 size/hash 校验、GET/open 与 DELETE/remove 失败 reason 接线、Qt Network 执行器非法请求 fail-closed，以及跨实例大文件 offer 指向对象的 size/hash 校验和 delivered 回执清理判定纯函数
- 复制当前 QQ 账号
- 退出登录并回到登录流程

## 文档索引

README 保留快速上手、项目结构、构建入口和运行入口；大段产品化状态、运维证据和测试矩阵拆到聚焦文档，避免自动化闭环继续在 README 里堆长段。

当前主线已经进入 release closeout：以 current-HEAD 的本地验证、release review、archive decision、delivery handoff、closeout summary 和 final local archive 为准，不再把新的产品功能扩展作为当前分支主任务。
文件 / 离线附件、MainWindow / UI 拆分、群组产品化、PostgreSQL、治理 dashboard 等主功能线已经完成当前分支范围内的产品化闭环；后续只在确有回归、缺陷或收官维护收益时再做小范围修正。
另一个 Redis 或环境外发布分支的事项，不计入当前 `main` 分支是否已经本地收官的判断；当前分支只对仓库内可验证、本地可归档的 closeout 事实负责。

- [Testing coverage](docs/testing-coverage.md)：CTest 覆盖面、协议/文件/Redis/自动化验证边界。
- [PostgreSQL operations](docs/postgresql-operations.md)：QPSQL 配置、健康检查、release acceptance、真实 smoke 和连接池治理。
- [Large-file governance](docs/large-file-governance.md)：对象路由、S3/MinIO 证据、delivered receipt 对账、治理 dashboard/status。
- [E2E hardening status](docs/e2e-hardening-status.md)：E2E 已完成边界、生产 crypto 剩余风险和后续旋转迁移门禁。
- [Release closeout](docs/release-closeout.md)：本地 release review、archive decision、delivery handoff、delivery drill、publication record 和 closeout summary 的总览。
- [Automation status](docs/automation-status.md)：当前自动化基线、受保护未跟踪项、验证摘要和下一轮优先级。
## 项目结构

```text
QtNetworkChat/
├── include/
│   ├── chatuser.h       # 用户数据结构
│   ├── clientstorage.h  # 本地 profile/friends/groups 存储服务
│   ├── client.h         # TCP 客户端接口
│   ├── friendmanager.h  # 联系人显示名、在线状态和通知 badge 逻辑
│   ├── groupmanager.h   # 群权限、群通知和成员状态纯逻辑
│   ├── historyservice.h # 本地聊天历史存储与查询服务
│   ├── mainwindow.h     # 主窗口接口
│   ├── transfermanager.h # 文件传输恢复菜单和状态事件编排
│   ├── redisclient.h    # 可选 Redis 在线状态服务客户端
│   ├── message.h        # 消息结构与序列化
│   └── server.h         # TCP 服务端接口
├── src/
│   ├── client.cpp       # 客户端连接、收发消息、登录注册协议
│   ├── clientstorage.cpp # SQLite/legacy file 本地状态持久化
│   ├── friendmanager.cpp # 好友/联系人 UI 状态纯逻辑
│   ├── groupmanager.cpp # 群组权限与成员显示状态逻辑
│   ├── historyservice.cpp # SQLite/legacy file 历史记录持久化
│   ├── main.cpp         # 程序入口、登录/注册窗口、启动流程
│   ├── mainwindow.cpp   # 主界面、聊天、好友、文件和历史 UI 编排
│   ├── transfermanager.cpp # 文件恢复动作状态和传输诊断事件
│   ├── redisclient.cpp  # Redis RESP 命令、响应解析和 presence 写入
│   ├── message.cpp      # JSON 消息序列化与反序列化
│   └── server.cpp       # 服务端连接管理、账号、好友、消息转发
├── ui/
│   └── mainwindow.ui    # Qt Designer 主界面文件
├── CMakeLists.txt       # CMake 构建配置
├── QtNetworkChat.pro    # qmake 构建配置
└── README.md            # 项目说明
```

## 技术栈

- C++17
- Qt 5.15+ 或 Qt 6.x
- Qt Widgets
- Qt Network
- Qt SQL
- Redis（可选，用于服务端在线状态/presence）
- TCP Socket
- JSON
- SQLite

## 环境要求

- Windows、macOS 或 Linux
- Qt 5.15+ 或 Qt 6.x
- 支持 C++17 的编译器
- 推荐 Windows 使用 Qt Creator + MinGW Kit

## 构建方法

### 使用 Qt Creator

1. 打开 `QtNetworkChat.pro`。
2. 选择可用的 Qt Kit，推荐 MinGW。
3. 执行 qmake。
4. 构建并运行项目。

### 使用 qmake

```bash
qmake QtNetworkChat.pro
make
```

Windows 下可根据 Qt Kit 使用 `mingw32-make`、`nmake` 或 `jom`。
`QtNetworkChat.pro` 现在会显式包含仓库内的 `include/qtnetworkchat_e2e_crypto_config.h`。这个头文件不是生成垃圾，而是给 qmake/legacy 构建路径保留的 fail-closed E2E fallback 配置；CMake 路径仍会通过 `include/qtnetworkchat_e2e_crypto_config.h.in` 在构建目录生成当前配置头。

### 使用 CMake

```bash
cmake -S . -B build
cmake --build build
```

如果 CMake 找不到 Qt，需要设置 `CMAKE_PREFIX_PATH`，或配置 `Qt6_DIR` / `Qt5_DIR`。
默认 CMake 会根据 `include/qtnetworkchat_e2e_crypto_config.h.in` 生成构建目录内的 `generated/qtnetworkchat_e2e_crypto_config.h`，以反映当前 production adapter / provider table / fail-closed 状态；源码目录内同名头只服务 qmake fallback，不参与 CMake 生成结果覆盖。

### 自动化验证

```bash
ctest --test-dir build --output-on-failure
```

当前 CTest 会执行构建产物冒烟测试，并覆盖消息、E2E、账号、TLS、文件传输、离线附件、Redis/object routing、PostgreSQL 计划、自动化状态和 Windows packaging 等 release gate。完整测试矩阵见 [Testing coverage](docs/testing-coverage.md)。

CTest wrapper 会把 `QTNETWORKCHAT_APPDATA_DIR` 指向构建目录下的隔离运行时数据目录，避免 Windows 用户 Roaming 目录不可写或残留真实账号库时影响协议测试。手动压测或临时双实例验证也可设置该变量来隔离 `accounts.sqlite3`、离线队列、离线附件和发送端恢复状态；生产运行通常不需要设置，且不要把它指向源码目录或包含真实运行数据的共享目录。

### Windows 打包

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1 -BuildDir build-qt6-mingw
```

脚本会先构建项目，再把 `QtNetworkChat.exe`、`README.md` 和 `manifest.json` 收集到 `build-qt6-mingw/release-package/QtNetworkChat-<版本号>-win-x64`。如果系统能找到 `windeployqt.exe`，会自动复制 Qt 运行库，并把 Qt Core/GUI/Network/SQL/Widgets DLL 纳入运行时依赖检查，最后生成 `build-qt6-mingw/release-package/QtNetworkChat-<版本号>-win-x64.zip`。`manifest.json` 会记录版本号、提交哈希、配置、可执行文件大小、windeployqt 路径和依赖检查结果；需要在 CI 或脚本测试中跳过构建/部署时，可使用 `-SkipBuild -NoDeploy`，需要把缺失运行库作为失败处理时使用 `-FailOnMissingRuntime`。

如果发布包需要直接支持 PostgreSQL，把 Qt 的 QPSQL SQL driver 和 PostgreSQL `libpq` 运行库一起收集进包：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1 `
  -BuildDir build-qt6-mingw `
  -QtRoot "D:\Qt\6.8.3\mingw_64" `
  -PostgresBinDir "D:\Program Files\PostgreSQL\17\bin" `
  -IncludePostgresSql `
  -FailOnMissingPostgresSql
```

该模式会把 `qsqlpsql.dll` 放入 `sqldrivers/`，并复制 `libpq.dll`、OpenSSL、iconv、intl 和 zlib 依赖到发布包根目录；`manifest.json` 的 `postgresSqlRuntime` 会记录插件来源、已复制文件和缺失项。SQLite 与 PostgreSQL 仍可并存：不设置 `QTNETWORKCHAT_DB_DRIVER` 时继续走 SQLite，设置为 `QPSQL` 时才连接 PostgreSQL。

### 本地发布交付包

完成当前 HEAD 的 Windows 打包和本地 release review 之后，可以把发布交付、安装入口、诊断采集和运维交接一起收成一个本地交付包：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-release-delivery-handoff.ps1 `
  -OutputDir build-qt6-mingw\release-delivery-handoff
```

该包会归档当前 HEAD 的 Windows package manifest/zip、本地 `release review` 收官包、发布上传说明、安装脚本和脱敏诊断采集脚本，并生成：

- `build-qt6-mingw/release-delivery-handoff/release-delivery-handoff.zip`
- `build-qt6-mingw/release-delivery-handoff/release-delivery-handoff-manifest.json`
- `build-qt6-mingw/release-delivery-handoff/release-delivery-handoff.md`

其中：

- `scripts/install-qtnetworkchat-package.ps1` 可把发布 zip 解压到目标目录并写入 `install-record.json`
- `scripts/collect-qtnetworkchat-diagnostics.ps1` 会收集本地验证日志、自动化状态、release review / release delivery manifest 和崩溃 dump 清单，只记录相对路径、大小、时间戳，不打包原始 dump 字节
- `release-upload-plan.json/.md` 只提供本地上传/分发说明，不会直接连接 GitHub Releases、对象存储或其他远端渠道

### 本地 release archive decision

当 current-head 的 `local-release-review` 和 `release-delivery-handoff` 都准备好以后，可以把最终人工归档决定单独记录为一个一等产物：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-release-archive-decision.ps1 `
  -OutputDir build-qt6-mingw\release-archive-decision
```

该包会把 `local-release-review`、`release-delivery-handoff` 与最终 archive decision 的状态拆开记录，并生成：

- `build-qt6-mingw/release-archive-decision/release-archive-decision.zip`
- `build-qt6-mingw/release-archive-decision/release-archive-decision-manifest.json`
- `build-qt6-mingw/release-archive-decision/release-archive-decision.md`

默认会写入 `pending-human-decision`，表示代码侧 closeout 已准备好，但最终是否归档、是否转入环境外发布，仍要由你显式确认。需要记录最终决定时，可追加例如：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-release-archive-decision.ps1 `
  -OutputDir build-qt6-mingw\release-archive-decision `
  -DecisionState approved-local-archive `
  -DecidedBy jun23 `
  -DecisionReason "Current-head release evidence reviewed locally." `
  -PublishingStatus published-outside-repo
```

这样 `automation-status`、诊断包、release review 和 release delivery handoff 都能回读同一份 archive decision，而不会把“代码验证已通过”和“外部渠道是否已发布”混在一起。

如果你还想顺手把本地安装/诊断交付 drill 一并跑掉，并把发布状态写回 record，可直接在收官刷新时一起带上：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/refresh-release-closeout.ps1 `
  -BuildDir build-qt6-mingw `
  -RunDeliveryDrill `
  -ArchiveDecisionState approved-local-archive `
  -ArchiveDecidedBy jun23 `
  -ArchiveDecisionReason "Current-head release closeout verified locally." `
  -ArchivePublishingStatus published-outside-repo `
  -ArchivePublishingChannel team-share
```

这样会额外生成：

- `build-qt6-mingw/release-delivery-drill/release-delivery-drill-manifest.json`
- `build-qt6-mingw/release-publication-record.json`

前者证明安装脚本和脱敏诊断收集脚本在本地交付 drill 里跑通过，后者只记录“环境外发布状态”的脱敏摘要，不会真的连接外部发布平台。

### 本地 release closeout 总览

如果你想把当前 HEAD 的收官状态收成一份单独总览，方便值班、交付或停笔前归档，可以使用：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-release-closeout-summary.ps1 `
  -OutputDir build-qt6-mingw\release-closeout-summary
```

该总览会把 `local-release-review`、`release-archive-decision`、`release-delivery-handoff`、`release-delivery-drill`、`release-publication-record` 和脱敏 diagnostics package 汇总成一份统一 closeout 视图，并生成：

- `build-qt6-mingw/release-closeout-summary/release-closeout-summary.zip`
- `build-qt6-mingw/release-closeout-summary/release-closeout-summary-manifest.json`
- `build-qt6-mingw/release-closeout-summary/release-closeout-summary.md`

其中 `closeoutGate=release-closeout-ready-for-stop-writing` 表示本地 release review、archive decision、delivery handoff、delivery drill、publication record 和 diagnostics package 都已经在同一基线上齐备；环境外发布是否真的完成，仍然只通过 `release-publication-record` 的脱敏状态显式记录，而不会被本地构建/测试结果隐式代替。

### 本地最终归档

当你希望把当前 HEAD 的“可停笔”状态收成一份最终本地归档，方便值班交接、归档保存或后续只回写发布状态时，可以额外运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-release-final-local-archive.ps1 `
  -OutputDir build-qt6-mingw\release-final-local-archive
```

该归档会把 `README`、`automation-status`、`docs/release-closeout.md`、`docs/e2e-hardening-status.md`，以及 `local-release-review`、`release-archive-decision`、`release-delivery-handoff`、`release-delivery-drill`、`release-publication-record`、脱敏 diagnostics package 和 `release-closeout-summary` 一起打包成：

- `build-qt6-mingw/release-final-local-archive/release-final-local-archive.zip`
- `build-qt6-mingw/release-final-local-archive/release-final-local-archive-manifest.json`
- `build-qt6-mingw/release-final-local-archive/release-final-local-archive.md`

其中 `archiveGate=ready-final-local-archive` 表示当前 HEAD 的本地 release review、最终 archive decision、delivery handoff、delivery drill、publication record、diagnostics package 和 closeout summary 已经在同一条本地基线上齐备；后续如果只是环境外发布状态变化，只需要刷新 publication record 并重新生成这份最终归档，而不需要重新解释代码验证结论。

如果需要把当前 HEAD 的本地验证、Windows 包、linked E2E candidate、release review、release delivery handoff 和 `docs/automation-status.md` 一次性刷新到同一基线，可运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/refresh-release-closeout.ps1 `
  -BuildDir build-qt6-mingw
```

这个收官脚本会按固定顺序重生成当前 HEAD 的 closeout 产物，包含 `local-release-review`、`release-archive-decision`、`release-delivery-handoff`、`release-delivery-drill`、`release-publication-record`、`release-closeout-summary` 与 `release-final-local-archive`，避免 `automation-status` 和各归档包因交叉引用旧 manifest 而停留在旧基线。`docs/automation-status.md` 也只保留稳定的 gate/ready/tail/decision 摘要，不再回写这些归档包自身的 SHA-256，以免状态文档与被其打包的归档产物形成自引用漂移。

当默认 fail-closed 的 E2E release evidence baseline 仍保留旧 HEAD、而 current-head 的 `local-release-review` 与 `release-closeout-summary` 已经完成时，`automation-status` 会把 current-head closeout 视为活跃收官基线。旧 baseline 仍会保留在状态板里作为信息性诊断项，但不会再覆盖当前 HEAD 的本地 release review 结论。

### 可选 MinIO S3 手动验证

S3/MinIO 后端默认仍保持关闭；需要验证当前 SigV4 签名和 path-style 请求边界时，可手动运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/minio-s3-smoke.ps1
```

脚本会尝试通过 Docker 启动本地 MinIO，并用 SigV4 完成 bucket 创建和对象 PUT/HEAD/GET/DELETE smoke。若已自行启动 MinIO，可追加 `-SkipContainer` 并传入 `-Endpoint`、`-Bucket`、`-AccessKey`、`-SecretKey`、`-Region`、`-Prefix`。若本机已经下载 MinIO，也可以直接传 `-MinioServerPath`（兼容旧参数 `-MinioExePath`）和 `-DataDir`；Windows 下会隐藏启动本地 MinIO，Linux/macOS 下会按普通后台进程启动，完成 smoke 后自动停止。Docker 模式会按 `-Endpoint` 和 `-ConsolePort` 映射端口，便于 Linux 服务器使用非 9000 端口。该脚本不属于默认 CTest 或 CI 前置条件。

Linux MinIO 示例：

```bash
pwsh ./scripts/minio-s3-smoke.ps1 \
  -MinioServerPath /usr/local/bin/minio \
  -DataDir /var/tmp/qtnetworkchat-minio \
  -Endpoint http://127.0.0.1:19000 \
  -ConsolePort 19001
```

需要把真实 smoke 纳入脱敏证据包时，可额外指定 `-SanitizedRouteLogPath`、`-SanitizedSummaryPath` 和 `-SanitizedSmokeLogPath`。这些产物只记录 `storeType=s3`、固定 `operation/reason/result`、字节数和 smoke 结果，不写 endpoint、bucket、object URL、access key、secret key、Authorization、Credential 或 Signature，可继续交给 `scripts/analyze-s3-request-results.ps1`、`scripts/verify-s3-real-backend-evidence.ps1` 和 `scripts/package-large-file-acceptance.ps1` 生成真实后端 evidence JSON/Markdown/alert 和验收 zip。

若要让服务端显式启用真实 S3/MinIO 后端，先用上述 smoke 脚本确认 endpoint、bucket、凭据和 prefix 可用，再在启动服务端前设置：

```powershell
$env:QTNETWORKCHAT_OBJECT_STORE = "s3"
$env:QTNETWORKCHAT_OBJECT_S3_ENABLE = "1"
$env:QTNETWORKCHAT_OBJECT_S3_ENDPOINT = "http://127.0.0.1:9000"
$env:QTNETWORKCHAT_OBJECT_S3_BUCKET = "qtchat-large-files"
$env:QTNETWORKCHAT_OBJECT_S3_REGION = "us-east-1"
$env:QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY = "qtchat-dev"
$env:QTNETWORKCHAT_OBJECT_S3_SECRET_KEY = "qtchat-dev-secret"
$env:QTNETWORKCHAT_OBJECT_S3_PREFIX = "qtchat/manual-smoke"
$env:QTNETWORKCHAT_OBJECT_S3_TIMEOUT_MS = "30000"
```

Linux 环境变量示例：

```bash
export QTNETWORKCHAT_OBJECT_STORE=s3
export QTNETWORKCHAT_OBJECT_S3_ENABLE=1
export QTNETWORKCHAT_OBJECT_S3_ENDPOINT='http://127.0.0.1:9000'
export QTNETWORKCHAT_OBJECT_S3_BUCKET='qtchat-large-files'
export QTNETWORKCHAT_OBJECT_S3_REGION='us-east-1'
export QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY='qtchat-dev'
export QTNETWORKCHAT_OBJECT_S3_SECRET_KEY='qtchat-dev-secret'
export QTNETWORKCHAT_OBJECT_S3_PREFIX='qtchat/manual-smoke'
export QTNETWORKCHAT_OBJECT_S3_TIMEOUT_MS=30000
```

`QTNETWORKCHAT_OBJECT_S3_ENABLE` 未显式设为 `1/true/yes/on` 时，`s3` 工厂会继续 fail-closed。真实后端失败时仍保留源实例离线附件兜底；日志、Redis 事件和离线队列只允许记录固定 reason 桶和逻辑 `objectKey`，不得写入 endpoint、bucket、object URL、access key、secret key、session token、Authorization、Credential 或 Signature。

完整的真实后端人工验收步骤见 `docs/s3-minio-manual-acceptance.md`：先跑 smoke，再启动 Redis 与两个服务端实例，验证 `large_file_offer/claim/delivered/failed`、`redis_large_file_route` 安全字段、失败回退、回源离线兜底回放，以及可选 delivered receipt 只读对账演练。该清单仍不纳入默认 CTest 或 CI。

大文件 route log 聚合、S3 reason 分析、delivered receipt 对账、轮转、治理 dashboard/status、acceptance package 和任务 preview 已拆到 [Large-file governance](docs/large-file-governance.md)。README 只保留手动 smoke 入口和安全原则：治理脚本必须拒绝 endpoint、bucket、object URL、access key、secret key、session token、Authorization、Credential 或 Signature 等敏感字段。

`scripts/write-s3-stabilization-evidence.ps1` 可把一个或多个脱敏 S3 request summary 与 route summary 汇总为稳定化证据 JSON、Markdown 和 alert summary，检查 timeout/network/tls/auth/retryable/server/hash/size reason 桶以及 PUT/HEAD/GET/DELETE/validate/read/remove 操作覆盖，并拒绝 endpoint、bucket、object URL、access key、secret key、session token、Authorization/Credential/Signature 等敏感输入。默认 CTest 使用生成样例验证该脚本，不连接真实 S3/MinIO。

失败回退演练可用 `scripts/s3-failure-drill.ps1 -Scenario all` 先列出 network、auth、missing-object、receiver-disconnect 场景的注入方式、预期固定 reason 和兜底检查点；该脚本默认只输出步骤，不连接 Redis、S3/MinIO 或修改队列。

### GitHub Actions

仓库保留 `.github/workflows/windows-build.yml` 作为可选补充覆盖：推送到 `main` 或提交 PR 时可在 GitHub Actions 上安装 Qt 6.8.3 MSVC 2022 x64、构建项目并运行 CTest，失败或取消时会上传 `windows-build-diagnostics` artifact，手动触发时还会运行 Windows 打包脚本并上传 `QtNetworkChat-win-x64.zip`。当前仓库自动化策略已明确将 GitHub Windows Build 视为 `disabled / not-required`，不参与当前 release gate，也不影响当前 head 的闭环判断；即使 run list 未观察到当前 HEAD，也只记录为 skipped/not-required。提交前有效验证只看本地 Qt MinGW `build-qt6-mingw`、PowerShell timeout wrapper 构建和本地全量 CTest。

自动化闭环状态可用 `scripts/write-automation-status.ps1` 生成或更新 `docs/automation-status.md`。默认不传任务路径时，脚本会在 `build-qt6-mingw/automation-tasks/` 下 bootstrap database-health、large-file-governance 和 PostgreSQL release acceptance 的 preview、status、last-run、history、ack 与脱敏 evidence 占位产物，并稳定回读到状态板；后续真实计划任务运行会覆盖同一 artifact。脚本也会回读 `docs/automation-policy.json`，可把 GitHub Windows Build 标记为 `disabled`、`optional` 或 `required`；当前策略为 `disabled` 时，状态板会将 Windows Build 记为 `disabled-by-policy` / `not-required`，并明确移出当前 release gate，release evidence promotion 只看本地 MinGW build/CTest、production-linked evidence 和对应验证基线的本地 release review。由于 `docs/automation-status.md` 本身也是收官归档输入，状态板里的 `HEAD` 表示最近一次已验证的证据基线，提交该文件后仓库提交可能再前进一步，这是预期行为。更多任务 preview、ack/history 与新鲜度约定见 [Large-file governance](docs/large-file-governance.md) 和 [PostgreSQL operations](docs/postgresql-operations.md)。

## 运行方式

1. 启动程序。
2. 在一台电脑上选择创建服务器，端口默认可使用 `8888`。
3. 其他电脑选择加入服务器，填写服务器电脑的局域网 IP 和端口。
4. 注册新 QQ 账号或登录已有账号。
5. 通过 QQ 号搜索用户并发送好友申请。
6. 好友通过后即可私聊、群聊、发送文件和查看历史记录。

## 本地数据

程序运行后可能产生以下本地数据文件：

- `accounts.sqlite3`：账号数据库
- `login_accounts.sqlite3`：客户端本地登录账号记忆库，只保存账号、昵称和记住标志，不保存明文密码
- `friends_<账号>.txt`：好友列表
- 聊天历史文件：群聊和私聊记录
- 离线消息文件：未在线用户的私聊消息与文件元数据
- 离线附件目录：未在线用户的文件和图片二进制数据，服务端会定期清理过期或无队列引用的附件

这些文件属于运行时数据，不建议提交到 GitHub。

账号库中的 `password_hash` 新写入格式为 `kdf$pbkdf2-sha256$<iterations>$<salt>$<hash>`，使用随机盐和 120000 次 PBKDF2-SHA256。旧版本的 `SHA-256(account:password)` 哈希仍可登录；只有密码校验成功后服务端才会把该账号升级到 KDF 格式，错误密码不会触发升级。旧 `accounts.json` 迁移入库时保留兼容哈希，后续首次成功登录再升级，便于灰度迁移。

客户端“记住密码”已收敛为“记住账号信息”：本地 `login_accounts.sqlite3` 和旧 `QSettings` 只保留账号、昵称和记住标志，密码输入框不会从本地自动回填。旧版本若已经写入 SQLite 或 QSettings 明文密码，启动加载或迁移时会清空该字段。

### 离线附件环境变量

| 环境变量 | 默认值 | 有效值与非法值回退 | 影响范围 | 测试覆盖 |
| --- | --- | --- | --- | --- |
| `QTNETWORKCHAT_OFFLINE_ATTACHMENT_QUOTA_MB` | `512` MB | 正整数 MB；空值、非数字或小于等于 0 时回退默认值 | 限制离线附件落盘总量，超限时不写入附件和离线队列 | CTest 覆盖自定义配额下的拒绝、无附件残留和无队列残留 |
| `QTNETWORKCHAT_OFFLINE_ATTACHMENT_TTL_DAYS` | `14` 天 | `1` 到 `3650` 的整数天数；空值、非数字、越界或小于等于 0 时回退默认值 | 控制启动和定时清理离线附件时的过期判断；仍被队列引用但已过期的附件也会清理 | CTest 覆盖自定义 TTL 下启动清理过期附件、保留未过期队列引用附件，以及后续缺失提示 |
| `QTNETWORKCHAT_OFFLINE_RESUME_TTL_HOURS` | `24` 小时 | `1` 到 `8760` 的整数小时数；空值、非数字、越界或小于等于 0 时回退默认值 | 控制 `resumeUpdatedAt` 的可信时间窗口；过期后不再信任 `confirmedBytes`/`confirmedChunks`，回退完整回放 | CTest 覆盖自定义 TTL 下旧续传进度回退完整回放 |

## 离线附件失败续传策略

当前离线附件登录回放会从附件文件头开始按分片发送，并等待接收端 ACK。若接收端中途断开、ACK 超时或拒绝分片，服务端会保留离线队列行和附件文件；只有所有需要发送的分片成功 ACK，或 confirmedChunks 已覆盖全部分片，才清理队列和附件。失败续传以“不误删附件、不信任陈旧进度、优先校验本地元数据”为安全基线：

- 队列元数据已开始写回可选的 `confirmedBytes`、`confirmedChunks` 和 `resumeUpdatedAt` 字段；默认 24 小时内的连续 confirmedBytes 与附件大小、chunkSize、chunkCount 匹配时，重登回放会从下一未确认分片续发，即使 confirmedChunks 为空也可使用；可用 `QTNETWORKCHAT_OFFLINE_RESUME_TTL_HOURS` 调整续传进度有效小时数；有效期内的 confirmedChunks 合法时会优先于 confirmedBytes，去重并从最早缺失分片续发、跳过后续已确认分片，若已覆盖全部分片则直接清理队列和附件；过期恢复进度会回退完整回放。
- 重登回放前校验附件路径、大小、标准 SHA-256 hash、chunkSize、chunkCount 与队列元数据一致；校验失败时沿用当前缺失/大小/hash 异常提示并清理坏状态。
- 继续下发时优先从最早未确认分片开始，跳过已确认分片；若接收端返回拒绝 ACK 或连接中断，则保留队列和附件等待下次登录重试，避免误删附件。
- 清理策略仍以“完整 ACK 后删除队列和附件”为唯一成功条件；默认 14 天内的队列引用附件会被保留，`QTNETWORKCHAT_OFFLINE_ATTACHMENT_TTL_DAYS` 可调整附件过期天数；过期或无队列引用的附件会被启动/定时清理删除。
- 这一链路在当前分支已完成协议与产品化闭环；后续若继续调整，只属于收官后的维护性优化，例如失败提示体验、跨实例大文件回放设计、治理指标和传输性能校验。

### 离线附件异常清理策略

离线附件回放前会先做本地一致性检查，并向接收端发送可区分原因的系统提示：附件路径缺失时提示“离线文件已丢失”，声明大小不一致时提示“离线文件大小异常”，标准 SHA-256 不匹配时提示“离线文件校验失败”，chunkSize/chunkCount 与附件大小不一致时提示“离线文件分片元数据异常”。这些状态都视为坏队列，服务端会清理对应离线队列行；除路径已不存在的场景外，也会删除坏附件文件。

下发过程中断、ACK 超时或接收端拒绝 ACK 不属于坏元数据，服务端会保留队列和附件，等待下次登录重试。只有所有需要发送的分片都被确认，或 confirmedChunks 已覆盖全部分片，才按成功回放清理队列和附件。

## 常见问题

### 如何启用 TLS 加密通道

默认使用普通 TCP，便于局域网快速测试。需要测试 TLS 时，启动服务端和客户端前设置：

```bash
set QTNETWORKCHAT_TLS=1
set QTNETWORKCHAT_TLS_CERT=C:\path\to\server.crt
set QTNETWORKCHAT_TLS_KEY=C:\path\to\server.key
```

客户端默认允许自签名证书，适合本地测试；如果需要校验证书链，可额外设置 `QTNETWORKCHAT_TLS_VERIFY=1`。

如果无法给本地测试证书配置完整信任链，也可固定服务端证书 SHA-256 指纹：

```bash
set QTNETWORKCHAT_TLS_PINNED_SHA256=ab12cd34...
```

设置指纹后，客户端会在 TLS 握手完成后校验服务端证书 DER 的 SHA-256；不匹配会立即断开并提示“TLS 证书指纹不匹配”。指纹可带 `SHA256=` 前缀、冒号或短横分隔符。该配置不会放宽 S3/MinIO TLS 校验。

### 端到端加密协议边界

当前 E2E 已完成身份签名协商、信任 pin、默认信任门禁、历史状态治理、私聊文件加密、Redis/object routing 证据、加密文件 same-wire 缓存续传/fail-closed 恢复策略、filesystem 对象密文读回续传、对象/离线恢复候选证据、离线密文附件回放解密校验，以及 production crypto backend fail-closed/linked OpenSSL dispatch 验收链。
生产 adapter 现在还公开逐操作 harness、execution plan、invocation contract、slot registry、dispatch binding、callable manifest、sanitized execution result contract、provider C ABI header、provider table ABI/build-probe 证据、provider table binding probe、运行时 provider table registration gate、逐操作 provider preflight gate、provider call frame gate、provider invocation dry-run/result capture、provider execution decision gate、provider callback harness gate、provider vector self-test gate、provider execution slot/path/sandbox/vector-result/invocation execution gate、reviewed execution candidate/handoff/stub/callable bridge/interface/runtime preflight/arming/execution acceptance 证据、production provider data-plane bridge 证据、public primitive execution 证据、显式 provider invocation execution probe、provider round-trip execution probe、public primitive execution probe、probe vector contract schema、probe execution frame schema、probe known-answer output evidence、输出形状 mismatch fail-closed 分类、per-operation known-answer vector/fixture mismatch 矩阵、status/material/pointer/table-blocked mismatch scope 分类、非执行结构校验器和 rotation dry-run/execute evidence。
当 `QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER=ON` 且构建环境发现 OpenSSL 时，运行时会绑定 `openssl-reviewed-provider-v1` 的 8 项 reviewed operation：session-key generation 通过 OpenSSL RNG 生成 32 字节 handle-only sealed output，identity-key generation 生成 Ed25519 seed 并派生 32 字节 public output，public-key derivation 只接受 32 字节 private identity handle 并派生 public output，agreement sign 只接受 32 字节 private identity handle，agreement verify 只接受 32 字节 public identity material，session derive 使用 HKDF-SHA256 从规范 transcript/session context material 派生 32 字节 handle-only sealed output，payload encrypt/decrypt 只接受 32 字节 session key handle 并使用 AES-256-GCM 生成和验证带 nonce/tag 的 payload。运行时自检会串联身份生成、公钥派生、签名、验签、会话派生、AES-GCM 加密、解密、签名/密文篡改拒绝，以及 malformed identity/public/session material 的 invalid-input 拒绝；通过后显式 production/openssl backend 请求会选择 `openssl-reviewed-adapter-v1`，公开 E2E API 会调度 provider 执行密钥生成、公钥派生、签名、验签、会话派生、payload 加密和解密。
linked reviewed provider 下 `productionReady=true` 表示 provider table、8 项回调、runtime self-test 和 public API dispatch 已经可用；默认 backend status 现在会把真实 provider invocation probe 贯通到 reviewed candidate、call handoff、operation stub、callable bridge、runtime preflight、invocation arming、execution acceptance、data-plane bridge 和 public primitive execution evidence，正常 probe fixture 使用 32 字节合法 handle，malformed handle/public key/session key 只在负向检查中出现。
linked OpenSSL 子构建通过这些真实结果后可报告 `productionAcceptance.accepted=true` / `releaseGate=production-crypto-accepted`，并额外验证 `executeE2EProductionRotation` 可在所有 gate 通过时生成 production local identity、持久化 backend id、隔离旧 draft sessions/pending agreements，并输出不含私钥、session key、完整 public key 或完整 fingerprint 的 local rebind 证据。
runtime gate 现在还覆盖 Alice/Bob/Carol 多端 production 身份重新公告、trust pin 重新验证、独立签名 session rotation、加密私聊文本和加密私有文件 payload；同一账号重启后也会恢复多端 production identity/trust pin，拒绝复用内存会话，再重新协商独立 production session 并完成加密文本/私有文件发送，确认会话落到 `openssl-reviewed-adapter-v1` 且不导出原始 session key/private material。
`productionRolloutObservability` 会把 acceptance release gate、material/export proof count、public primitive readiness、filesystem object ciphertext readback gate、operator recovery prompts 和 user recovery prompts 聚合成脱敏发布观测面；未链接或未验收时 fail-closed，linked accepted 时才给 `releaseGate=production-rollout-observability-ready`。
`e2e_rollout_observability_exporter` 可把该观测面持久化为脱敏 JSON/Markdown；默认 CTest 校验未链接 fail-closed 证据，linked OpenSSL runtime gate 要求 accepted 证据。
未链接或未通过自检时显式请求 production backend 仍返回 `production-crypto-backend-unavailable` 或固定 provider failure reason，并且不会回退到 draft；filesystem 对象密文读回现在可在 object key、hash/size、信封 `ciphertextSha256`、会话 key id/fingerprint 和服务端续传元数据全部匹配时恢复缺失密文分片，并在发布观测里输出 `e2e-filesystem-object-ciphertext-readback-ready` 的无敏感材料证据；S3 和 offline mirror 密文读回都必须显式 reviewed opt-in，未 opt-in 时 fail-closed；离线密文附件回放可保留 envelope header 并由接收端本地解密。
显式 probe、round-trip execution probe、public primitive execution probe、reviewed candidate、reviewed call handoff、reviewed operation stub boundary、reviewed callable table bridge、reviewed operation callable interface、reviewed callable runtime preflight、reviewed invocation arming、reviewed invocation execution acceptance 和 production data-plane bridge 仍作为测试/审计证据；它们只记录 status、size、material-policy、输出形状和布尔结果，不捕获原始 key/session/private identity/plaintext/ciphertext 字节。详细状态、生产 crypto 剩余风险和下一步门禁见 [E2E hardening status](docs/e2e-hardening-status.md)。

### 文件传输状态时间线

文件传输会把发送端准备清单、续传恢复、完成送达、ACK 拒绝、ACK 超时、临时重试、跨实例大文件失败和离线兜底保留统一映射为用户可读状态。接收端也会把开始接收、分片组包完成、本机保存成功、保存失败、完整性失败和保存文件无法打开接入同一条状态时间线。主界面会同步显示到状态栏、聊天系统消息和聊天提示，并在菜单中提供“复制最近文件状态”，便于把最近一次准备、恢复、接收、保存、打开、失败、重试或兜底状态整理成诊断文本。该诊断只包含文件名、传输 ID、固定 reason、分类、是否可重试和进度字节，不包含对象存储 endpoint、bucket、URL、凭据或签名信息。

### 如何启用 Redis 在线状态服务

默认不启用 Redis，服务端仍使用进程内在线用户表。需要为更高并发或多服务实例部署准备在线状态共享时，可以在启动服务端前设置：

```bash
set QTNETWORKCHAT_REDIS=1
set QTNETWORKCHAT_REDIS_HOST=127.0.0.1
set QTNETWORKCHAT_REDIS_PORT=6379
```

如果本机 Redis 安装在 `D:\Program Files\Redis-8.6.2`，可以直接用脚本启动或复用 Redis，并让程序启用 Redis：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/start-with-redis.ps1
```

脚本会检查或启动该目录下的 `redis-server.exe`，随后设置 `QTNETWORKCHAT_REDIS=1` 再运行 `QtNetworkChat.exe`。Redis 可用时会启用 presence、在线列表共享和 Pub/Sub 跨实例路由；Redis 不可用时仍保留原有内存在线表和本地转发降级能力，便于本地开发、CI 和单机局域网使用。

如 Redis 配置了密码，可额外设置：

```bash
set QTNETWORKCHAT_REDIS_PASSWORD=your_password
set QTNETWORKCHAT_REDIS_PREFIX=qtchat
```

### PostgreSQL / SQLite 并存

服务端账号、离线消息、群组和审计默认继续使用 SQLite：不设置 `QTNETWORKCHAT_DB_DRIVER` 时会读写本地 `accounts.sqlite3`。需要切换到 PostgreSQL 时设置 `QTNETWORKCHAT_DB_DRIVER=QPSQL`，SQLite 文件仍可保留，二者不会互相覆盖。

本机 PostgreSQL 17、Redis 8.6.2 和 MinIO 路径可先用只读检查脚本生成连接计划：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/verify-local-infra.ps1 `
  -PostgresBinDir "D:\Program Files\PostgreSQL\17\bin" `
  -PostgresPassword "<本机 PostgreSQL 密码>" `
  -RedisBinDir "D:\Program Files\Redis-8.6.2" `
  -QtRoot "D:\Qt\6.8.3\mingw_64" `
  -MinioServerPath "D:\Dminio-server\minio.windows-amd64.RELEASE.2025-09-07T16-13-09Z.exe" `
  -MinioClientPath "D:\Dminio-server\mc.windows-amd64.RELEASE.2025-08-13T08-35-41Z.exe" `
  -PlanOnly
```

确认 PostgreSQL 服务和 `qtnetworkchat` 数据库已创建后，去掉 `-PlanOnly` 可执行 `psql select 1` 与 `redis-cli PING` 连接检查。启动服务端前可设置：

```powershell
$env:QTNETWORKCHAT_DB_DRIVER = "QPSQL"
$env:QTNETWORKCHAT_PGHOST = "127.0.0.1"
$env:QTNETWORKCHAT_PGPORT = "5432"
$env:QTNETWORKCHAT_PGDATABASE = "qtnetworkchat"
$env:QTNETWORKCHAT_PGUSER = "postgres"
$env:QTNETWORKCHAT_PGPASSWORD = "<本机 PostgreSQL 密码>"
$env:QTNETWORKCHAT_REDIS = "1"
$env:QTNETWORKCHAT_REDIS_HOST = "127.0.0.1"
$env:QTNETWORKCHAT_REDIS_PORT = "6379"
```

Linux 启动示例：

```bash
export QTNETWORKCHAT_DB_DRIVER=QPSQL
export QTNETWORKCHAT_PGHOST='127.0.0.1'
export QTNETWORKCHAT_PGPORT='5432'
export QTNETWORKCHAT_PGDATABASE='qtnetworkchat'
export QTNETWORKCHAT_PGUSER='postgres'
export QTNETWORKCHAT_PGPASSWORD='<本机 PostgreSQL 密码>'
export QTNETWORKCHAT_REDIS=1
export QTNETWORKCHAT_REDIS_HOST='127.0.0.1'
export QTNETWORKCHAT_REDIS_PORT='6379'
```

服务端内部提供数据库健康快照，覆盖连接打开、`SELECT 1` ping 和账号/消息/离线队列/好友事件/公共群/审计所需表检查；PostgreSQL 配置输出只包含 host、port、database、user 和 `<redacted>` 密码。健康 JSON 中每个 check 都会输出固定 `reason`，成功为 `ok`，失败会收敛到 `runtime`、`auth`、`network`、`tls`、`schema`、`path` 或 `query`，方便仪表盘、告警和计划任务按原因分派。运维侧可用脚本生成 SQLite 或 PostgreSQL 健康计划/检查 JSON：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/check-database-health.ps1 `
  -Driver postgres `
  -QtRoot "D:\Qt\6.8.3\mingw_64" `
  -PostgresBinDir "D:\Program Files\PostgreSQL\17\bin" `
  -PostgresPassword "<本机 PostgreSQL 密码>" `
  -JsonPath "build-qt6-mingw\database-health.json"
```

`-PlanOnly` 只校验 QPSQL 插件、`psql.exe` 和 `libpq.dll` 等本地运行时，不连接真实数据库；去掉 `-PlanOnly` 后会检查服务端必需表。健康 JSON 会输出连接池、线程亲和、慢查询、query failure、release gate 和脱敏 evidence 字段；完整字段与调参说明见 [PostgreSQL operations](docs/postgresql-operations.md)。

需要在真实 PostgreSQL 上演练慢查询或固定失败 reason 时，可给健康检查追加只读探针参数：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/check-database-health.ps1 `
  -Driver postgres `
  -PostgresPassword $env:QTNETWORKCHAT_PGPASSWORD `
  -InjectSlowQueryProbe `
  -SlowQueryProbeSeconds 2 `
  -InjectQueryFailureReason schema `
  -JsonPath "build-qt6-mingw\database-health-probed.json"
```

`-InjectSlowQueryProbe` 会执行 `pg_sleep` 只读探针，并按 `-SlowQueryMs` 计入 `queryMetrics.slowQueryCount`；`-InjectQueryFailureReason` 只把固定 reason 桶写进脱敏 `queryMetrics.errorReasons`，用于演练 `query-failures` gate，不会写入或破坏业务表。状态、dashboard 和 PostgreSQL release acceptance 会透传这些指标：存在 query failure 时 release gate 降为 `review-query-failures`，只有慢查询时降为 `review-slow-queries`。

需要让服务端在启动本地托管服务后落盘健康快照，可启用启动导出：

```powershell
$env:QTNETWORKCHAT_DB_HEALTH_EXPORT = "1"
$env:QTNETWORKCHAT_DB_HEALTH_JSON = "build-qt6-mingw\database-health-startup.json"
```

未设置 `QTNETWORKCHAT_DB_HEALTH_JSON` 时会写入应用数据目录下的 `database-health.json`。导出内容包含 `source=server-startup`、数据库驱动、脱敏配置、open/ping/required-tables/connection-pool 检查结果、连接复用计数、打开失败数、退避跳过数、连接池空闲/溢出回收计数、跨线程 checkout/release 拦截计数、池内线程归属数量、查询失败数、慢查询计数和整体状态，不写入真实 PostgreSQL 密码。连接池遵循 Qt SQL 线程亲和约束：同名连接若被其他线程 checkout，会丢弃旧连接并重建；连接若从非拥有线程 release，会直接关闭而不会回到池中。

已有健康快照可再转成只读运维状态 JSON/Markdown：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/show-database-health-status.ps1 `
  -HealthPath "build-qt6-mingw\database-health-startup.json" `
  -JsonPath "build-qt6-mingw\database-health-status.json" `
  -MarkdownPath "build-qt6-mingw\database-health-status.md" `
  -FailOnUnhealthy
```

状态脚本会汇总 `status`、`driver`、检查数量、失败检查和敏感字段扫描结果；发现未脱敏的 `password`、access key、signature 等内容时会把状态降为 unhealthy，并在 `-FailOnUnhealthy` 下非零退出。

需要交给 Windows 计划任务周期执行时，先生成 preview 和启动脚本：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/register-database-health-task.ps1 `
  -TaskName "QtNetworkChatDatabaseHealth" `
  -Schedule Hourly `
  -EveryHours 4 `
  -Driver postgres `
  -QtRoot "D:\Qt\6.8.3\mingw_64" `
  -PostgresBinDir "D:\Program Files\PostgreSQL\17\bin" `
  -OutputDir "build-qt6-mingw\database-health-scheduled" `
  -PlanOnly `
  -FailOnUnhealthy `
  -WriteMarkdown `
  -WriteDashboard
```

默认只写 `database-health-task-preview.json` 和 `run-database-health-task.ps1`，不会创建系统计划任务；确认 preview 后再追加 `-Register`。启动脚本运行时从 `QTNETWORKCHAT_PGPASSWORD` 读取 PostgreSQL 密码，仓库、preview、launcher 和 `last-run.log` 都只记录密码来源，不写入真实密码。去掉 `-PlanOnly` 后，计划任务会真实检查 PostgreSQL 必需表并生成 `database-health.json`、`database-health-status.json`；开启 `-WriteMarkdown` 时生成 Markdown 值班摘要，开启 `-WriteDashboard` 或显式传入 `-DashboardPath` / `-DashboardMarkdownPath` 时会继续生成 `database-health-dashboard.json`、`database-health-dashboard.md`。同一次运行结束后，launcher 还会根据 `last-run.log` 与同目录 `automation-task-ack.json` 自动生成 `automation-task-history.json`、`automation-task-history.md`，供状态板和人工值班统一回读。

计划任务或手工检查产生的健康 JSON、状态 JSON 和 preview 可以聚合成一个本地仪表盘：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/write-database-health-dashboard.ps1 `
  -HealthPath "build-qt6-mingw\database-health-scheduled\database-health.json" `
  -StatusPath "build-qt6-mingw\database-health-scheduled\database-health-status.json" `
  -TaskPreviewPath "build-qt6-mingw\database-health-scheduled\database-health-task\database-health-task-preview.json" `
  -DashboardPath "build-qt6-mingw\database-health-scheduled\database-health-dashboard.json" `
  -MarkdownPath "build-qt6-mingw\database-health-scheduled\database-health-dashboard.md" `
  -FailOnUnhealthy
```

该仪表盘只读取本地脱敏产物，不连接 PostgreSQL/Redis/S3/MinIO，也不修改数据库；它会汇总健康状态、驱动、检查数量、失败检查、计划任务配置、密码来源和敏感字段扫描结果，发现未脱敏密码或签名字段时会标记 unhealthy。

仪表盘结果可继续转成告警摘要或通知入口：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/notify-database-health-unhealthy.ps1 `
  -DashboardPath "build-qt6-mingw\database-health-scheduled\database-health-dashboard.json" `
  -AlertPath "build-qt6-mingw\database-health-scheduled\database-health-alert.json" `
  -MarkdownPath "build-qt6-mingw\database-health-scheduled\database-health-alert.md" `
  -DryRun `
  -FailOnUnhealthy
```

健康状态会生成 `severity=info` 且不通知；`unhealthy`、`unknown` 或敏感字段命中会生成可机读 alert JSON，`-DryRun` 下只打印将写入 EventLog/webhook 的内容。实际接入时可配置 `-EventLogSource` 或 `-WebhookUrl`，脚本会拒绝包含未脱敏密码、access key、签名字段的消息和输入。

需要用真实 Qt QPSQL 插件跑服务端协议 smoke 时，先完成构建，再运行：

```powershell
$env:QTNETWORKCHAT_PGPASSWORD = "<本机 PostgreSQL 密码>"
powershell -ExecutionPolicy Bypass -File scripts/run-pgsql-protocol-smoke.ps1 `
  -TestExe "build-qt6-mingw\postgres_qpsql_protocol_smoke_test.exe" `
  -QtRoot "D:\Qt\6.8.3\mingw_64" `
  -PostgresBinDir "D:\Program Files\PostgreSQL\17\bin" `
  -PostgresPassword $env:QTNETWORKCHAT_PGPASSWORD `
  -EnsureDatabase `
  -BootstrapJsonPath "build-qt6-mingw\local-postgres.json" `
  -JsonPath "build-qt6-mingw\pgsql-protocol-smoke.json" `
  -MarkdownPath "build-qt6-mingw\pgsql-protocol-smoke.md"
```

`start-local-postgres.ps1` 与 `run-pgsql-protocol-smoke.ps1 -EnsureDatabase` 可串联本地 bootstrap、真实 QPSQL smoke、离线附件异常、分片恢复、公共群审计和脱敏 evidence。完整覆盖面、bootstrap/recovery/audit summary 字段和安全边界见 [PostgreSQL operations](docs/postgresql-operations.md)。

已有 SQLite 账号库迁移到 PostgreSQL 时，先跑 plan 模式生成脱敏迁移计划：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/migrate-sqlite-to-postgres.ps1 `
  -MigratorExe "build-qt6-mingw\sqlite_to_postgres_migrator.exe" `
  -SQLitePath "accounts.sqlite3" `
  -QtRoot "D:\Qt\6.8.3\mingw_64" `
  -PostgresBinDir "D:\Program Files\PostgreSQL\17\bin" `
  -PostgresPassword "<本机 PostgreSQL 密码>" `
  -Mode plan `
  -JsonPath "build-qt6-mingw\sqlite-pg-migration-plan.json" `
  -MarkdownPath "build-qt6-mingw\sqlite-pg-migration-plan.md" `
  -HtmlPath "build-qt6-mingw\sqlite-pg-migration-plan.html" `
  -RollbackPreviewPath "build-qt6-mingw\sqlite-pg-rollback-preview.json" `
  -RollbackPreviewMarkdownPath "build-qt6-mingw\sqlite-pg-rollback-preview.md" `
  -RollbackAuditPath "build-qt6-mingw\sqlite-pg-rollback-audit.json" `
  -RollbackAuditMarkdownPath "build-qt6-mingw\sqlite-pg-rollback-audit.md"
```

确认计划中的表和行数符合预期后，把 `-Mode plan` 改为 `-Mode execute` 执行导入；后续可用 `validate`、`diff` 和 `rollback` 复核。迁移、rollback preview/audit、release gate 和 cutover 建议的详细解释见 [PostgreSQL operations](docs/postgresql-operations.md)。

若需要把数据库健康、真实 smoke、迁移计划和 rollback dry-run 预览汇总成一份统一的 PostgreSQL 发布验收摘要，可在上述产物生成后执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/write-pgsql-release-acceptance.ps1 `
  -DatabaseHealthDashboardPath "build-qt6-mingw\database-health-scheduled\database-health-dashboard.json" `
  -SmokeJsonPath "build-qt6-mingw\pgsql-smoke.json" `
  -MigrationJsonPath "build-qt6-mingw\sqlite-pg-migration-plan.json" `
  -RollbackPreviewPath "build-qt6-mingw\sqlite-pg-rollback-preview.json" `
  -JsonPath "build-qt6-mingw\pgsql-release-acceptance.json" `
  -MarkdownPath "build-qt6-mingw\pgsql-release-acceptance.md"
```

该摘要会统一输出 `summary.readiness`、`summary.operatorAction`、聚合后的 `auditSummary.releaseGate` / `auditFocus` / `evidenceBundle`，以及 `releaseDetails`，把 bootstrap 是否仍必需、smoke recovery hint、migration write intent、backupRequired、rollbackPreviewAvailable、rollback risk、rollback audit gate 和 rollback 是否已执行放在同一处，方便值班或自动化先决定当前是 `review-pgsql-evidence` 还是已经接近 cutover review。脚本只读取本地脱敏 JSON 产物，不连接 PostgreSQL，也不会修改数据库。

若要把这份统一验收摘要接进 Windows 计划任务或自动化状态板，可先生成 preview 和 launcher：

```powershell
$env:QTNETWORKCHAT_PGPASSWORD = "<本机 PostgreSQL 密码>"
powershell -ExecutionPolicy Bypass -File scripts/register-pgsql-release-acceptance-task.ps1 `
  -TaskName "QtNetworkChatPgsqlReleaseAcceptance" `
  -Schedule Hourly `
  -EveryHours 6 `
  -At "04:45" `
  -OutputDir "build-qt6-mingw\pgsql-release-acceptance-task" `
  -QtRoot "D:\Qt\6.8.3\mingw_64" `
  -PostgresBinDir "D:\Program Files\PostgreSQL\17\bin" `
  -TestExe "build-qt6-mingw\postgres_qpsql_protocol_smoke_test.exe" `
  -MigratorExe "build-qt6-mingw\sqlite_to_postgres_migrator.exe" `
  -EnsureDatabase `
  -InjectSlowQueryProbe `
  -SlowQueryProbeSeconds 2 `
  -InjectQueryFailureReason schema `
  -FailOnUnhealthy
```

默认只写 preview 和 launcher，不注册系统计划任务；确认 preview 后再追加 `-Register`。launcher 从 `QTNETWORKCHAT_PGPASSWORD` 读取密码并生成 health、smoke、migration、rollback、acceptance、history/ack 和 evidence zip，状态板可按 `taskKind=pgsql-release-acceptance` 回读。

需要把 rollback audit 跑成一次真实 PostgreSQL live evidence 时，可使用受控样例编排脚本。它会生成临时 SQLite 样例，先执行 execute/diff/rollback preview，再真实执行 rollback 并生成 after audit，同时串联数据库健康、真实 QPSQL smoke、release acceptance、history/ack 和 evidence zip；所有密码字段仍只写 `<redacted>`：

```powershell
$env:QTNETWORKCHAT_PGPASSWORD = "<本机 PostgreSQL 密码>"
powershell -ExecutionPolicy Bypass -File scripts/run-pgsql-rollback-live-evidence.ps1 `
  -OutputDir "build-qt6-mingw\pgsql-rollback-live-evidence" `
  -QtRoot "D:\Qt\6.8.3\mingw_64" `
  -PostgresBinDir "D:\Program Files\PostgreSQL\17\bin" `
  -PostgresPassword $env:QTNETWORKCHAT_PGPASSWORD `
  -EnsureDatabase
```

完成后总览产物为 `pgsql-rollback-live-evidence.json/.md`，其中 `summary.releaseGate=can-close-pgsql-rollback-live-evidence` 表示 before preview、rollback execute、after audit、release acceptance 与 evidence package 都已生成并通过脱敏扫描。默认 CTest 只跑该脚本的 `-PlanOnly` 验证，不连接真实 PostgreSQL、不读取密码。

如果只需要把已有脱敏产物统一归档，也可以直接调用 evidence 打包脚本：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-pgsql-release-evidence.ps1 `
  -OutputDir "build-qt6-mingw\pgsql-release-acceptance-task\evidence" `
  -DatabaseHealthDashboardPath "build-qt6-mingw\pgsql-release-acceptance-task\database-health-dashboard.json" `
  -SmokeJsonPath "build-qt6-mingw\pgsql-release-acceptance-task\pgsql-smoke.json" `
  -MigrationJsonPath "build-qt6-mingw\pgsql-release-acceptance-task\sqlite-pg-migration-plan.json" `
  -RollbackPreviewPath "build-qt6-mingw\pgsql-release-acceptance-task\sqlite-pg-rollback-preview.json" `
  -AcceptanceJsonPath "build-qt6-mingw\pgsql-release-acceptance-task\pgsql-release-acceptance.json"
```

打包脚本只复制本地文件并扫描敏感字段；发现明文密码、PAT、Authorization、Credential 或 Signature 会失败，除非显式用于本地排障的 `-NoFailOnSensitive`。

启用后，服务端会在用户登录和心跳时写入 `qtchat:presence:<QQ号>`，并设置短 TTL，同时维护 `qtchat:presence:users` 在线索引；用户断开或服务端停止时会主动删除该在线状态。发送在线列表时，服务端会把本实例内存在线表与 Redis presence 合并，因此多个服务实例连接同一个 Redis 时可以共享在线用户视图。普通群聊和私聊消息完成本地投递后，会发布带 `instanceId` 的 `qtchat:pubsub:messages` 事件；服务端也会订阅该通道，跳过本实例事件，并把远端群聊/私聊转发给本实例在线用户。文件和图片只在编码后的 Redis 事件体不超过 1 MB 时通过 Pub/Sub 路由；超过该限制的 payload 会留在源实例离线附件队列，后续应按 [Redis 跨实例大文件路由计划](docs/redis-large-file-routing-plan.md) 通过控制面事件加对象存储式数据面承载。Redis 不可用时服务端会回退到原有内存在线表和本地转发，不影响局域网单机服务端运行。

### 客户端连接不上服务器

- 确认服务器端已经点击创建服务器。
- 确认客户端填写的是服务器电脑的局域网 IP。
- 确认端口一致，默认端口通常为 `8888`。
- 检查 Windows 防火墙是否允许程序访问网络。
- 跨公网或不同 NAT 网络时，需要额外配置端口映射、内网穿透或公网服务器。

### Qt Creator 报找不到头文件

请重新执行 qmake，或确认 `QtNetworkChat.pro` 中已包含 `include` 目录。

### CMake 找不到 Qt

需要把 Qt 安装路径加入 `CMAKE_PREFIX_PATH`，例如：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/mingw_64"
```

### 旧账号显示异常

旧版本如果使用昵称作为账号注册，建议重新注册新账号，以便使用随机 QQ 号登录流程。

## 收官后维护边界

1. **文档 / 状态 / 脚本口径**：当前 README、`docs/automation-status.md`、`docs/release-closeout.md`、`docs/e2e-hardening-status.md` 与 closeout 脚本生成物应保持同一套 current-head closeout 语义；其中 `docs/automation-status.md` 记录的是最近一次已验证证据基线，不要求机械等于包含该文件的最新提交。后续若 closeout 产物、状态板或脚本输出发生变化，只做同一轮维护性同步，不再把这条线表述成未完成主任务。
2. **本地 release closeout 基线**：current-HEAD 的 `local-release-review`、`release-archive-decision`、`release-delivery-handoff`、`release-closeout-summary` 和 `release-final-local-archive` 现在共同构成 stop-writing 基线。环境外发布仍是显式记录项，但不再反向否定已经完成的本地收官链。
3. **高价值补强与仓库清理**：后续只补少量最值钱的回归测试、维护型清理和文档一致性修正，不再在当前分支开启新的产品主线。
4. **环境外发布跟踪**：如果后面确实把生成好的 zip 发到团队共享盘、工单系统或发行页面，再通过 `release-publication-record` 或 archive decision 回写发布状态；这属于环境外动作，不是当前仓库 closeout 的前置条件。
5. **运维后续动作**：当前 live evidence 下，PostgreSQL follow-up=`can-review-cutover`、automation acknowledgement follow-up=`passing`、acknowledgement renewal follow-up=`not-required`。后续如果真实值班产物再次变化，状态板应继续按 live evidence 回读并滚动更新，可能回到 `review-slow-queries`、`review-query-failures`、`acknowledged-operational-remediation-gated`、`renew-soon` 或 `renew-required` 等运维态；这些都属于运维队列，不等于当前分支代码或产品闭环倒退。

## 说明

本项目主要用于学习和演示 Qt 桌面开发、TCP 网络通信和即时通讯系统设计。当前分支已经完成仓库范围内的 closeout；如果后续要把它继续推进到更严格的生产环境要求，应作为新的独立主线评估安全性、稳定性、离线传输治理和传输性能，而不是回溯性否定当前本地收官结论。


