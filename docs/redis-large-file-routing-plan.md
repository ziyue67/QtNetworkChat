# Redis 跨实例大文件路由计划

当前 Redis Pub/Sub 只适合承载文本和编码后不超过 1 MB 的小文件/小图片事件。大文件或 base64/JSON 编码后超限的文件会留在源实例离线附件队列，等待收件人连接到源实例后回放。跨实例大文件路由的目标是让收件人在线于其他服务实例时，也能收到大文件，同时坚持一个边界：Pub/Sub 只传控制消息，不传文件字节。

## 目标

- 大文件和图片跨实例投递不通过 Redis Pub/Sub 承载二进制 payload。
- 远端实例只在本实例确有在线收件人时拉取或读取附件。
- 文件元数据必须校验 `fileHash`、`fileSize`、`chunkSize`、`chunkCount`，不信任远端声明的任意路径。
- 源实例在远端确认完整投递前保留离线附件兜底，避免跨实例链路失败导致丢文件。
- 所有临时对象都有 TTL、配额和孤儿清理策略。

## 非目标

- 不把 Redis Streams、Lists 或 Pub/Sub 当成大文件分片通道。
- 不在第一阶段引入端到端加密；只保证服务端之间的元数据校验和传输完整性。
- 不要求一次支持公网跨 NAT 直连；先覆盖同一部署集群内的共享对象存储或共享磁盘场景。

## 推荐架构

第一阶段采用“控制面 Pub/Sub + 数据面对象存储”的方案：

| 通道 | 内容 | 说明 |
| --- | --- | --- |
| Redis Pub/Sub | `large_file_offer`、`large_file_claim`、`large_file_delivered`、`large_file_failed` 等控制事件 | 事件体必须保持很小，只包含元数据和对象 key |
| 对象存储或共享附件目录 | 原始文件字节 | 可先用共享文件系统目录，后续替换为 S3/MinIO 等对象存储 |
| 现有 TCP 客户端连接 | 服务端到最终收件人的分片下发 | 复用 `sendChunkedFileToSocket()` 与 ACK 校验 |

源实例收到大文件后，把附件写入对象存储，同时在本地离线队列保留同一份可回放状态；当前已完成这一步的 filesystem ObjectStore 写入与 `large_file_offer` 发布。远端实例收到 offer 后，已可在收件人在线于本实例时认领对象，校验对象 size/hash/chunk 元数据并按现有分片 ACK 流程下发；成功后还需要补 `large_file_delivered`，让源实例清理本地兜底队列和对象引用。

## 控制事件

### `large_file_offer`

源实例发布，表示有一个可跨实例拉取的附件。

必需字段：

- `eventType`: `large_file_offer`
- `instanceId`: 源实例 ID
- `transferId`: 客户端传输编号或服务端生成编号
- `objectKey`: 随机对象 key，不允许是本地绝对路径
- `senderId` / `senderName`
- `receiverId`
- `messageType`: `File` 或 `Image`
- `fileName`
- `fileSize`
- `fileHash`: SHA-256
- `chunkSize`
- `chunkCount`
- `expiresAt`

### `large_file_claim`

远端实例发布，表示本实例有在线收件人并开始投递。

必需字段：

- `eventType`: `large_file_claim`
- `instanceId`: 认领实例 ID
- `sourceInstanceId`
- `transferId`
- `objectKey`
- `receiverId`

源实例收到 claim 后不立即删除本地状态，只记录“远端投递中”，并保留 TTL 兜底。

### `large_file_delivered`

远端实例在客户端完整 ACK 后发布。

必需字段：

- `eventType`: `large_file_delivered`
- `instanceId`: 投递实例 ID
- `sourceInstanceId`
- `transferId`
- `objectKey`
- `receiverId`
- `confirmedBytes`
- `fileHash`

源实例确认 `sourceInstanceId`、`transferId`、`receiverId`、`objectKey`、`fileHash` 匹配后，清理离线队列和对象引用。

### `large_file_failed`

远端实例遇到收件人断开、ACK 超时、对象读取失败或元数据校验失败时发布。源实例收到后继续保留离线队列和对象引用，等待下一次投递或 TTL 清理。

## 元数据校验

远端实例读取对象前必须校验：

- `objectKey` 只能由服务端生成，不能来自客户端路径。
- 对象实际大小等于 `fileSize`。
- 流式读取计算的 SHA-256 等于 `fileHash`。
- `chunkSize` 等于当前服务端下发分片大小。
- `chunkCount` 等于 `(fileSize + chunkSize - 1) / chunkSize`。
- 当前实例存在 `receiverId` 的在线 socket；否则忽略 offer。

任何校验失败都发送 `large_file_failed`，并避免向客户端下发部分文件。

## 失败回退

- Redis 发布失败：保持当前行为，源实例离线队列兜底。
- 没有远端实例认领：源实例离线队列保留，等待收件人回源实例或后续重试。
- 远端认领后客户端断开：远端发布 failed，源实例保留离线队列。
- 对象存储不可用：不发布 offer，直接使用源实例离线队列。
- delivered 丢失：源实例按 TTL 暂时保留对象和离线队列；后续可补对账任务，按远端 delivery receipt 查询清理。

## 配置建议

| 环境变量 | 作用 | 默认 |
| --- | --- | --- |
| `QTNETWORKCHAT_LARGE_FILE_ROUTING` | 是否启用跨实例大文件对象路由 | `0` |
| `QTNETWORKCHAT_OBJECT_STORE` | `filesystem` 或后续的 `s3` | `filesystem` |
| `QTNETWORKCHAT_OBJECT_ROOT` | 共享对象根目录 | 空，未配置时禁用对象路由 |
| `QTNETWORKCHAT_OBJECT_TTL_HOURS` | 对象保留时间 | `24` |
| `QTNETWORKCHAT_OBJECT_QUOTA_MB` | 对象存储软配额 | `1024` |

## 最小实现顺序

1. 已完成：新增 filesystem `ObjectStore` helper，支持安全 objectKey 生成、共享目录写入、路径穿越拒绝、hash/size 校验和 TTL 清理，并用 CTest 覆盖核心边界。
2. 已完成：源实例在大文件或编码超限文件进入离线附件队列后，若对象路由配置可用，会额外写入对象并发布 `large_file_offer` 元数据事件；发布失败时仍保留源实例离线队列兜底。
3. 已完成：远端实例订阅 `large_file_offer`，仅当 `receiverId` 在线于本实例时认领，校验对象 size/hash/chunk 元数据，并从对象存储按分片 ACK 下发给客户端。
4. 下一步：远端完整 ACK 后发布 `large_file_delivered`；源实例收到后清理离线队列和对象引用。
5. 补失败路径：对象读取失败、hash 不一致、客户端断开、delivered 丢失和 TTL 清理。
6. 再评估 S3/MinIO 后端，把 filesystem helper 抽象为最小 `ObjectStore` 接口。

## 当前保护边界

- 已有代码会拒绝发布编码后超过 1 MB 的 Redis message event。
- 已有测试覆盖大文件和编码后超限文件不经 Pub/Sub，并回落源实例离线队列。
- 已有测试覆盖源实例为大文件和编码超限文件发布小体积 `large_file_offer`，且 offer 指向对象的 size/hash 与原始附件一致。
- 已有测试覆盖远端实例仅在本地在线收件人存在时认领 `large_file_offer`，并从 filesystem ObjectStore 校验后分片下发给客户端。
- 源实例尚未处理 `large_file_delivered`，因此远端投递成功后仍会保留源实例离线兜底队列和对象引用；这是下一步清理边界。
