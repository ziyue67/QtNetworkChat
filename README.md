# QtNetworkChat

[![Windows Build](https://github.com/ziyue67/QtNetworkChat/actions/workflows/windows-build.yml/badge.svg)](https://github.com/ziyue67/QtNetworkChat/actions/workflows/windows-build.yml)

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
- 公共群成员面板支持群主/管理员按 QQ 邀请或右键移出成员
- 服务端会阻止已移出公共群的账号继续发送公共群消息或文件
- 服务端记录公共群移出状态，避免被移出账号重登后自动重新入群
- 客户端会提示公共群被移出/重新加入状态，并在被移出时禁用公共群发送入口
- 好友私聊
- QQ 号搜索用户
- 好友申请、同意、拒绝
- 在线好友和离线好友列表
- 发送普通文件、图片、视频等媒体文件
- 文件和图片支持分片传输、进度提示、取消发送、ACK 已接收字节回传、服务端/客户端断点续传状态查询、发送端查询并校验续传状态后按最早缺失分片续传、跳过服务端已确认分片、发送端本地保存并通过菜单入口恢复/清除续传元数据，恢复失败时会保留记录并提示稍后重试或清除、过期恢复状态自动清理、ACK 超时后自动查询续传状态、ACK 超时重试、临时拒绝重试和完整性校验
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
- CTest 覆盖消息序列化、账号密码 PBKDF2-SHA256 KDF 新注册入库、旧 SHA-256 派生账号兼容登录与成功后升级、错误密码不升级、文件分片 ACK 接收进度、重复分片 ACK 去重、空分片、越界分片、分片数量不一致、非末尾分片大小和同传输元数据变更拒绝、fileHash 变更拒绝并清理 pending 续传状态、伪造 senderId 分片拒绝、跨账号续传查询不泄露 pending 元数据、乱序缺片保持未完成状态、服务端/客户端断点续传状态查询、查询续传状态后校验元数据并按最早缺失分片续传、跨连接恢复持久化发送状态并只发送下一缺失分片、跳过已确认分片并按实际 received chunks 汇总进度、服务端组包后 hash 不一致拒绝、在线接收端拒绝转发分片后回落离线附件队列、发送端续传元数据持久化与恢复 helper、完整续传状态清理持久化发送状态、过期恢复状态清理、离线附件配额拒绝、离线附件成功回放后的队列和附件清理、离线附件缺失/大小/hash/chunkSize/chunkCount 异常提示和队列清理、离线附件队列持久化失败回滚、离线附件下发中断后队列和附件保留及重试成功清理、离线附件拒绝 ACK、非法确认进度和部分 ACK 后重试成功清理、部分 ACK 后记录 confirmedBytes/confirmedChunks/resumeUpdatedAt 并按 confirmedBytes 续发、confirmedChunks 为空时按有效 confirmedBytes 续发、过期或超过自定义 TTL 的 resumeUpdatedAt 回退完整回放、无效 confirmedBytes 回退完整回放、非法 confirmedChunks 回退 confirmedBytes 续发、confirmedBytes 与 confirmedChunks 冲突时按 confirmedChunks 最早缺口续发、confirmedChunks 非连续缺口从最早缺失分片续发、重复 confirmedChunks 去重后仍从最早缺口续发、confirmedChunks 覆盖全部分片时不再发送分片并直接清理、孤儿离线附件启动清理、仍被队列引用的离线附件启动保留、自定义 TTL 过期离线附件启动清理和后续缺失提示、ACK 超时后自动查询续传状态、分片 ACK 丢失、临时拒绝和非法确认进度后的发送端/服务端转发重试、硬拒绝不重试、文件取消清理、服务端群成员变更、重复成员添加拒绝、群主自移除保护和公告权限协议
- CTest 覆盖客户端登录凭据本地存储安全：记住登录只保存账号、昵称和记住标志，不保存明文密码；旧 SQLite/QSettings 明文密码会在加载或迁移时清理
- CTest 覆盖 Redis RESP 命令编码、响应解析、presence 命令流、Pub/Sub 发布/订阅基础、断线重订阅、跨实例群聊/私聊/小文件/小图片路由、订阅侧超大文件/图片事件拒收、编码后超限的 Redis file/image 事件拒收、缺少 senderId、非聊天 eventType、空消息或未知类型的 Redis 事件拒收、大文件和编码后超限文件不经 Pub/Sub 并离线兜底、源实例写入 ObjectStore 后发布带 `storeType` 的 `large_file_offer` 元数据、远端实例仅在本地 ObjectStore 类型与 offer 匹配时认领 offer 并校验对象后分片下发、完整 ACK 后发布 `large_file_delivered` 并清理源实例兜底状态、源实例只读输出 `delivered_reconcile` cleaned/retained 日志、远端对象下发阶段以 `offer_delivery operation=deliver` 输出固定 reason 失败日志、对象缺失等失败路径发布 `large_file_failed` 且源实例保留离线兜底、非法 objectKey/分片元数据不下发、offer storeType 不支持或与本地配置不匹配时固定 reason 拒绝且不 claim、非 filesystem store 不消费、过期未 delivered 对象可由 ObjectStore TTL 清理且离线兜底仍可回放、私聊文本和小文件发布失败离线兜底、Redis 不可用降级登录
- CTest 覆盖 filesystem ObjectStore 的安全 objectKey 生成、路径穿越拒绝、hash/size 校验、TTL 清理、S3 配置/URL/Signature V4 纯函数、不联网 Qt Network 请求构造、S3 对象方法白名单、HTTP 状态分类、请求超时配置、transfer timeout 写入、可选 session token 签名头、显式启用开关、请求结果归一化、失败 reason 聚合、错误脱敏、注入式 PUT/GET/HEAD/DELETE 执行边界、GET 响应体 size/hash 校验、GET/open 与 DELETE/remove 失败 reason 接线、Qt Network 执行器非法请求 fail-closed，以及跨实例大文件 offer 指向对象的 size/hash 校验和 delivered 回执清理判定纯函数
- 复制当前 QQ 账号
- 退出登录并回到登录流程

## 项目结构

```text
QtNetworkChat/
├── include/
│   ├── chatuser.h       # 用户数据结构
│   ├── client.h         # TCP 客户端接口
│   ├── mainwindow.h     # 主窗口接口
│   ├── redisclient.h    # 可选 Redis 在线状态服务客户端
│   ├── message.h        # 消息结构与序列化
│   └── server.h         # TCP 服务端接口
├── src/
│   ├── client.cpp       # 客户端连接、收发消息、登录注册协议
│   ├── main.cpp         # 程序入口、登录/注册窗口、启动流程
│   ├── mainwindow.cpp   # 主界面、聊天、好友、文件、历史记录
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

### 使用 CMake

```bash
cmake -S . -B build
cmake --build build
```

如果 CMake 找不到 Qt，需要设置 `CMAKE_PREFIX_PATH`，或配置 `Qt6_DIR` / `Qt5_DIR`。

### 自动化验证

```bash
ctest --test-dir build --output-on-failure
```

当前 CTest 会执行构建产物冒烟测试，并覆盖消息序列化、账号密码 PBKDF2-SHA256 KDF 新注册入库、旧 SHA-256 派生账号兼容登录与成功后升级、错误密码不升级、客户端登录凭据本地存储不落明文密码和旧明文清理、服务端群成员变更、重复成员添加拒绝、群主自移除保护、文件分片 ACK 接收进度、重复分片 ACK 去重、空分片、越界分片、分片数量不一致、非末尾分片大小和同传输元数据变更拒绝、fileHash 变更拒绝并清理 pending 续传状态、伪造 senderId 分片拒绝、跨账号续传查询不泄露 pending 元数据、乱序缺片保持未完成状态、服务端/客户端断点续传状态查询、发送端按确认位置续传、查询续传状态后校验元数据并按最早缺失分片续传、跨连接恢复持久化发送状态并只发送下一缺失分片、跳过已确认分片并按实际 received chunks 汇总进度、服务端组包后 hash 不一致拒绝、在线接收端拒绝转发分片后回落离线附件队列、发送端续传元数据持久化与恢复 helper、完整续传状态清理持久化发送状态、过期恢复状态清理、离线附件配额拒绝、离线附件成功回放后的队列和附件清理、离线附件缺失/大小/hash/chunkSize/chunkCount 异常提示和队列清理、离线附件队列持久化失败回滚、离线附件下发中断后队列和附件保留及重试成功清理、离线附件拒绝 ACK、非法确认进度和部分 ACK 后重试成功清理、部分 ACK 后记录 confirmedBytes/confirmedChunks/resumeUpdatedAt 并按 confirmedBytes 续发、confirmedChunks 为空时按有效 confirmedBytes 续发、过期或超过自定义 TTL 的 resumeUpdatedAt 回退完整回放、无效 confirmedBytes 回退完整回放、非法 confirmedChunks 回退 confirmedBytes 续发、confirmedBytes 与 confirmedChunks 冲突时按 confirmedChunks 最早缺口续发、confirmedChunks 非连续缺口从最早缺失分片续发、重复 confirmedChunks 去重后仍从最早缺口续发、confirmedChunks 覆盖全部分片时不再发送分片并直接清理、孤儿离线附件启动清理、仍被队列引用的离线附件启动保留、自定义 TTL 过期离线附件启动清理和后续缺失提示、ACK 超时后自动查询续传状态、分片 ACK 丢失、临时拒绝和非法确认进度后的发送端/服务端转发重试、硬拒绝不重试、文件取消清理、filesystem ObjectStore 安全 key/校验/清理、Redis RESP 协议基础能力、presence 写入/读取命令流、Pub/Sub 发布/订阅基础、断线重订阅、跨实例群聊/私聊/小文件/小图片路由、订阅侧超大文件/图片事件拒收、编码后超限的 Redis file/image 事件拒收、缺少 senderId、非聊天 eventType、空消息或未知类型的 Redis 事件拒收、大文件和编码后超限文件不经 Pub/Sub 并离线兜底、私聊文本和小文件发布失败离线兜底、Redis 不可用时的登录降级。

CTest wrapper 会把 `QTNETWORKCHAT_APPDATA_DIR` 指向构建目录下的隔离运行时数据目录，避免 Windows 用户 Roaming 目录不可写或残留真实账号库时影响协议测试。手动压测或临时双实例验证也可设置该变量来隔离 `accounts.sqlite3`、离线队列、离线附件和发送端恢复状态；生产运行通常不需要设置，且不要把它指向源码目录或包含真实运行数据的共享目录。

### Windows 打包

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1 -BuildDir build-qt6-mingw
```

脚本会先构建项目，再把 `QtNetworkChat.exe` 和 `README.md` 收集到 `dist/QtNetworkChat-win-x64`。如果系统能找到 `windeployqt.exe`，会自动复制 Qt 运行库，并生成 `dist/QtNetworkChat-win-x64.zip`。

### 可选 MinIO S3 手动验证

S3/MinIO 后端默认仍保持关闭；需要验证当前 SigV4 签名和 path-style 请求边界时，可手动运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/minio-s3-smoke.ps1
```

脚本会尝试通过 Docker 启动本地 MinIO，并用 SigV4 完成 bucket 创建和对象 PUT/HEAD/GET/DELETE smoke。若已自行启动 MinIO，可追加 `-SkipContainer` 并传入 `-Endpoint`、`-Bucket`、`-AccessKey`、`-SecretKey`、`-Region`、`-Prefix`。该脚本不属于默认 CTest 或 CI 前置条件。

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

`QTNETWORKCHAT_OBJECT_S3_ENABLE` 未显式设为 `1/true/yes/on` 时，`s3` 工厂会继续 fail-closed。真实后端失败时仍保留源实例离线附件兜底；日志、Redis 事件和离线队列只允许记录固定 reason 桶和逻辑 `objectKey`，不得写入 endpoint、bucket、object URL、access key、secret key、session token、Authorization、Credential 或 Signature。

完整的真实后端人工验收步骤见 `docs/s3-minio-manual-acceptance.md`：先跑 smoke，再启动 Redis 与两个服务端实例，验证 `large_file_offer/claim/delivered/failed`、`redis_large_file_route` 安全字段、失败回退、回源离线兜底回放，以及可选 delivered receipt 只读对账演练。该清单仍不纳入默认 CTest 或 CI。

验收日志可用 `scripts/analyze-large-file-route-logs.ps1` 做只读聚合和脱敏扫描，统计 `event/result/reason/storeType/operation`，并按 `transferId/objectKey/receiverId` 输出 delivered cleanup、`delivered_reconcile` 只读对账事件与 failed fallback 的对账候选摘要，`-SummaryPath` 可额外落盘 `failedFallbackRetained`、`failedWithoutFallback`、`deliveredWithoutCleanup` 和 reason 计数等机器可读 JSON；`scripts/analyze-large-file-route-summary.ps1` 可读取该 summary 并按 failed/delivered 阈值告警；`scripts/analyze-s3-request-results.ps1` 可聚合 `storeType=s3` 且对象请求相关 operation 的固定 reason 桶，并对 timeout/retryable/auth/tls/hash/size 做阈值告警。脚本在 `redis_large_file_route` 行里发现 endpoint、bucket、object URL、access key、secret key、session token、Authorization、Credential 或 Signature 时失败。源实例可通过 `QTNETWORKCHAT_DELIVERED_RECEIPT_DIR` 显式落盘脱敏 `delivered-receipts.jsonl` 摘要；若要离线评估 delivered receipt 与源实例兜底摘要是否满足清理条件，可用 `scripts/run-large-file-delivery-reconcile.ps1 -ReceiptPath` 直接读取持久化 receipt，或用 `-RouteLogPath` 从安全 route log 导出 receipt，再串联源实例离线队列摘要的 fallback 导出和只读对账；传入 `-RunS3Analysis` 时会复用 `-RouteLogPath` 生成 `s3-analysis-summary.json`。脚本输出 `cleaned` 或 `retained` 以及固定 reason，并可生成只读 `delivered_reconcile` route log，不连接 Redis/S3 或修改队列。`scripts/rotate-large-file-receipts.ps1` 可按保留条数和保留天数轮转 `delivered-receipts.jsonl`，把旧摘要归档为 JSONL 或 ZIP，并可通过 `-SummaryPath` 落盘轮转摘要；`scripts/analyze-large-file-receipt-rotation.ps1` 可读取该摘要并按 retained/archived/sensitiveHits 阈值告警。保留条数、天数、归档目录、压缩开关和摘要路径也可由 `QTNETWORKCHAT_DELIVERED_RECEIPT_*` 环境变量提供默认值。它同样拒绝敏感字段，只治理 receipt 摘要文件，不会清理离线队列、附件或对象。需要本地演练输入时，可用 `scripts/write-large-file-reconcile-sample.ps1 -RunReconcile -RunRotate -RunS3RequestAnalysis` 生成脱敏样例并直接跑完整只读对账、轮转和 S3 请求结果分析链路；真实人工验收完成后，可用 `scripts/package-large-file-acceptance.ps1` 打包 route log、summary、对账输出、轮转摘要和结论 notes，脚本会再次扫描敏感字段并生成 `manifest.json`；也可用 `scripts/run-large-file-governance.ps1` 一键串联日志聚合、S3 reason 分析、delivered 对账、receipt 轮转告警和验收包打包，并额外生成 route/S3/receipt rotation 三类统一 `kind/ok/warnings/metrics` alert summary JSON；`scripts/show-large-file-governance-status.ps1` 可读取 dashboard/health/alert overview 输出值班摘要、JSON 或 Markdown，并可用 `-FailOnUnhealthy` 让计划任务在 unhealthy/unknown 时非零退出；`scripts/write-s3-failure-batch-sample.ps1 -RunAnalysis` 可生成不含真实 endpoint/bucket/凭据的批量 S3 失败 route log 样例，覆盖 timeout/network/tls/auth/retryable/server/hash/size/deliver/delete 等固定桶并复用 S3 request result 分析器；治理入口和计划任务 preview 也可用 `-RunS3FailureBatchSample` 把该样例纳入 alert overview、dashboard 和诊断产物；真实验收完成后，`scripts/verify-s3-real-backend-evidence.ps1` 可把 S3 summary、route summary、governance status、smoke log 和 notes 汇总成脱敏 evidence JSON/Markdown，并校验成功路径与固定失败 reason 是否都出现；`scripts/write-s3-stability-runbook.ps1` 可进一步读取 S3 summary、evidence 和 governance status，生成只读稳定化 runbook JSON/Markdown，把 timeout/retryable/network/server/auth/tls/hash/size/not_found 固定桶转成处置建议和验证口径；治理入口可用 `-WriteS3StabilityRunbook` 直接生成 runbook，计划任务 preview 也会透传对应路径；evidence 与 runbook 产物放入治理目录后会被报告、诊断包、统一 alert overview、dashboard 和 status CLI 自动采集。需要把治理入口交给 Windows 定时运行时，先用 `scripts/register-large-file-governance-task.ps1` 生成默认 preview 的启动脚本和计划任务 JSON；只有显式传入 `-Register` 才创建或更新 Windows Scheduled Task。所有这些运维脚本都不连接 Redis/S3，不修改离线队列、附件或对象；只有显式传入 receipt 轮转路径时会治理脱敏 receipt 摘要文件，且保留条数、保留天数或压缩开关必须与 `ReceiptRotationPath` 一起配置。

失败回退演练可用 `scripts/s3-failure-drill.ps1 -Scenario all` 先列出 network、auth、missing-object、receiver-disconnect 场景的注入方式、预期固定 reason 和兜底检查点；该脚本默认只输出步骤，不连接 Redis、S3/MinIO 或修改队列。

### GitHub Actions

仓库包含 `.github/workflows/windows-build.yml`。推送到 `main` 或提交 PR 时会自动安装 Qt、构建项目并运行 CTest。手动触发该工作流时，还会运行 Windows 打包脚本并上传 `QtNetworkChat-win-x64.zip`。

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
- 后续优化重点不再是基础协议补洞，而是失败提示体验、跨实例大文件回放设计、治理指标和传输性能优化。

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

## 后续优化优先级

1. **Redis 跨实例大文件治理**：跨实例大文件链路已经形成 filesystem 与显式启用 S3/MinIO 后端的 fail-closed 闭环。源实例会先进入离线兜底队列，再按 `storeType` 发布小体积 `large_file_offer`；远端只认领本地后端匹配的 offer，unsupported/mismatch、对象缺失、校验失败、客户端断开或 ACK 超时都会发布固定 reason 的 `large_file_failed` 并保留源实例离线兜底。服务端已提供测试专用 ObjectStore 工厂注入点，默认 CTest 使用注入替身，不连接真实 S3/MinIO。

   S3 稳定化覆盖已从对象层推进到服务端路由层：写入、HEAD/GET 校验、GET/open 读取、DELETE/remove 清理失败都会收敛到 timeout、network、tls、auth、retryable、client、server、not_found、hash、size、unknown 等固定桶，route log、Redis 事件、离线队列、报告和诊断包不携带 endpoint、bucket、object URL、凭据、session token、Authorization/Credential/Signature 或底层错误文本；delivered cleanup 仍以离线兜底匹配结果为准，不会因为 S3 删除失败误删或误报。跨实例测试已覆盖源端写入失败不发布 offer、远端 validate/read fail-closed、源端 delete retained、下发失败 fallback retained，以及收件人回源实例后的离线回放。

   运维闭环已经接入一键治理入口、真实后端 evidence 汇总、S3 批量失败演练、S3 稳定化 runbook、统一 alert summary、健康检查、JSON/Markdown dashboard、只读 status CLI、Markdown/HTML 运维报告和治理诊断 zip。`stabilizationCoverage` 会在 runbook、dashboard、status、report 和 diagnostics manifest 中展示默认测试已覆盖的稳定化边界、固定 reason 桶和观测缺口，便于人工验收和计划任务消费。下一步优先做“覆盖缺口告警/状态门禁”或“真实后端 retry/timeout/TLS/hash/size 组合证据”这类横跨服务端、脚本、测试和文档的大块功能包，不再继续堆零碎字段补丁。
2. **安全增强**：账号密码已升级为带盐 PBKDF2-SHA256 KDF，并兼容旧 SHA-256 派生账号的登录后迁移；客户端本地登录记忆已改为不保存明文密码并清理旧明文；TLS 已有可选入口，但还缺证书链校验体验、指纹固定配置，后续可继续评估端到端加密。
3. **群组和权限边界**：服务端群组模型已覆盖核心成员变更、重复成员添加拒绝、公告权限和群主自移除保护，后续可补更多边界测试，例如管理员角色、私有群、群文件权限和被移出后的历史可见性。
4. **结构拆分**：`mainwindow.cpp` 已承载聊天、好友、群组、文件、历史和恢复入口，后续应小步抽出 TransferManager、FriendManager、GroupManager、HistoryService、Storage，降低 UI 层复杂度。
5. **发布与运维体验**：补版本号注入、Release 自动上传、安装包、运行时依赖校验、崩溃日志和可选诊断日志，方便非开发环境使用。
6. **文件传输后续收尾**：在线文件/图片已覆盖 ACK 超时续传、跨连接持久化续传、元数据冲突隔离、临时拒绝重试、硬拒绝不重试和离线附件缺口续发；后续只建议补用户可见状态、治理指标和性能压测，不再作为首要功能线。
7. **测试补齐方向**：优先补高价值边界和回归风险点，而不是继续堆同类协议测试；当前更值得覆盖 S3 服务端日志字段扩展、真实启用路径手动验收清单、TLS 配置、群权限边界、历史导出一致性和发布脚本。

## 说明

本项目主要用于学习和演示 Qt 桌面开发、TCP 网络通信和即时通讯系统设计。当前文件和媒体传输适合局域网测试，如用于生产环境，还需要继续增强安全性、稳定性、离线传输治理和传输性能。
