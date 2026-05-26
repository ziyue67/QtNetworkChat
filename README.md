# QtNetworkChat

[![Windows Build](https://github.com/ziyue67/QtNetworkChat/actions/workflows/windows-build.yml/badge.svg)](https://github.com/ziyue67/QtNetworkChat/actions/workflows/windows-build.yml)

QtNetworkChat 是一个基于 C++ 和 Qt Widgets 开发的 QQ 风格局域网即时通讯软件，支持账号注册登录、好友搜索与添加、群聊、私聊、文件发送、聊天记录、离线消息和系统托盘提醒。项目适合用于 Qt 网络编程、TCP 通信、桌面客户端开发和即时通讯原型学习。

## 功能特性

- QQ 风格登录和注册界面
- 随机 QQ 号注册
- 账号密码登录
- 密码 SHA-256 哈希保存
- SQLite 本地账号数据库
- 创建服务器和加入服务器
- TCP Socket 局域网通信
- JSON 消息协议
- 可选 Redis 在线状态服务，为高并发和多服务实例部署提供 presence 与在线列表共享基础
- 服务端可向 Redis Pub/Sub 发布本实例聊天事件，为跨实例消息路由打基础
- Redis Pub/Sub 已接入服务端远端聊天事件消费，按实例 ID 去重后转发给本实例在线用户
- 跨实例私聊收件人在线时不会在原服务实例重复写入离线队列
- 小文件/图片可通过 Redis Pub/Sub 路由到远端服务实例的在线收件人
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
- 文件和图片支持分片传输、进度提示、取消发送、ACK 已接收字节回传、服务端/客户端断点续传状态查询、发送端查询并校验续传状态后按最早缺失分片续传、跳过服务端已确认分片、发送端本地保存并通过菜单入口恢复/清除续传元数据、过期恢复状态自动清理、ACK 超时后自动查询续传状态、ACK 超时重试和完整性校验
- 取消文件/图片发送时会通知服务端清理未完成分片上传
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
- CTest 覆盖消息序列化、文件分片 ACK 接收进度、重复分片 ACK 去重、空分片、越界分片、分片数量不一致、非末尾分片大小和同传输元数据变更拒绝、乱序缺片保持未完成状态、服务端/客户端断点续传状态查询、查询续传状态后校验元数据并按最早缺失分片续传、跳过已确认分片并按实际 received chunks 汇总进度、服务端组包后 hash 不一致拒绝、发送端续传元数据持久化与恢复 helper、过期恢复状态清理、离线附件配额拒绝、离线附件成功回放后的队列和附件清理、离线附件缺失/大小/hash/chunkSize/chunkCount 异常提示和队列清理、离线附件队列持久化失败回滚、离线附件下发中断后队列和附件保留及重试成功清理、离线附件拒绝 ACK 和部分 ACK 后重试成功清理、部分 ACK 后记录 confirmedBytes/confirmedChunks/resumeUpdatedAt 并按 confirmedBytes 续发、confirmedChunks 为空时按有效 confirmedBytes 续发、过期或超过自定义 TTL 的 resumeUpdatedAt 回退完整回放、无效 confirmedBytes 回退完整回放、非法 confirmedChunks 回退 confirmedBytes 续发、confirmedBytes 与 confirmedChunks 冲突时按 confirmedChunks 最早缺口续发、confirmedChunks 非连续缺口从最早缺失分片续发、重复 confirmedChunks 去重后仍从最早缺口续发、confirmedChunks 覆盖全部分片时不再发送分片并直接清理、孤儿离线附件启动清理、仍被队列引用的离线附件启动保留、自定义 TTL 过期离线附件启动清理和后续缺失提示、ACK 超时后自动查询续传状态、分片 ACK 丢失后的发送端重试进度、文件取消清理、服务端群成员变更、群主自移除保护和公告权限协议
- CTest 覆盖 Redis RESP 命令编码、响应解析、presence 命令流、Pub/Sub 发布/订阅基础、断线重订阅、跨实例群聊/私聊/小文件/小图片路由、订阅侧超大文件/图片事件拒收、缺少 senderId、非聊天 eventType、空消息或未知类型的 Redis 事件拒收、大文件不经 Pub/Sub 并离线兜底、私聊文本和小文件发布失败离线兜底、Redis 不可用降级登录
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

当前 CTest 会执行构建产物冒烟测试，并覆盖消息序列化、服务端群成员变更、群主自移除保护、文件分片 ACK 接收进度、重复分片 ACK 去重、空分片、越界分片、分片数量不一致、非末尾分片大小和同传输元数据变更拒绝、乱序缺片保持未完成状态、服务端/客户端断点续传状态查询、发送端按确认位置续传、查询续传状态后校验元数据并按最早缺失分片续传、跳过已确认分片并按实际 received chunks 汇总进度、服务端组包后 hash 不一致拒绝、发送端续传元数据持久化与恢复 helper、过期恢复状态清理、离线附件配额拒绝、离线附件成功回放后的队列和附件清理、离线附件缺失/大小/hash/chunkSize/chunkCount 异常提示和队列清理、离线附件队列持久化失败回滚、离线附件下发中断后队列和附件保留及重试成功清理、离线附件拒绝 ACK 和部分 ACK 后重试成功清理、部分 ACK 后记录 confirmedBytes/confirmedChunks/resumeUpdatedAt 并按 confirmedBytes 续发、confirmedChunks 为空时按有效 confirmedBytes 续发、过期或超过自定义 TTL 的 resumeUpdatedAt 回退完整回放、无效 confirmedBytes 回退完整回放、非法 confirmedChunks 回退 confirmedBytes 续发、confirmedBytes 与 confirmedChunks 冲突时按 confirmedChunks 最早缺口续发、confirmedChunks 非连续缺口从最早缺失分片续发、重复 confirmedChunks 去重后仍从最早缺口续发、confirmedChunks 覆盖全部分片时不再发送分片并直接清理、孤儿离线附件启动清理、仍被队列引用的离线附件启动保留、自定义 TTL 过期离线附件启动清理和后续缺失提示、ACK 超时后自动查询续传状态、分片 ACK 丢失后的发送端重试进度、文件取消清理、Redis RESP 协议基础能力、presence 写入/读取命令流、Pub/Sub 发布/订阅基础、断线重订阅、跨实例群聊/私聊/小文件/小图片路由、订阅侧超大文件/图片事件拒收、缺少 senderId、非聊天 eventType、空消息或未知类型的 Redis 事件拒收、大文件不经 Pub/Sub 并离线兜底、私聊文本和小文件发布失败离线兜底、Redis 不可用时的登录降级。

### Windows 打包

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1 -BuildDir build-qt6-mingw
```

脚本会先构建项目，再把 `QtNetworkChat.exe` 和 `README.md` 收集到 `dist/QtNetworkChat-win-x64`。如果系统能找到 `windeployqt.exe`，会自动复制 Qt 运行库，并生成 `dist/QtNetworkChat-win-x64.zip`。

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
- `friends_<账号>.txt`：好友列表
- 聊天历史文件：群聊和私聊记录
- 离线消息文件：未在线用户的私聊消息与文件元数据
- 离线附件目录：未在线用户的文件和图片二进制数据，服务端会定期清理过期或无队列引用的附件

这些文件属于运行时数据，不建议提交到 GitHub。

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
- 后续优化重点不再是基础协议补洞，而是失败提示体验、跨实例大文件回放设计、治理指标和更细粒度重传策略。

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

启用后，服务端会在用户登录和心跳时写入 `qtchat:presence:<QQ号>`，并设置短 TTL，同时维护 `qtchat:presence:users` 在线索引；用户断开或服务端停止时会主动删除该在线状态。发送在线列表时，服务端会把本实例内存在线表与 Redis presence 合并，因此多个服务实例连接同一个 Redis 时可以共享在线用户视图。普通群聊和私聊消息完成本地投递后，会发布带 `instanceId` 的 `qtchat:pubsub:messages` 事件；服务端也会订阅该通道，跳过本实例事件，并把远端群聊/私聊转发给本实例在线用户。Redis 不可用时服务端会回退到原有内存在线表和本地转发，不影响局域网单机服务端运行。

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

1. **文件传输可靠性**：在线文件/图片已能在 ACK 超时后查询续传状态、跳过已确认分片并修正非连续确认进度；下一步更适合做失败分片更细粒度重传、跨连接持久化续传和更明确的普通发送失败提示。离线附件协议已具备缺口续发与治理基础，后续更适合补用户可见状态和失败原因。
2. **Redis 高并发与跨实例大文件设计**：当前 Pub/Sub 只承载文本和小文件/小图片，大文件会落源实例离线队列兜底。下一步应设计跨实例大文件/图片的流式路由或对象存储式中转，避免把大 payload 放进 Pub/Sub。
3. **安全增强**：TLS 已有可选入口，但还缺证书链校验体验、指纹固定配置；账号密码仍是简单 SHA-256 派生，建议升级为带盐 KDF，再评估端到端加密。
4. **群组和权限边界**：服务端群组模型已覆盖核心成员变更、公告权限和群主自移除保护，后续可补更多边界测试，例如重复邀请、管理员角色、私有群、群文件权限和被移出后的历史可见性。
5. **结构拆分**：`mainwindow.cpp` 已承载聊天、好友、群组、文件、历史和恢复入口，后续应小步抽出 TransferManager、FriendManager、GroupManager、HistoryService、Storage，降低 UI 层复杂度。
6. **发布与运维体验**：补版本号注入、Release 自动上传、安装包、运行时依赖校验、崩溃日志和可选诊断日志，方便非开发环境使用。
7. **测试补齐方向**：优先补高价值边界和回归风险点，而不是继续堆同类协议测试；当前更值得覆盖 TLS 配置、密码迁移、群权限边界、历史导出一致性和发布脚本。

## 说明

本项目主要用于学习和演示 Qt 桌面开发、TCP 网络通信和即时通讯系统设计。当前文件和媒体传输适合局域网测试，如用于生产环境，还需要继续增强安全性、稳定性、离线传输治理和传输性能。
