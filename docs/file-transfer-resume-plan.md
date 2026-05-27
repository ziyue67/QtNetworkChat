# 文件传输跨连接续传计划

当前在线文件/图片传输已经有单连接内的分片 ACK、ACK 超时后续传状态查询、发送端本地 outgoing transfer 元数据持久化，以及离线附件回放的 confirmedBytes/confirmedChunks 缺口续发。跨连接持久化续传的目标是在发送端或接收端断开重连后，仍能基于同一份文件元数据恢复未完成上传，而不是要求用户重新发送整文件。

## 目标

- 服务端在连接断开后短期保留未完成上传状态，并允许同一发送者按 `transferId` 查询。
- 客户端恢复时必须校验 `fileHash`、`fileSize`、`chunkSize`、`chunkCount` 与本地文件一致。
- 续传只信任服务端已经落入 pending 状态的分片索引；客户端从最早缺口续发，并跳过后续已确认分片。
- 过期、取消、元数据冲突或 hash 不一致时明确拒绝续传，并清理服务端临时状态。

## 状态归属

服务端 pending 状态应从当前 socket 维度扩展为连接无关的 transfer 维度：

| 字段 | 说明 |
| --- | --- |
| `transferId` | 客户端生成并持久化的传输编号，作为主键的一部分 |
| `senderId` | 当前登录账号，防止其他用户复用 transferId |
| `receiverId` | 私聊接收者；公共群可为空 |
| `fileName` | 展示和提示用，不作为可信校验 |
| `fileHash` | SHA-256，必须与恢复请求一致 |
| `fileSize` | 文件总大小，必须与恢复请求一致 |
| `chunkSize` | 当前固定为客户端分片大小，必须一致 |
| `chunkCount` | 总分片数，必须一致 |
| `receivedIndexes` | 已接收分片集合，用于推断最早缺口 |
| `lastActivityMs` | 清理过期状态 |

当前内存 key 包含 socket 指针，断线后会在 `onClientDisconnected()` 中删除。跨连接续传需要把 pending key 调整为 `senderId:transferId`，并保留 socket 只作为当前连接引用。

## 握手流程

1. 发送端开始文件发送前持久化 outgoing transfer 状态。
2. 服务端收到首个分片后建立 pending 状态，并在 ACK 中返回 `receivedBytes`。
3. 若连接断开，服务端保留 pending 状态到短 TTL。
4. 发送端重连后调用 `file_transfer_resume_query`，请求中带 `transferId`。
5. 服务端仅在当前登录 `senderId` 与 pending 状态一致，且元数据一致时返回 `canResume=true`、`confirmedBytes`、`nextChunkIndex`、`receivedChunks`、`fileSize`、`chunkSize`、`chunkCount`、`fileHash`。
6. 客户端按现有 `resolveResumeProgress()` 从 receivedChunks 推断最早缺口并续发。
7. 所有分片组包成功后，服务端删除 pending 状态；客户端清理 outgoing transfer 状态。

## 拒绝和清理

- `transferId` 不存在：返回 `canResume=false`，原因“服务端未找到可续传状态”。
- `senderId` 不一致：返回 `canResume=false`，原因“续传发送者不一致”，并不泄露原状态细节。
- `fileHash`、`fileSize`、`chunkSize`、`chunkCount` 不一致：返回 `canResume=false`，原因按具体字段区分。
- `receivedIndexes` 越界或与 `receivedBytes` 冲突：清理 pending 状态并拒绝。
- 用户取消传输：立即删除 pending 状态，并清理客户端本地 outgoing transfer 状态。
- pending 状态超过 TTL：定时清理，并向仍在线的发送端提示“文件分片上传已超时清理”。

## 最小实现顺序

1. 先补服务端单元测试：同一发送者断开后重连，查询原 transferId 能拿到已接收分片集合。
2. 调整服务端 pending key，不再把 socket 指针作为唯一身份；断开时只清 socket 引用，不删除未完成状态。
3. 补元数据不一致和 senderId 不一致的拒绝测试。
4. 补客户端 `resumeSavedOutgoingTransfer()` 的跨连接集成测试，验证重连后只发送缺口分片。
5. 最后再考虑 UI 文案和用户可见的“继续发送未完成文件”提示细化。
