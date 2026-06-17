
# QQ NT 重设计：整体实施计划（修订版）

## 1. 目标与边界

- 把 `D:\C++VS pro\QtNetworkChat` 完全按 **QQ NT 新版桌面客户端** 视觉与交互重设计。
- **前端由 Kimi 负责**，**后端由 GPT5.5 负责**。
- 架构：**Tauri v2 + React/TypeScript** 前端；**Rust 主进程** 作为桥接；**C++ 无头 sidecar** 执行真实网络与业务逻辑；**Redis 仅由 QQNTServer 连接**。
- 现有 TCP 协议与 `Client`/`Server` 保持向后兼容。
- 核心功能必须真实跑后端：登录、联系人、单聊、群聊、文件（含进度/断点续传）、E2E 状态。
- 扩展入口可先 mock：空间、频道、邮件、文档、日历、会议、收藏、钱包。

---

## 2. 总体架构

```
前端 React
  ↓ invoke / listen
Tauri Rust (commands.rs / bridge.rs / sidecar.rs)
  ↓ NDJSON stdin/stdout          ↓ optional spawn
QQNTEngine.exe (Client)     QQNTServer.exe (Server)
  ↓ TCP                            ↓ Redis RESP/PubSub
QQNTServer                        Redis
```

---

## 3. 仓库目录结构

```
D:\C++VS pro\QtNetworkChat\
├── CMakeLists.txt
├── QtNetworkChat.pro
├── scripts/
│   └── copy-sidecars.ps1            # C++ 编译后拷 sidecar 到 Tauri
├── dev/
│   └── redis-compose.yml            # 开发 Redis
├── docs/
│   └── qqnt-ipcv1.md                # IPC v1 协议文档（单一事实来源）
├── include/
│   ├── qqnt_client_bridge.h
│   ├── qqnt_engine_command_router.h
│   ├── qqnt_redis_service.h
│   └── qqnt_server_controller.h
├── src/
│   ├── qqnt_client_bridge.cpp
│   ├── qqnt_engine_command_router.cpp
│   ├── qqnt_redis_service.cpp
│   └── qqnt_server_controller.cpp
├── tools/
│   ├── qqnt_engine.cpp
│   └── qqnt_server.cpp
├── tests/
│   ├── qqnt_engine_smoke_test.cpp
│   ├── qqnt_server_redis_test.cpp
│   ├── qqnt_protocol_drift_test.cpp
│   └── fixtures/
│       └── ready.json
├── tauri-qqnt/
│   ├── package.json
│   ├── vite.config.ts
│   ├── tailwind.config.js
│   ├── tsconfig.json
│   ├── index.html
│   ├── src/
│   │   ├── main.tsx
│   │   ├── App.tsx
│   │   ├── styles/
│   │   │   ├── index.css
│   │   │   └── theme.css
│   │   ├── types/
│   │   │   └── qqnt.ts
│   │   ├── api/
│   │   │   └── qqnt.ts
│   │   ├── hooks/
│   │   │   ├── useEngine.ts
│   │   │   ├── useQQNTEvents.ts
│   │   │   └── useTheme.ts
│   │   ├── stores/
│   │   │   ├── authStore.ts
│   │   │   ├── contactStore.ts
│   │   │   ├── sessionStore.ts
│   │   │   ├── messageStore.ts
│   │   │   ├── fileStore.ts
│   │   │   └── uiStore.ts
│   │   ├── components/
│   │   │   ├── frame/
│   │   │   │   ├── TitleBar.tsx
│   │   │   │   └── WindowControls.tsx
│   │   │   ├── sidebar/
│   │   │   │   ├── AppNav.tsx
│   │   │   │   └── NavItem.tsx
│   │   │   ├── session/
│   │   │   │   ├── SessionList.tsx
│   │   │   │   ├── SessionItem.tsx
│   │   │   │   └── SearchBar.tsx
│   │   │   ├── chat/
│   │   │   │   ├── ChatPanel.tsx
│   │   │   │   ├── MessageList.tsx
│   │   │   │   ├── MessageBubble.tsx
│   │   │   │   ├── Composer.tsx
│   │   │   │   └── FileMessage.tsx
│   │   │   ├── contact/
│   │   │   │   ├── ContactList.tsx
│   │   │   │   ├── ContactCard.tsx
│   │   │   │   ├── AddFriendModal.tsx
│   │   │   │   └── CreateGroupModal.tsx
│   │   │   └── common/
│   │   │       ├── Avatar.tsx
│   │   │       ├── Badge.tsx
│   │   │       └── Modal.tsx
│   │   └── views/
│   │       ├── LoginView.tsx
│   │       ├── MainLayout.tsx
│   │       ├── MessageView.tsx
│   │       ├── ContactsView.tsx
│   │       ├── SettingsView.tsx
│   │       ├── ProfileView.tsx
│   │       ├── SpaceView.tsx
│   │       ├── ChannelView.tsx
│   │       ├── MailView.tsx
│   │       ├── DocsView.tsx
│   │       ├── CalendarView.tsx
│   │       ├── MeetingView.tsx
│   │       ├── FavoritesView.tsx
│   │       └── WalletView.tsx
│   └── src-tauri/
│       ├── Cargo.toml
│       ├── build.rs
│       ├── tauri.conf.json
│       ├── capabilities/default.json
│       └── src/
│           ├── main.rs
│           ├── lib.rs
│           ├── commands.rs
│           ├── bridge.rs
│           ├── sidecar.rs
│           ├── state.rs
│           └── error.rs
```

