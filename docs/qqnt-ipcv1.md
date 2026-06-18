# QQ NT IPC v1 协议

本文档是 React 前端、Tauri Rust 主进程、C++ QQNTEngine sidecar 之间的协议单一事实来源。所有新增命令、事件、字段都先更新本文档，再同步实现。

## 1. 版本与传输

- 当前协议版本：`1`。
- 传输格式：`stdin`/`stdout` 上的 NDJSON，每行一个 UTF-8 JSON object。
- `stdout` 只能输出协议包；C++/Qt 日志必须写入 `stderr`。
- Rust 与前端各自维护 `EXPECTED_PROTOCOL_VERSION = 1`。收到版本不一致的 `ready` 后必须提示升级或拒绝继续。

## 2. 命令包络

前端通过 Tauri `invoke('qqnt_command', { payload })` 把命令交给 Rust；Rust 先校验命令包络，再转为 NDJSON 写入 `QQNTEngine`，并在返回前校验 `ack` 包络。

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
| `create_group` | `{groupName, members?: string[]/memberRef[], announcement?}` | 创建私有群聊；`members` 为初始成员账号或成员对象数组，成员对象可使用 `account`/`id`/`userId`/`memberId`，服务端加入已存在账号 |
| `update_group_announcement` | `{groupId, announcement}` | 更新群公告 |
| `update_group_member` | `{groupId, memberId, action=add/remove/promote_admin/demote_admin}` | 管理群成员 |
| `send_file` | `{receiverId xor groupId, filePath}` | 发送文件；必须且只能提供一个目标，缺失返回 `missing_target`，同时提供返回 `ambiguous_target` |
| `send_image` | `{receiverId xor groupId, filePath}` | 发送图片；必须且只能提供一个目标，缺失返回 `missing_target`，同时提供返回 `ambiguous_target` |
| `cancel_transfer` | `{transferId}` | 取消当前活动文件传输；`transferId` 必须匹配当前发送任务，成功返回 `{cancelled, transferId}`，无活动返回 `transfer_not_active`，不匹配返回 `transfer_mismatch` |
| `query_resume` | `{transferId, filePath?, receiverId xor groupId?, contentType?}` | 查询断点续传状态；提供 `filePath` 时按状态继续发送，恢复发送必须且只能提供一个目标，缺失返回 `missing_target`，同时提供返回 `ambiguous_target`；`receiverId`/`groupId` 仅在提供 `filePath` 的恢复模式下有效；`contentType` 仅支持 `file`/`image` |
| `e2e_status` | `{peerId?}` | 查询 E2E 状态；省略时只返回 `localIdentity`，提供时追加 `session` 与 `identity` |
| `e2e_announce_identity` | `{peerId}` | 公告身份密钥 |
| `e2e_pin_identity` | `{peerId, fingerprint?}` | 固定或更新身份指纹 |
| `e2e_request_rotation` | `{peerId}` | 请求会话密钥轮换 |
| `profile_update` | `{userName?, avatarBase64?}` | 更新个人资料；`avatarBase64` 提供时必须是有效 Base64，非法返回 `invalid_profile_field` |
| `settings_sync` | `{settings}` | 同步本地设置 |

### 成功 ack payload 表

`status: "ok"` 的 `ack.payload` 必须符合下表；失败时使用统一 `error` 对象，不使用本表字段。

