# QQ NT IPC v1 协议

本文档是 React 前端、Tauri Rust 主进程、C++ QQNTEngine sidecar 之间的协议单一事实来源。所有新增命令、事件、字段都先更新本文档，再同步实现。

## 1. 版本与传输

- 当前协议版本：`1`。
- 传输格式：`stdin`/`stdout` 上的 NDJSON，每行一个 UTF-8 JSON object。
- `stdout` 只能输出协议包；C++/Qt 日志必须写入 `stderr`。
- Rust 与前端各自维护 `EXPECTED_PROTOCOL_VERSION = 1`。收到版本不一致的 `ready` 后必须提示升级或拒绝继续。

## 2. 命令包络

前端通过 Tauri `invoke('qqnt_command', { payload })` 把命令交给 Rust，Rust 原样转为 NDJSON 写入 `QQNTEngine`。

```json
{
  "op": "send_private_message",
  "reqId": "uuid",
  "payload": {
    "receiverId": "10001",
    "content": "hello"
  }
}
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---:|---|
| `op` | string | 是 | 命令名，见命令表 |
| `reqId` | string | 是 | 前端生成的请求 ID，建议 `crypto.randomUUID()` |
| `payload` | object | 否 | 命令参数；无参数时可省略或传 `{}` |

## 3. 响应与事件包络

`ack` 用于响应某个 `reqId`，`event` 用于主动推送。

```json
{
  "type": "ack",
  "op": "send_private_message",
  "reqId": "uuid",
  "status": "ok",
  "payload": {}
}
```

```json
{
  "type": "event",
  "event": "message",
  "payload": {
    "sessionId": "10001",
    "message": {}
  }
}
```

| 字段 | 类型 | 适用 | 说明 |
|---|---|---|---|
| `type` | `ack` \| `event` | 全部 | 包类型 |
| `op` | string | `ack` | 对应命令名 |
| `event` | string | `event` | 事件名 |
| `reqId` | string | `ack` | 对应请求 ID |
| `status` | `ok` \| `error` | `ack` | 命令执行状态 |
| `payload` | object | 全部 | 业务载荷 |
| `error` | object | `ack:error` | 统一错误对象 |

错误对象格式：

```json
{
  "code": "engine_timeout",
  "message": "engine timeout",
  "source": "rust"
}
```

## 4. Ready 握手

`QQNTEngine` 启动完成后必须主动发出 `ready` 事件；收到 `ready` 命令时也必须返回同等信息的 `ack`。

```json
{
  "type": "event",
  "event": "ready",
  "payload": {
    "protocolVersion": 1,
    "version": "0.1.0",
    "qtVersion": "6.8.3",
    "e2eStatus": "uninitialized"
  }
}
```

## 5. 命令表

| `op` | `payload` | 说明 |
|---|---|---|
| `ready` | `{}` | 查询协议版本、程序版本、Qt 版本、E2E 状态 |
| `connect` | `{host, port}` | TCP 连接 QQNTServer |
| `disconnect` | `{}` | 断开 TCP 连接 |
| `login` | `{account, password}` | 账号密码登录 |
| `register` | `{account, password, userName}` | 注册账号 |
| `logout` | `{}` | 清空状态并断开 |
| `set_user_info` | `{userId, userName}` | 设置当前用户信息 |
| `get_user_list` | `{}` | 获取在线用户 |
| `get_friend_list` | `{}` | 获取好友列表 |
| `get_group_list` | `{}` | 获取群组列表 |
| `search_friend` | `{account}` | 搜索 QQ 号 |
| `send_friend_request` | `{receiverId}` | 发送好友申请 |
| `respond_friend_request` | `{senderId, accepted}` | 接受或拒绝好友申请 |
| `send_private_message` | `{receiverId, content}` | 发送私聊文本 |
| `send_group_message` | `{groupId, content}` | 发送群聊文本 |
| `create_group` | `{groupName, members, announcement?}` | 创建群聊 |
| `update_group_announcement` | `{groupId, announcement}` | 更新群公告 |
| `update_group_member` | `{groupId, memberId, action}` | 管理群成员 |
| `send_file` | `{receiverId?, groupId?, filePath}` | 发送文件 |
| `send_image` | `{receiverId?, groupId?, filePath}` | 发送图片 |
| `cancel_transfer` | `{transferId}` | 取消文件传输 |
| `query_resume` | `{filePath, transferId, receiverId?}` | 查询断点续传状态 |
| `e2e_status` | `{peerId}` | 查询 E2E 会话状态 |
| `e2e_announce_identity` | `{peerId}` | 公告身份密钥 |
| `e2e_pin_identity` | `{peerId, fingerprint?}` | 固定或更新身份指纹 |
| `e2e_request_rotation` | `{peerId}` | 请求会话密钥轮换 |
| `profile_update` | `{userName?, avatarBase64?}` | 更新个人资料 |
| `settings_sync` | `{settings}` | 同步本地设置 |

## 6. 主动事件表

Rust 收到 `event` 后统一按 `qqnt://engine/<event>` 转发给前端。

| `event` | `payload` | 说明 |
|---|---|---|
| `ready` | `{protocolVersion, version, qtVersion, e2eStatus}` | 引擎初始化完成 |
| `connection_state` | `{connected, host, port}` | TCP 连接状态变化 |
| `login_result` | `{success, userId, userName, error?}` | 登录结果 |
| `user_list` | `{users}` | 在线用户列表 |
| `user_joined` | `{userId, userName}` | 用户上线 |
| `user_left` | `{userId, userName}` | 用户下线 |
| `friend_event` | `{type, senderId, senderName, accepted?}` | 好友申请或回应 |
| `friend_search_result` | `{found, userId, userName, online}` | 搜索好友结果 |
| `message` | `{sessionId, message}` | 新消息 |
| `group_snapshot` | `{groups}` | 群组快照 |
| `group_member_updated` | `{groupId, memberId, action}` | 群成员变化 |
| `file_progress` | `{transferId, fileName, bytes, total, direction}` | 文件传输进度 |
| `file_done` | `{transferId, fileName, filePath, direction}` | 文件传输完成 |
| `file_error` | `{transferId, reason}` | 文件传输失败 |
| `e2e_session_state` | `{peerId, rotationRequired}` | E2E 会话状态 |
| `e2e_identity_state` | `{peerId, trusted, fingerprint}` | E2E 身份状态 |
| `notification` | `{title, body}` | 前端通知 |
| `error` | `{message, source}` | 通用错误 |

## 7. 消息模型最小字段

```json
{
  "messageId": "server-or-client-id",
  "clientMessageId": "uuid",
  "sessionId": "10001",
  "senderId": "10000",
  "senderName": "Alice",
  "timestamp": 1710000000000,
  "contentType": "text",
  "content": "hello",
  "status": "received"
}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `messageId` | string | 服务端消息 ID；乐观发送时可暂用客户端 ID |
| `clientMessageId` | string | 前端生成的本地消息 ID |
| `sessionId` | string | 私聊使用 peerId，群聊使用 `group:<groupId>` |
| `contentType` | `text` \| `image` \| `file` \| `system` | 消息类型 |
| `status` | `sending` \| `sent` \| `failed` \| `received` | 消息状态 |

## 8. Tauri 事件与命令约定

- 前端命令入口固定为 `invoke('qqnt_command', { payload })`。
- Engine 事件固定为 `listen('qqnt://engine/<event>', handler)`。
- Server 致命错误固定为 `listen('qqnt://server/fatal', handler)`，例如 Redis 未就绪。
- 后端新增字段必须保持向后兼容；删除或改名必须升级 `protocolVersion`。