---

## 4. 技术栈

| 层 | 技术 |
|---|---|
| 前端 | React 18 + TypeScript + Vite |
| 样式 | Tailwind CSS + CSS Variables |
| 状态 | Zustand |
| 路由 | React Router |
| 主进程 | Tauri v2 + Rust |
| 客户端引擎 | C++17 + Qt 6.8.3（无头，`QCoreApplication`） |
| 服务端 | C++17 + Qt 6.8.3（无头，`QCoreApplication`，连 Redis） |
| 缓存/路由 | Redis 7 |

---

## 5. 后端详细设计

### 5.1 CMake 改造

新增 `QQNTClientCore` OBJECT library，engine 与 server 各自复用。

```cmake
add_library(QQNTClientCore OBJECT ${client_core_sources})
target_link_libraries(QQNTClientCore PUBLIC
    Qt6::Core Qt6::Network Qt6::Sql Qt6::Multimedia
)

add_executable(QQNTEngine
    tools/qqnt_engine.cpp
    src/qqnt_client_bridge.cpp
    src/qqnt_engine_command_router.cpp
    $<TARGET_OBJECTS:QQNTClientCore>
)

add_executable(QQNTServer
    tools/qqnt_server.cpp
    src/server.cpp
    src/redisclient.cpp
    src/objectstore.cpp
    src/heartbeatmonitor.cpp
    src/qqnt_redis_service.cpp
    src/qqnt_server_controller.cpp
)
```

CMake POST_BUILD 自动调用 `scripts/copy-sidecars.ps1`，把 exe 拷到 Tauri external bin 目录。

### 5.2 `QQNTEngine` 无头引擎

入口 `tools/qqnt_engine.cpp`：

```cpp
#include <QtGlobal>

static void qqntMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    const QByteArray line = qFormatLogMessage(type, ctx, msg).toLocal8Bit();
    fwrite(line.constData(), 1, line.size(), stderr);
    fputc('\n', stderr);
    fflush(stderr);
}

int main(int argc, char* argv[]) {
    qInstallMessageHandler(qqntMessageHandler);
    QCoreApplication app(argc, argv);
    QQNTClientBridge bridge;
    if (!bridge.initialize()) return 1;
    return app.exec();
}
```

`QQNTClientBridge`：
- 从 `stdin` 读 NDJSON。
- 调用 `QQNTEngineCommandRouter` 分发到 `Client`。
- 监听 `Client` 所有 signals，输出 NDJSON event 到 `stdout`。
- 日志通过全局 handler 全部走 `stderr`，`stdout` 严禁出现非 NDJSON。

### 5.3 NDJSON 协议正式 Schema

**输入命令：**
```json
{
  "op": "send_private_message",
  "reqId": "uuid",
  "payload": { "receiverId": "10001", "content": "hello" }
}
```

**输出包络：**
```json
{
  "type": "event" | "ack",
  "op": "send_private_message",
  "event": "...",
  "reqId": "uuid",
  "status": "ok" | "error",
  "payload": { ... }
}
```

**协议版本协商**：`ready` 事件与 `ack` 中必须包含：
```json
{ "protocolVersion": 1, "version": "...", "qtVersion": "..." }
```
前端与 Rust 维护 `EXPECTED_PROTOCOL_VERSION`常量，版本不一致时提示升级。

### 5.4 命令路由表 V1