| `op` | `payload` | 说明 |
|---|---|---|
| `ready` | `{protocolVersion, version, qtVersion, e2eStatus}` | 与 `ready` 事件一致 |
| `connect` | `{connected, host, port}` | 连接目标与结果 |
| `disconnect` | `{}` | 空成功回包 |
| `login` | `{accepted, requiresConnect, mode=login}` | 登录凭据已接收 |
| `register` | `{accepted, requiresConnect, mode=register}` | 注册凭据已接收 |
| `logout` | `{}` | 空成功回包 |
| `set_user_info` | `{}` | 空成功回包 |
| `get_user_list` | `{users}` | 在线用户列表快照 |
| `get_friend_list` | `{friends}` | 好友列表快照 |
| `get_group_list` | `{groups, removedGroups, hasSnapshot}` | 群组列表快照 |
| `search_friend` | `{accepted}` | 命令已发送到服务端 |
| `send_friend_request` | `{accepted}` | 命令已发送到服务端 |
| `respond_friend_request` | `{accepted}` | 命令已发送到服务端 |
| `send_private_message` | `{receiverId}` | 私聊发送目标 |
| `send_group_message` | `{accepted}` | 命令已发送到服务端 |
| `create_group` | `{accepted}` | 命令已发送到服务端 |
| `update_group_announcement` | `{accepted}` | 命令已发送到服务端 |
| `update_group_member` | `{accepted}` | 命令已发送到服务端 |
| `send_file` | `{accepted}` | 文件发送任务已接收 |
| `send_image` | `{accepted}` | 图片发送任务已接收 |
| `cancel_transfer` | `{cancelled, transferId}` | 当前活动传输已取消 |
| `query_resume` | `{canResume, transferId, confirmedBytes, nextChunkIndex, fileSize, chunkSize, chunkCount, fileHash, receivedChunks, resumed, mode}` | 断点续传状态；计数字段和 `receivedChunks` 项以无符号整数字符串表示，`mode` 为 `query` 或 `resume` |
| `e2e_status` | `{localIdentity, peerId?, session?, identity?}` | 省略 `peerId` 时仅返回本地身份；提供 `peerId` 时同时返回会话与对端身份 |
| `e2e_announce_identity` | `{accepted}` | 命令已发送到服务端 |
| `e2e_pin_identity` | `{accepted}` | 本地身份信任状态已更新 |
| `e2e_request_rotation` | `{accepted}` | 命令已发送到服务端 |
| `profile_update` | `{accepted, avatarSent, userName}` | 个人资料更新结果 |
| `settings_sync` | `{accepted, revision, settings, appliedDownloadDir?}` | 设置同步结果；下载目录实际应用时返回 `appliedDownloadDir` |

## 6. 主动事件表

Rust 收到 `event` 后统一按 `qqnt://engine/<event>` 转发给前端。

| `event` | `payload` | 说明 |
|---|---|---|
| `ready` | `{protocolVersion, version, qtVersion, e2eStatus}` | 引擎初始化完成 |
| `connection_state` | `{connected, host, port}` | TCP 连接状态变化 |
| `login_result` | `{success, userId?, userName?, registered?, error?}` | 登录结果 |
| `user_list` | `{users}` | 在线用户列表 |
| `user_joined` | `{userId, userName}` | 用户上线 |
| `user_left` | `{userId, userName}` | 用户下线 |
| `friend_list` | `{friends}` | 好友列表快照 |
| `friend_event` | `{type, senderId?, senderName?, receiverId?, accepted?, delivered?}` | 好友申请发送、收到或回应 |
| `friend_search_result` | `{found, userId, userName, online, reason?}` | 搜索好友结果 |
| `message` | `{sessionId, message}` | 新消息 |
| `group_snapshot` | `{groups, removedGroups, hasSnapshot}` | 群组快照；`removedGroups` 保留被移出群后的只读历史标记 |
| `group_member_updated` | `{groupId, memberId, action=add/remove/promote_admin/demote_admin}` | 群成员变化 |
| `file_progress` | `{transferId, fileName, bytes, total, direction=incoming/outgoing}` | 文件传输进度 |
| `file_done` | `{transferId, fileName, filePath, direction=incoming/outgoing}` | 文件传输完成 |
| `file_error` | `{transferId, reason}` | 文件传输失败 |
| `e2e_session_state` | `{peerId, rotationRequired}` | E2E 会话状态 |
| `e2e_identity_state` | `{peerId, configured, trusted, publicKeyFingerprintSha256?}` | E2E 身份状态 |
| `e2e_rotation_request` | `{peerId, agreement}` | 收到 E2E 会话轮换请求 |
| `e2e_rotation_response` | `{peerId, agreement, accepted, reason?}` | 收到 E2E 会话轮换回应 |
| `settings_synced` | `{accepted, revision, settings, appliedDownloadDir?}` | 设置已由 engine 接收；传 `settings.files.downloadDir`、`settings.files.downloadDirectory` 或 `settings.fileDownloadDir` 时同时应用接收文件下载目录 |
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