| op | payload | 说明 |
|---|---|---|
| `ready` | — | 返回协议版本、Qt 版本、E2E 状态 |
| `connect` | `{host, port}` | TCP 连接服务端 |
| `disconnect` | — | 断开 |
| `login` | `{account, password}` | 账号密码登录 |
| `register` | `{account, password, userName}` | 注册 |
| `logout` | — | 清空状态并断开 |
| `set_user_info` | `{userId, userName}` | 设置当前用户信息 |
| `get_user_list` | — | 在线用户 |
| `get_friend_list` | — | 好友列表 |
| `get_group_list` | — | 群组列表 |
| `search_friend` | `{account}` | 搜 QQ 号 |
| `send_friend_request` | `{receiverId}` | 加好友 |
| `respond_friend_request` | `{senderId, accepted}` | 接受/拒绝 |
| `send_private_message` | `{receiverId, content}` | 私聊 |
| `send_group_message` | `{groupId, content}` | 群聊 |
| `create_group` | `{groupName, members?, announcement?}` | 建私有群；初始成员账号存在时随群创建加入 |
| `update_group_announcement` | `{groupId, announcement}` | 改公告 |
| `update_group_member` | `{groupId, memberId, action}` | 成员管理 |
| `send_file` | `{receiverId xor groupId, filePath}` | 发文件；目标必须二选一 |
| `send_image` | `{receiverId xor groupId, filePath}` | 发图片；目标必须二选一 |
| `cancel_transfer` | `{transferId}` | 取消当前活动发送；`transferId` 必须匹配当前传输，成功回显 `{cancelled, transferId}` |
| `query_resume` | `{transferId, filePath?, receiverId xor groupId?, contentType?}` | 续传查询；带 `filePath` 时恢复发送，恢复发送目标必须二选一 |
| `e2e_status` | `{peerId?}` | E2E 状态；省略时返回本机身份，提供时追加会话与对端身份 |
| `e2e_announce_identity` | `{peerId}` | 身份公告 |
| `e2e_pin_identity` | `{peerId, fingerprint?}` | 固定身份 |
| `e2e_request_rotation` | `{peerId}` | 请求轮换 |
| `profile_update` | `{userName?, avatarBase64?}` | 更新资料 |
| `settings_sync` | `{settings}` | 本地设置 |

### 5.5 主动事件表 V1

| event | payload | 触发源 |
|---|---|---|
| `ready` | `{protocolVersion, version, qtVersion, e2eStatus}` | 初始化完成 |
| `connection_state` | `{connected, host, port}` | TCP 状态 |
| `login_result` | `{success, userId, userName, error?}` | 登录结果 |
| `user_list` | `{users[]}` | 在线列表 |
| `user_joined` / `user_left` | `{userId, userName}` | 上下线 |
| `friend_list` | `{friends[]}` | 好友列表快照 |
| `friend_event` | `{type, senderId, senderName, accepted?}` | 好友申请/回应 |
| `friend_search_result` | `{found, userId, userName, online}` | 搜索结果 |
| `message` | `{sessionId, message}` | 新消息 |
| `group_snapshot` | `{groups[]}` | 群快照 |
| `group_member_updated` | `{groupId, memberId, action}` | 群成员变更 |
| `file_progress` | `{transferId, fileName, bytes, total, direction}` | 文件进度 |
| `file_done` | `{transferId, fileName, filePath, direction}` | 文件完成 |
| `file_error` | `{transferId, reason}` | 文件失败 |
| `e2e_session_state` | `{peerId, rotationRequired}` | E2E 会话 |
| `e2e_identity_state` | `{peerId, trusted, fingerprint}` | E2E 身份 |
| `settings_synced` | `{accepted, revision, settings, appliedDownloadDir?}` | 设置同步；文件下载目录可影响 engine 接收文件保存位置 |
| `notification` | `{title, body}` | 需要前端通知 |
| `error` | `{message, source}` | 通用错误 |

### 5.6 `QQNTServer` 与 Redis 服务封装

入口 `tools/qqnt_server.cpp` 使用 `QCoreApplication`，由 `QQNTServerController` 拉起 `Server`。

`QQNTRedisService` 把现有 `Server` 中零散的 Redis presence/pubsub/offline-queue 逻辑抽出来：

```cpp
class QQNTRedisService : public QObject {
    Q_OBJECT
public:
    explicit QQNTRedisService(QObject* parent = nullptr);
    bool initialize(QString* error);
    bool isReady() const;

    bool setPresence(const ChatUser& user, const QString& instanceId);
    bool clearPresence(const QString& userId, const QString& instanceId);
    bool isUserOnline(const QString& userId, bool* online);

    bool publishMessage(const Message& msg, const QString& deliveryState);
    bool publishE2EControl(const QJsonObject& event);
    bool publishFileOffer(const QJsonObject& offer);
    bool publishFileClaim(const QJsonObject& offer);
    bool publishFileDelivered(const QJsonObject& offer, qint64 confirmedBytes);
    bool publishFileFailed(const QJsonObject& offer, const QString& reason);

    bool enqueueOfflineMessage(const Message& msg);
    bool enqueueOfflineFileOffer(const QJsonObject& offer);
    bool fetchOfflineMessages(const QString& userId, QList<Message>* out);
    bool fetchOfflineFileOffers(const QString& userId, QList<QJsonObject>* out);

signals:
    void messageRouted(const QByteArray& payload);
    void e2eControlRouted(const QJsonObject& event);
    void fileOfferRouted(const QJsonObject& offer);
    void fileDeliveredRouted(const QJsonObject& event);
    void fileFailedRouted(const QJsonObject& event);
};
```

`Server` 内部所有 Redis 调用改由该 service 代理。

### 5.7 Redis Key Schema

统一前缀 `qqnt:`，可通过 `QTNETWORKCHAT_REDIS_PREFIX` 覆盖。

| Key / Channel | 类型 | 说明 |
|---|---|---|
| `qqnt:presence:<userId>` | Hash | 在线状态，TTL 90s |
| `qqnt:presence:index` | Set | 在线 userId 集合 |
| `qqnt:device:<userId>:<instanceId>` | Hash | 设备元数据，TTL 90s |
| `qqnt:contacts:<userId>` | Sorted Set | 好友列表 |
| `qqnt:groups:<userId>` | Set | 加入的群 |
| `qqnt:unread:<userId>:<sessionId>` | String | 未读数 |
| `qqnt:msg:seq:<sessionId>` | String | 消息自增 ID |
| `qqnt:offline:msg:<userId>` | List | 离线消息 |
| `qqnt:offline:file:<userId>` | List | 离线文件 offer |
| `qqnt:server:instances` | Set | 在线实例 ID |
| `qqnt:routing:msg` | Pub/Sub | 消息路由 |
| `qqnt:routing:presence` | Pub/Sub | 上下线广播 |
| `qqnt:routing:file` | Pub/Sub | 文件路由 |
| `qqnt:routing:e2e` | Pub/Sub | E2E 控制面 |
| `qqnt:rate:<action>:<userId>` | String | 限流计数 |

---

## 6. Rust Tauri 主进程详细设计

### 6.1 crate 模块

- `main.rs`：应用入口；注册 command；启动引擎 sidecar。
- `lib.rs`：导出各模块。
- `commands.rs`：`qqnt_command` 通用命令 + 类型化 wrapper。
- `bridge.rs`：NDJSON 读写；请求-响应关联；事件广播。
- `sidecar.rs`：spawn/monitor `QQNTEngine` 与可选 `QQNTServer`。
- `state.rs`：`AppState` 与 `EngineState`。
- `error.rs`：统一错误类型。

### 6.2 sidecar 启动与 Redis 预检

`sidecar.rs` 在托管模式下启动 `QQNTServer` 前，先 ping Redis：

```rust
pub async fn start_server(state: &AppState, app: &AppHandle) -> Result<(), String> {
    let host = env::var("QTNETWORKCHAT_REDIS_HOST").unwrap_or_else(|_| "127.0.0.1".into());
    let port = env::var("QTNETWORKCHAT_REDIS_PORT").unwrap_or_else(|_| "6379".into()).parse::<u16>().unwrap_or(6379);

    if tokio::net::TcpStream::connect((host.as_str(), port)).await.is_err() {
        app.emit("qqnt://server/fatal", json!({"reason":"redis_unavailable"})).ok();
        return Err("Redis 未就绪".into());
    }

    let (mut rx, child) = tauri::api::process::Command::new_sidecar("QQNTServer")
        .map_err(|e| e.to_string())?
        .spawn()
        .map_err(|e| e.to_string())?;
    // ...
}
```

`QQNTEngine` 启动统一用 `Command::new_sidecar("QQNTEngine")`，Tauri 自动处理 dev/build 路径差异。

### 6.3 环境变量透传

Rust 给 `QQNTServer` 进程设置：
- `QTNETWORKCHAT_REDIS=1`
- `QTNETWORKCHAT_REDIS_HOST/PORT/PASSWORD/PREFIX`
- `QTNETWORKCHAT_TLS`、`QTNETWORKCHAT_TLS_CERT`、`QTNETWORKCHAT_TLS_KEY`、`QTNETWORKCHAT_TLS_VERIFY`
- `QTNETWORKCHAT_APPDATA_DIR`（可选）

### 6.4 bridge 请求-响应关联

```rust
pub async fn call_engine(state: &AppState, cmd: Value) -> Result<Value, String> {
    let req_id = cmd["reqId"].as_str().unwrap_or("").to_string();
    let (tx, rx) = oneshot::channel();

    {
        let mut pending = state.engine.pending.lock().await;
        pending.insert(req_id, tx);
    }

    write_stdin(state, serde_json::to_string(&cmd).unwrap()).await?;
    let resp = tokio::time::timeout(Duration::from_secs(30), rx).await
        .map_err(|_| "engine timeout")?
        .map_err(|_| "engine channel closed")?;
    Ok(resp)
}
```

### 6.5 Tauri 事件命名

统一前缀 `qqnt://engine/` 与 `qqnt://server/`：
- `qqnt://engine/ready`
- `qqnt://engine/connection_state`
- `qqnt://engine/login_result`
- `qqnt://engine/message`
- `qqnt://engine/file_progress`
- `qqnt://engine/error`
- `qqnt://server/fatal`

---

## 7. 前端详细设计

### 7.1 依赖

```json
{
  "dependencies": {
    "react": "^18.3.0",
    "react-dom": "^18.3.0",
    "react-router-dom": "^6.23.0",
    "zustand": "^4.5.0",
    "@tauri-apps/api": "^2.0.0",
    "@tauri-apps/plugin-shell": "^2.0.0",
    "date-fns": "^3.6.0",
    "react-window": "^1.8.10",
    "clsx": "^2.1.0"
  },
  "devDependencies": {
    "typescript": "^5.4.0",
    "vite": "^5.2.0",
    "@vitejs/plugin-react": "^4.2.0",
    "tailwindcss": "^3.4.0",
    "vitest": "^1.6.0",
    "@testing-library/react": "^15.0.0",
    "jsdom": "^24.0.0"
  }
}
```

### 7.2 类型定义

```ts
export type QQNTMessageStatus = 'sending' | 'sent' | 'failed' | 'received';

export interface QQNTMessage {
  messageId: string;
  clientMessageId?: string;
  sessionId: string;
  senderId: string;
  senderName: string;
  senderAvatar?: string;
  timestamp: number;
  contentType: 'text' | 'image' | 'file' | 'system';
  content?: string;
  fileName?: string;
  fileSize?: number;
  transferId?: string;
  transferProgress?: number;
  status: QQNTMessageStatus;
}

export interface QQNTSession {
  sessionId: string;
  type: 'private' | 'group';
  targetId: string;
  targetName: string;
  avatar?: string;
  unread: number;
  lastMessage?: {
    contentType: 'text' | 'image' | 'file' | 'system';
    preview: string;
    timestamp: number;
  };
  updatedAt: number;
  pinned: boolean;
}

export interface QQNTContact {
  userId: string;
  userName: string;
  avatar?: string;
  online: boolean;
  relation: 'friend' | 'stranger' | 'pending_in' | 'pending_out';
  signature?: string;
}

export interface QQNTGroup {
  groupId: string;
  groupName: string;
  announcement?: string;
  type: 'public' | 'private';
  memberCount: number;
  role?: 'owner' | 'admin' | 'member';
}

export interface QQNTFileTransfer {
  transferId: string;
  sessionId: string;
  fileName: string;
  totalBytes: number;
  bytes: number;
  direction: 'up' | 'down';
  status: 'pending' | 'running' | 'paused' | 'done' | 'error';
}

export interface QQNTAppEntry {
  id: string;
  icon: string;
  label: string;
  path: string;
  mock: boolean;
}
```

### 7.3 Store 职责

- `authStore`：当前用户、登录态、engine ready、连接状态、托管/加入模式、服务器地址。
- `contactStore`：好友、群、好友申请、在线状态。
- `sessionStore`：会话列表、选中会话、未读、排序、最后消息预览。
- `messageStore`：按 sessionId 分组的消息、乐观发送、状态更新。
- `fileStore`：当前文件传输进度。
- `uiStore`：侧边栏展开、当前导航、弹窗、主题。

### 7.4 IPC 接入

`useQQNTEvents.ts`：

```ts
useEffect(() => {
  const unlisten = listen('qqnt://engine/message', (e) => {
    messageStore.handleIncoming(e.payload);
  });
  return () => { unlisten.then(f => f()); };
}, []);
```

`api/qqnt.ts`：

```ts
export async function sendPrivateMessage(receiverId: string, content: string) {
  return invoke('qqnt_command', {
    payload: {
      op: 'send_private_message',
      reqId: crypto.randomUUID(),
      payload: { receiverId, content }
    }
  });
}
```

### 7.5 导航入口配置

```ts
export const APP_ENTRIES: QQNTAppEntry[] = [
  { id: 'messages', icon: 'MessageIcon', label: '消息', path: '/', mock: false },
  { id: 'contacts', icon: 'ContactsIcon', label: '联系人', path: '/contacts', mock: false },
  { id: 'space', icon: 'SpaceIcon', label: '动态', path: '/space', mock: true },
  { id: 'channel', icon: 'ChannelIcon', label: '频道', path: '/channel', mock: true },
  { id: 'mail', icon: 'MailIcon', label: '邮件', path: '/mail', mock: true },
  { id: 'docs', icon: 'DocsIcon', label: '文档', path: '/docs', mock: true },
  { id: 'calendar', icon: 'CalendarIcon', label: '日历', path: '/calendar', mock: true },
  { id: 'meeting', icon: 'MeetingIcon', label: '会议', path: '/meeting', mock: true },
  { id: 'favorites', icon: 'FavoritesIcon', label: '收藏', path: '/favorites', mock: true },
  { id: 'wallet', icon: 'WalletIcon', label: '钱包', path: '/wallet', mock: true },
  { id: 'settings', icon: 'SettingsIcon', label: '设置', path: '/settings', mock: false },
];
```

`mock=true` 的视图统一使用 `<MockPlaceholder />`。

---

## 8. 构建、运行与打包

### 8.1 C++ 编译

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/mingw_64"
cmake --build build --target QQNTEngine QQNTServer
```

### 8.2 sidecar 同步脚本

`scripts/copy-sidecars.ps1`：

```powershell
$triplet = "x86_64-pc-windows-msvc"
$outDir = "tauri-qqnt/src-tauri/binaries"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Copy-Item -Path "build/QQNTEngine.exe" -Destination "$outDir/QQNTEngine-$triplet.exe" -Force
Copy-Item -Path "build/QQNTServer.exe" -Destination "$outDir/QQNTServer-$triplet.exe" -Force
```

CMake POST_BUILD 接入：

```cmake
add_custom_command(TARGET QQNTEngine POST_BUILD
    COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/copy-sidecars.ps1"
)
```

### 8.3 Redis 开发环境

```yaml
# dev/redis-compose.yml
services:
  redis:
    image: redis:7-alpine
    ports:
      - "6379:6379"
    volumes:
      - redis-data:/data
volumes:
  redis-data:
```

```powershell
docker compose -f dev/redis-compose.yml up -d
```

### 8.4 Tauri 开发

```powershell
cd tauri-qqnt
npm install
npm run tauri dev
```

### 8.5 打包

```powershell
cd tauri-qqnt
npm run tauri build
```

---

## 9. 测试矩阵

| 层 | 文件 | 验证点 |
|---|---|---|
| Engine stdout 与命令契约 | `tests/qqnt_engine_smoke_test.cpp` + `tests/fixtures/protocol_contract.json` | 每行 stdout 都是合法 JSON；fixture 中每个命令都会被 `QQNTEngine` 路由且不会返回 `unknown_op` |
| Engine ready/connect | 同上 | `ready` ack 含 `protocolVersion`；`connect` ack 正确 |
| Server Redis 就绪 | `tests/qqnt_server_redis_test.cpp` | `isServiceReady=true` |
| 跨实例消息路由 | 同上 | A 实例发布，B 实例通过 Redis 收到 |
| 跨实例群快照刷新 | 同上 | 建私有群时远端在线初始成员通过 Redis 收到群快照 |
| 跨实例私有群消息 | 同上 | 远端在线私有群成员收到群消息，同实例非成员不收到 |
| 跨实例私有群小文件 | 同上 | 远端在线私有群成员收到 Redis Pub/Sub 可承载的小文件分片，同实例非成员不收到 |
| 跨实例私有群大文件 | 同上 | 远端在线私有群成员收到 object-store large_file_offer 分片，同实例非成员不收到 |
| 协议漂移 | `tests/qqnt_protocol_drift_test.cpp` + `tests/fixtures/ready.json` + `tests/fixtures/protocol_contract.json` | 三端能解析同一份 fixture；命令/事件清单和命令/事件 payload 形状与 IPC 文档一致 |
| Rust Bridge mock | `tests/qqnt_bridge_rust_test.rs` | reqId 关联与事件广播 |
| 前端 store | `src/stores/*.test.ts` | 会话排序、未读、乐观发送 |
| 前端组件 | `src/components/**/*.test.tsx` | MessageBubble、Composer、SessionList |
| 回归 | 现有 CTest | `ctest --test-dir build --output-on-failure` 全过 |

---

## 10. 风险与缓解（已落地到任务）

| 风险 | 缓解措施 | 对应任务 |
|---|---|---|
| `QQNTClientCore` 依赖 Widgets 导致无头启动失败 | 先不链 Widgets；必要时加 `QQNT_HEADLESS` 宏排除 GUI 代码；smoke test 验证无窗口 | Phase 1 任务 1.3、1.4 |
| Sidecar 路径 dev/build 不一致 | `tauri.conf.json` 注册 `externalBin`；CMake POST_BUILD 执行 `scripts/copy-sidecars.ps1`；Rust 用 `Command::new_sidecar` | Phase 1 任务 1.2、1.2a |
| Redis 未启动时 `QQNTServer` 拒绝启动 | `dev/redis-compose.yml`；Rust ping Redis 失败 emit `qqnt://server/fatal`；前端提示 | Phase 1 任务 1.1、1.2；Phase 3 任务 3.3 |
| Engine stdout 被日志污染 | `qInstallMessageHandler` 重定向到 `stderr`；bridge 只写 NDJSON 到 `stdout` | Phase 1 任务 1.4 |
| 前端 TS 类型与 C++/Rust 不同步 | `protocolVersion` 协商；单一 `docs/qqnt-ipcv1.md`；共享 fixture 三端漂移测试 | Phase 1 任务 1.4、1.5a；Phase 8 任务 8.1 |

---

## 11. WBS 与里程碑

### Phase 1：骨架

| 编号 | 任务 | 负责人 | 验收 |
|---|---|---|---|
| 1.1 | 安装 Rust + Tauri CLI + WebView2；安装/验证 Docker + Redis compose | GPT5.5 | `cargo tauri --version`；`docker compose -f dev/redis-compose.yml up -d` 成功 |
| 1.2 | 初始化 `tauri-qqnt`；配置 `tauri.conf.json` 与 `capabilities/default.json` | GPT5.5 | `npm run tauri dev` 打开空窗口 |
| 1.2a | 编写 `scripts/copy-sidecars.ps1` 并接入 CMake POST_BUILD | GPT5.5 | C++ 编译后 `tauri-qqnt/src-tauri/binaries/` 出现带 triplet 的 exe |
| 1.3 | CMake 新增 `QQNTEngine` 与 `QQNTServer` target；`QQNTClientCore` OBJECT library | GPT5.5 | `cmake --build build --target QQNTEngine QQNTServer` 成功 |
| 1.4 | 实现 `QQNTEngine` ready + connect；安装 `qInstallMessageHandler` 保证 stdout 纯净；`ready` ack 带 `protocolVersion` | GPT5.5 | CTest `qqnt_engine_smoke_test` 通过；stdout 每行合法 JSON |
| 1.5 | Rust `bridge.rs`/`sidecar.rs` spawn engine + NDJSON 读写 + 事件广播 | GPT5.5 | 测试能收到 `qqnt://engine/ready` |
| 1.5a | 创建 `docs/qqnt-ipcv1.md` 与 `tests/fixtures/ready.json` | GPT5.5 | 文档与 fixture 齐全 |

### Phase 2：前端框架

| 编号 | 任务 | 负责人 | 验收 |
|---|---|---|---|
| 2.1 | Vite + React + TypeScript + Tailwind + Router + Zustand 脚手架 | Kimi | `npm run dev` 成功 |
| 2.2 | 无边框窗口 + `TitleBar` + `WindowControls` | Kimi | 可拖动、最小化、最大化、关闭 |
| 2.3 | 11 个导航入口 + 路由 + `MockPlaceholder` | Kimi | 所有入口可切换 |
| 2.4 | 主题色板 + CSS Variables | Kimi | 明暗色切换生效 |
| 2.5 | 初始化 Zustand stores | Kimi | 组件能读写状态 |

### Phase 3：账号与连接

| 编号 | 任务 | 负责人 | 验收 |
|---|---|---|---|
| 3.1 | Engine 实现 login/register 命令与事件 | GPT5.5 | Rust 调用返回正确 userId |
| 3.2 | 前端 `LoginView` + 服务器配置 UI | Kimi | 能输入 host/port/account/password |
| 3.3 | Rust Redis 预检 + 前端错误提示（含 `qqnt://server/fatal`） | GPT5.5 & Kimi | Redis 未启动时前端明确提示 |
| 3.4 | Rust 类型化 command wrapper：`connect_server`、`login` | GPT5.5 | 前端 `api/qqnt.ts` 可用 |

### Phase 4：私聊消息

| 编号 | 任务 | 负责人 | 验收 |
|---|---|---|---|
| 4.1 | Engine 私聊发送/接收 + `message` 事件 | GPT5.5 | 两端实时显示 |
| 4.2 | 前端 `SessionList` + `ChatPanel` + `MessageBubble` | Kimi | 消息正确渲染、会话置顶、未读 +1 |
| 4.3 | 消息虚拟滚动 | Kimi | 长历史不卡顿 |
| 4.4 | 消息状态（sending/sent/failed） | Kimi | 发送失败可重试 |

### Phase 5：联系人/群聊

| 编号 | 任务 | 负责人 | 验收 |
|---|---|---|---|
| 5.1 | Engine 好友列表/在线状态/搜索/申请 | GPT5.5 | 命令与事件正确 |
| 5.2 | 前端 `ContactsView` + `ContactCard` + `AddFriendModal` | Kimi | 在线状态正确，可加好友 |
| 5.3 | Engine 群列表/建群/群消息/群成员管理 | GPT5.5 | 群聊消息同步 |
| 5.4 | 前端群聊列表与群成员面板 | Kimi | 群消息收发正常 |

> 后端已支持 `create_group.members[]` 作为可选初始成员账号数组；服务端建私有群时会把已存在账号加入群，向本实例在线成员直接推送 `server_group_snapshot`，并通过 Redis 内部刷新事件通知其他实例上的在线初始成员。
> 私有群文本消息已接入 Redis 跨实例路由；远端实例会按 SQLite 群成员关系只推送给本实例在线成员，非成员不会收到。
> 私有群小文件/图片已接入 Redis 跨实例路由；远端实例会按群成员关系把 Pub/Sub 负载分片发送给本实例在线成员，非成员不会收到。
> 私有群大文件/图片已接入 Redis 对象路由；发送实例写入 object store 并发布 `large_file_offer`，远端实例按群成员关系投递给本实例在线成员，群 fanout 不发送一对一 delivered 清理回执，对象保留到 TTL 清理。

### Phase 6：文件传输

| 编号 | 任务 | 负责人 | 验收 |
|---|---|---|---|
| 6.1 | Engine 文件/图片发送 + 进度事件 | GPT5.5 | 进度条更新 |
| 6.2 | 前端 `FileMessage` + 下载/打开目录 | Kimi | 文件可接收 |
| 6.3 | 取消传输 | Kimi/GPT5.5 | 前端传入当前 `transferId` 后，Engine 校验并停止当前发送任务 |
| 6.4 | 断点续传查询与恢复 | GPT5.5 | `query_resume` 可只查状态，也可带 `filePath` 和 `receiverId`/`groupId` 二选一目标按续传状态继续发送 |

> 后端已支持 `send_file`/`send_image`、`file_progress`/`file_done`/`file_error` 事件、`query_resume` 查询与恢复；文件/图片发送和续传恢复目标按 `receiverId`/`groupId` 二选一校验，群文件续传会保留 `groupId`；`cancel_transfer` 按当前活动 `transferId` 校验，缺失、无活动或不匹配时返回错误 ack。

### Phase 7：设置与扩展

| 编号 | 任务 | 负责人 | 验收 |
|---|---|---|---|
| 7.1 | 前端 `SettingsView`（通用/账号/通知/文件/网络/E2E/关于） | Kimi | 配置保存并影响 engine |
| 7.2 | 个人资料编辑页 | Kimi | 昵称头像可修改 |
| 7.3 | 8 个扩展页 mock 占位 | Kimi | 入口与 UI 完整 |

> 后端已支持 `settings_sync` 校验、最近设置快照、`settings_synced` 事件，以及文件下载目录设置应用；前端可通过 `settings.files.downloadDir`、`settings.files.downloadDirectory` 或 `settings.fileDownloadDir` 影响 engine 接收文件保存位置。

### Phase 8：收尾

| 编号 | 任务 | 负责人 | 验收 |
|---|---|---|---|
| 8.1 | 全量 CTest + Vitest + protocol drift 测试 | GPT5.5 & Kimi | 全部通过 |
| 8.2 | Tauri `npm run tauri build` 出 MSI/NSIS | GPT5.5 | 安装包可安装运行 |
| 8.3 | 更新 README、IPC 文档 | Kimi | 新构建与运行方式说明 |
| 8.4 | 清理运行时数据提交；`.gitignore` 检查 | 共同 | 无 accounts.sqlite3、histories、离线附件入仓 |

> 后端 Phase 8 验证已覆盖后端 CTest 子集、`tauri-qqnt/src-tauri` 的 `cargo fmt --check` / `cargo test`、以及 `npm run tauri build` 打包；Vitest 渲染与 `*.test.ts(x)` 用例归 `codex/qqnt-frontend`，后端分支不补前端测试桩。

### 立即并行启动
- **Kimi**：Phase 2（前端脚手架 + 无边框窗口 + 11 入口）。
- **GPT5.5**：Phase 1（Tauri 壳 + `QQNTEngine` ready/connect + sidecar 同步 + stdout 纯净）。

