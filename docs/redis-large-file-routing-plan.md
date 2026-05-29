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

源实例收到大文件后，把附件写入对象存储，同时在本地离线队列保留同一份可回放状态；当前已完成这一步的 filesystem ObjectStore 写入与 `large_file_offer` 发布。远端实例收到 offer 后，已可在收件人在线于本实例时认领对象，校验对象 size/hash/chunk 元数据并按现有分片 ACK 流程下发；完整 ACK 后会发布 `large_file_delivered`，源实例确认匹配后清理本地兜底队列、离线附件和对象文件。远端对象校验、读取、客户端连接或 ACK 失败时会发布 `large_file_failed`，源实例确认来源后保留离线兜底。代码层已有最小 `ObjectStore` 抽象和后端工厂，filesystem 实现通过同一接口提供写入、校验、读取、删除和 TTL 清理；未支持后端会明确拒绝，后续 S3/MinIO 后端应优先复用该边界。

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

远端实例遇到收件人断开、ACK 超时、对象读取失败或元数据校验失败时发布。源实例收到后校验 `sourceInstanceId`、`transferId`、`receiverId`、`objectKey` 和 `fileHash` 的基本形态，并继续保留离线队列和对象引用，等待下一次投递或 TTL 清理。

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
- delivered 丢失或不完整：源实例只在 `confirmedBytes >= fileSize` 且 sourceInstanceId、transferId、receiverId、objectKey、fileHash 全部匹配时清理队列和对象；回执缺失、confirmedBytes 不足或元数据不匹配时继续保留离线队列。当前清理资格已提取为只读纯函数，固定输出 `cleaned`、`invalid-receipt`、`invalid-payload`、`receipt-not-matched`、`confirmed-bytes-insufficient`，便于后续对账任务复用同一安全判定；对象 TTL 到期后可清理 ObjectStore 对象，离线附件队列继续作为回源兜底。后续可补对账任务，按远端 delivery receipt 查询清理队列。

## 配置建议

| 环境变量 | 作用 | 默认 |
| --- | --- | --- |
| `QTNETWORKCHAT_LARGE_FILE_ROUTING` | 是否启用跨实例大文件对象路由 | `0` |
| `QTNETWORKCHAT_OBJECT_STORE` | `filesystem` 或后续的 `s3` | `filesystem` |
| `QTNETWORKCHAT_OBJECT_ROOT` | 共享对象根目录 | 空，未配置时禁用对象路由 |
| `QTNETWORKCHAT_OBJECT_TTL_HOURS` | 对象保留时间 | `24` |
| `QTNETWORKCHAT_OBJECT_QUOTA_MB` | 对象存储软配额 | `1024` |

## S3/MinIO 后端设计

`s3` 后端用于替换共享文件系统目录，但必须保持与 filesystem 后端相同的 `ObjectStore` 契约：写入返回随机 `objectKey` 和标准 SHA-256，读取前可独立校验 size/hash，删除和 TTL 清理失败时不影响离线附件兜底。第一阶段只设计配置和测试替身，不引入真实云存储依赖。

建议新增配置：

| 环境变量 | 作用 | 安全边界 |
| --- | --- | --- |
| `QTNETWORKCHAT_OBJECT_S3_ENDPOINT` | S3/MinIO endpoint，例如 `https://minio.internal:9000` | 必须显式配置，不从 Redis 事件读取 |
| `QTNETWORKCHAT_OBJECT_S3_BUCKET` | 存储 bucket | 只允许服务端配置，offer 只传 `objectKey` |
| `QTNETWORKCHAT_OBJECT_S3_REGION` | S3 region，MinIO 可留空或设为本地约定值 | 不参与控制事件 |
| `QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY` / `QTNETWORKCHAT_OBJECT_S3_SECRET_KEY` | 访问凭据 | 只读进程环境变量，不写入日志、Redis 或离线队列 |
| `QTNETWORKCHAT_OBJECT_S3_SESSION_TOKEN` | 可选临时凭据 token | 只进入本机 S3 请求的 `x-amz-security-token` 签名头，不写入日志、Redis 或离线队列 |
| `QTNETWORKCHAT_OBJECT_S3_PREFIX` | 对象 key 前缀，例如 `qtchat/large-files/` | 必须规范化，禁止 `..`、反斜杠和绝对路径语义 |
| `QTNETWORKCHAT_OBJECT_S3_TLS_VERIFY` | 是否校验证书链 | 默认开启；关闭时必须输出 warning |
| `QTNETWORKCHAT_OBJECT_S3_TIMEOUT_MS` | 单次 S3 HTTP 请求超时 | 默认 `30000`，只接受 `1000` 到 `300000` 毫秒；超时走对象路由失败回退 |
| `QTNETWORKCHAT_OBJECT_S3_ENABLE` | 是否启用真实 S3/MinIO 网络后端 | 默认关闭；只有显式设为 `1/true/yes/on` 时工厂才创建真实后端 |

当前已完成配置校验骨架：`s3` 模式会解析上述环境变量，校验 endpoint、bucket、access key、secret key 和 prefix，并为后续网络请求读取可选 session token、有界超时与显式启用开关；配置错误和未启用错误都会返回不含凭据值的明确原因。即使配置完整，未设置 `QTNETWORKCHAT_OBJECT_S3_ENABLE=1` 时也不会创建真实网络后端，不会把 access key、secret key 或 session token 写入错误信息。

当前已完成薄适配骨架：`S3ObjectStore` 类已存在并持有规范化后的配置；默认构造仍保持 fail-closed，工厂只有在配置完整且 `QTNETWORKCHAT_OBJECT_S3_ENABLE` 显式启用时才创建带 Qt Network 执行器的 `s3` 后端。测试可注入 `S3RequestExecutor`，用不联网替身覆盖 `writeObject()` 的 `PUT` 上传、本地 SHA-256 返回、`openObject()` 的 `GET` 只读设备、`validateObject()` 的 `HEAD` 快速大小检查加 `GET` 响应体 size/hash 最终校验，以及 `removeObject()` 的 `DELETE` 结果处理；默认未启用时仍 fail-closed，TTL 清理为 no-op；单测会校验这些失败路径不泄露 access key、secret key 或 session token。

当前已完成 URL、签名与请求构造边界骨架：`s3ObjectUrl()` 会按 MinIO 兼容的 path-style 格式生成 `endpoint/bucket/prefix/objectKey`，并复用 objectKey 校验拒绝路径穿越；Signature V4 纯函数已覆盖 payload SHA-256、credential scope、canonical request、string-to-sign、HMAC signing key、signature 和 Authorization header，并用 AWS GET Object 固定向量锁定输出；`s3SignedObjectRequest()` 可在不联网的前提下生成带 `host`、`x-amz-date`、`x-amz-content-sha256`、可选 `x-amz-security-token`、`Authorization` 和 transfer timeout 的 `QNetworkRequest`，覆盖方法规范化、空 region 默认值、非法 objectKey 拒绝、TLS 校验开关、请求超时、临时凭据 token 签名头和仅允许 `PUT`/`GET`/`HEAD`/`DELETE` 的第一阶段对象方法边界；`classifyS3HttpStatus()` 已把 2xx 成功、404 不存在、401/403 凭据或权限错误、408/409/429 可重试状态、4xx 客户端错误、5xx 可重试服务端错误和未知状态分开；`s3RequestResultFromReply()` 已把 HTTP 状态、网络错误、超时和 TLS 失败归一到 `S3HttpResult`，并由 `redactS3ErrorText()` 脱敏 access key、secret key、session token、Authorization/Credential/Signature 文本；`s3FailureReasonForLog()` 和 `s3ValidationFailureReasonForLog()` 只返回固定 reason 字符串，可聚合 timeout/network/tls/auth/not_found/retryable/client/server/hash/size 等失败而不携带 endpoint、bucket、object URL 或凭据；注入式 `PUT`/`GET`/`HEAD`/`DELETE` 执行边界已覆盖上传 payload、返回本地 SHA-256、GET 只读设备、HEAD 快速大小检查、GET 响应体最终 size/hash 校验、缺 HEAD hash 时仍以 body hash 为准、失败错误脱敏和删除结果处理；`executeS3ObjectRequest()` 已提供真实 Qt Network 执行薄层，负责发起 `sendCustomRequest()`、收集响应 header/body、按配置超时 abort、捕获 TLS 错误，并统一返回脱敏后的 `S3RequestExecutionResult`。后续真实后端应复用这些边界发起 Qt Network 请求，不把 bucket、endpoint、凭据或 Authorization header 写入 Redis 控制事件或日志。

适配规则：

- 工厂只在 `QTNETWORKCHAT_OBJECT_STORE=s3`、endpoint/bucket/凭据完整且 `QTNETWORKCHAT_OBJECT_S3_ENABLE=1` 时创建真实网络后端；配置缺失或未显式启用时返回明确错误并保留源实例离线队列。
- S3 object key 仍由服务端随机生成，逻辑 `objectKey` 不包含 bucket、endpoint、绝对路径或凭据；实际远端 key 可由 `prefix + objectKey` 组成。
- `writeObject()` 上传后必须计算并返回本地 SHA-256；不能信任 S3 ETag 作为文件哈希，因为多段上传和加密场景下 ETag 不等于 MD5 或 SHA-256。
- `validateObject()` 必须至少校验对象大小和流式 SHA-256；HEAD 只能作为快速大小检查，最终仍以读取校验为准。
- TTL 清理优先依赖 bucket lifecycle；本地 `cleanupExpired()` 可只做 best-effort 前缀扫描或返回 0，但必须记录不可枚举/权限不足的原因，不能清理离线附件队列。
- 任何上传、下载、校验、删除、TLS 或凭据错误都回落为对象路由失败，源实例离线附件队列继续保留。

### Signature V4 与 Qt Network 计划

真实 S3/MinIO 后端建议按 AWS Signature Version 4 实现一个小型签名器，不引入大型 SDK：

- **请求范围**：第一阶段只需要 `PUT`、`GET`、`HEAD`、`DELETE` 四类对象请求；`LIST` 仅用于后续 best-effort TTL 清理，默认 CTest 不依赖。
- **Canonical request**：方法、规范化路径、规范化查询、`host`、`x-amz-content-sha256`、`x-amz-date`、可选 `x-amz-security-token` 进入 signed headers；payload hash 使用 SHA-256 十六进制，不使用 `UNSIGNED-PAYLOAD`。
- **String to sign**：`AWS4-HMAC-SHA256`、UTC `yyyyMMddTHHmmssZ`、`date/region/s3/aws4_request` scope 和 canonical request hash。
- **Signing key**：`AWS4 + secret` 依次 HMAC `date`、`region`、`s3`、`aws4_request`；日志和错误不得输出 secret、derived key 或 Authorization header。
- **Qt Network 调用**：用 `QNetworkAccessManager` 发 path-style `QNetworkRequest`；TLS 默认校验证书链，只有 `QTNETWORKCHAT_OBJECT_S3_TLS_VERIFY=0` 时才允许跳过并输出 warning；每次请求必须套用 `QTNETWORKCHAT_OBJECT_S3_TIMEOUT_MS` 的有界超时；超时、HTTP 4xx/5xx、证书错误和 hash/size mismatch 都走对象路由失败回退；HTTP 状态先通过 `classifyS3HttpStatus()` 归类，避免把权限、缺对象、限流和服务端错误混成同一种失败。
- **实现顺序**：签名纯函数测试、固定 AWS 示例向量、不联网 request 构造测试、对象方法白名单、HTTP 状态分类、S3 请求超时配置并写入 `QNetworkRequest`、可选 session token 签名头、请求执行结果结构、错误脱敏 helper、失败 reason 聚合 helper、注入式 `PUT`/`GET`/`HEAD`/`DELETE` 执行边界、GET 响应体 size/hash 最终校验、真实 Qt Network 执行器薄层、显式发布 gating、可选 MinIO 手动 smoke 脚本、服务端安全日志字段、人工验收清单、失败回退演练、只读 delivered 对账原型、`delivered_reconcile` 日志聚合、只读 route log 生成、服务端只读日志事件、receipt/fallback 输入导出、一键只读对账编排脚本、脱敏样例生成脚本、delivered receipt 摘要持久化原型、一键对账直接读取持久化 receipt、receipt 摘要轮转/压缩脚本、样例脚本一键轮转演练、轮转摘要落盘和轮转环境变量默认值已完成；下一步转向下一条 Redis/S3 治理可观测性小切片，或补轮转摘要告警阈值说明，保持失败时 fail-closed 和离线兜底。

测试替身计划：

- 已实现仅测试使用的 `InMemoryObjectStore`，复用 `ObjectStore` 契约测试，覆盖写入、读取、删除、size/hash 不一致和 TTL no-op 行为。
- 服务端集成测试不连接真实 S3；只验证工厂在 `s3` 配置缺失时不发布 offer，并在未来注入假后端后可复用同一分片 ACK 下发流程。
- 真实 MinIO 端到端验证已提供可选 `scripts/minio-s3-smoke.ps1`，可用 Docker 自动启动本地 MinIO 或通过 `-SkipContainer` 连接已有 MinIO；脚本会用 SigV4 对 bucket 创建和对象 PUT/HEAD/GET/DELETE 做手动 smoke，并输出 QtNetworkChat 所需环境变量示例。该脚本不纳入默认 CTest 前置条件。
- 启用真实后端前必须先跑 smoke 脚本确认 endpoint、bucket、access key、secret key、region 和 prefix 可用，再设置 `QTNETWORKCHAT_OBJECT_STORE=s3` 与 `QTNETWORKCHAT_OBJECT_S3_ENABLE=1` 启动服务端。真实后端仍必须遵守离线兜底：任何 S3 上传、下载、校验、删除、TLS、超时或凭据错误都发布固定 reason 并保留源实例离线附件队列。
- 手动验收日志只检查 `redis_large_file_route` 的 `event/result/reason/objectKey/receiverId/fileHash/bytes/storeType/operation` 等逻辑字段；不得输出 endpoint、bucket、object URL、access key、secret key、session token、Authorization、Credential 或 Signature。默认 CTest、CI 和 smoke 脚本都不要求真实 S3 长驻运行。
- 真实后端人工验收清单已补到 `docs/s3-minio-manual-acceptance.md`，覆盖 smoke、双服务端、远端在线大文件投递、失败注入、离线兜底回源回放和日志/Redis/队列脱敏检查。
- 日志聚合与脱敏检查已补可选脚本 `scripts/analyze-large-file-route-logs.ps1`，只读取服务端日志，统计 `event/result/reason/storeType/operation`，按 `transferId/objectKey/receiverId` 输出 delivered cleanup 与 failed fallback 对账候选摘要，并在结构化日志中发现 endpoint、bucket、object URL、凭据或签名字段时失败。
- 失败回退演练已补可选脚本 `scripts/s3-failure-drill.ps1`，默认只输出 network、auth、missing-object、receiver-disconnect 场景的注入方式、预期 reason 和兜底检查点；传入日志路径时复用日志分析器，不连接 Redis、S3/MinIO 或修改离线队列。
- delivered 回执清理判定已补 `evaluateLargeFileDeliveredReceiptCleanup()` 纯函数和 CTest，运行时源实例清理逻辑复用该 helper；源实例可通过 `QTNETWORKCHAT_DELIVERED_RECEIPT_DIR` 显式追加脱敏 `delivered-receipts.jsonl` 摘要；只读对账原型脚本 `scripts/reconcile-large-file-delivery.ps1` 可读取 receipt 与源实例兜底 JSON/JSONL 摘要，输出候选 `cleaned`/`retained` 与固定 reason，`-EmitRouteLog` 可额外生成只读 `redis_large_file_route event=delivered_reconcile` 行供日志分析器聚合；`scripts/export-large-file-receipts.ps1` 可从安全 route log 导出 receipt 输入，`scripts/export-large-file-fallbacks.ps1` 可从离线队列摘要导出只含安全字段的 fallback 输入，`scripts/run-large-file-delivery-reconcile.ps1` 可用 `-RouteLogPath` 导出 receipt 或用 `-ReceiptPath` 直接消费持久化 receipt，并把后续步骤串成一键只读演练，`scripts/write-large-file-reconcile-sample.ps1 -RunReconcile -RunRotate` 可生成不含 S3 配置或凭据的样例输入并直接演练对账和轮转，`scripts/rotate-large-file-receipts.ps1` 可按条数和天数轮转持久化 receipt，并把旧摘要归档为 JSONL 或 ZIP。脚本都会扫描输入中是否误带 endpoint、bucket、object URL、凭据或签名字段；不连接 Redis、S3/MinIO，也不会修改离线队列、附件或对象。后续治理任务应先复用这类只读输出，再由人工或显式任务决定是否触发真实清理。

## 治理观测

服务端会输出统一前缀的 `redis_large_file_route` 结构化日志，字段采用 `key=value` 形式，便于压测或线上日志聚合：

- `event`: `offer`、`claim`、`delivered`、`failed`、`failed_received`、`delivered_cleanup`、`offer_validation`、`object_write`、`object_ttl_cleanup`
- `result`: `published`、`publish-failed`、`skipped`、`fallback-retained`、`cleaned`、`retained`、`removed`
- 常见维度：`sourceInstanceId`、`transferId`、`objectKey`、`receiverId`、`fileName`、`messageType`、`storeType`、`operation`、`bytes`、`reason`

其中 `offer/claim/delivered` 成功发布、`delivered_cleanup` 成功清理和 `object_ttl_cleanup` 会走 info 日志；失败、跳过、远端 failed 和保留兜底会走 warning 日志。

## Delivered 丢失对账任务设计

目标是补上“远端已投递但 `large_file_delivered` 控制事件丢失”的后续治理，同时不牺牲离线兜底安全性。对账任务只做保守清理，默认保留队列：

- **对账输入**：源实例本地离线队列中仍含 `objectStoreKey` 的消息、远端 delivery receipt 记录、对象 TTL 清理日志和 `redis_large_file_route` 日志。
- **可清理条件**：远端 receipt 必须同时匹配 `sourceInstanceId`、`transferId`、`receiverId`、`objectKey`、`fileHash`，并且 `confirmedBytes >= fileSize`。任一字段缺失、不一致或进度不足，都只能标记为待复查，不能删除离线队列。
- **对象已过期场景**：ObjectStore 对象被 TTL 清理不代表文件已送达；只清对象，不清离线附件队列。后续如果 receipt 补齐，再按可清理条件清队列和离线附件。
- **重试窗口**：在对象 TTL 内，源实例仍可等待远端补发 delivered 或 receipt；TTL 后对象可释放空间，但离线附件继续按 `QTNETWORKCHAT_OFFLINE_ATTACHMENT_TTL_DAYS` 作为最终兜底窗口。
- **误删保护**：对账任务必须复用 `cleanupDeliveredRedisLargeFile()` 的匹配规则或等价校验；不得仅凭 `transferId`、`receiverId`、claim 事件、日志行或对象不存在来删除队列。
- **观测输出**：服务端收到 delivered 回执时会先输出只读 `redis_large_file_route event=delivered_reconcile result=cleaned|retained reason=...`，再进入原有 `delivered_cleanup` 清理路径；离线脚本也可用 `-EmitRouteLog` 生成同类事件。该事件只记录候选和保留原因，方便区分 receipt 缺失、hash 不一致、进度不足和对象已 TTL 清理；现有日志分析脚本已可聚合 cleaned/retained 候选，不执行自动删除。
- **receipt 摘要**：设置 `QTNETWORKCHAT_DELIVERED_RECEIPT_DIR` 后，源实例会把有效 delivered 回执追加到 `delivered-receipts.jsonl`，字段仅包含 `sourceInstanceId`、`transferId`、`receiverId`、`objectKey`、`fileHash`、`confirmedBytes`、`result`、`reason`、`cleanupResult` 和 `createdAt`；默认关闭，且不会修改离线队列、附件或对象。运维可用 `scripts/rotate-large-file-receipts.ps1 -ReceiptPath delivered-receipts.jsonl -KeepRecords 10000 -MaxAgeDays 30 -CompressArchive -SummaryPath rotate-summary.json` 对摘要做离线轮转，输出旧摘要归档、原子重写 active 文件，并落盘本次轮转摘要供定时任务收集；计划任务也可用 `QTNETWORKCHAT_DELIVERED_RECEIPT_KEEP_RECORDS`、`MAX_AGE_DAYS`、`ARCHIVE_DIR`、`COMPRESS_ARCHIVE`、`SUMMARY_PATH` 提供默认值。
- **只读演练**：没有真实日志时，可用 `scripts/write-large-file-reconcile-sample.ps1 -OutputDir sample-reconcile -RunReconcile -RunRotate` 生成脱敏样例并跑完整只读对账和轮转链路；真实验收时可用 `scripts/run-large-file-delivery-reconcile.ps1 -ReceiptPath delivered-receipts.jsonl -QueuePath offline-queue.jsonl -SourceInstanceId source-a -OutputDir reconcile -EmitRouteLog` 直接消费持久化 receipt，也可用 `-RouteLogPath source.log,remote.log` 从日志导出 receipt。输出 `cleaned` 仅代表候选满足条件，不会执行真实清理。

## 最小实现顺序

1. 已完成：新增 filesystem `ObjectStore` helper，支持安全 objectKey 生成、共享目录写入、路径穿越拒绝、hash/size 校验和 TTL 清理，并用 CTest 覆盖核心边界。
2. 已完成：源实例在大文件或编码超限文件进入离线附件队列后，若对象路由配置可用，会额外写入对象并发布 `large_file_offer` 元数据事件；发布失败时仍保留源实例离线队列兜底。
3. 已完成：远端实例订阅 `large_file_offer`，仅当 `receiverId` 在线于本实例时认领，校验对象 size/hash/chunk 元数据，并从对象存储按分片 ACK 下发给客户端。
4. 已完成：远端完整 ACK 后发布 `large_file_delivered`；源实例收到并确认 sourceInstanceId、transferId、receiverId、objectKey、fileHash 和 confirmedBytes 匹配后，清理离线队列、离线附件和对象文件。
5. 已完成：远端对象缺失、校验失败、客户端断开或 ACK 超时时发布 `large_file_failed`；源实例收到后保留离线兜底队列和对象引用，后续登录仍可回源实例回放。
6. 已完成：补安全边界测试：无本地在线收件人不 claim、不 delivered，非法 objectKey/chunk 元数据不下发，非 filesystem store 不消费。
7. 已完成：补 TTL/治理测试：未 delivered 对象超过 ObjectStore TTL 后可被启动清理删除，源实例离线附件队列仍保留并可在收件人回源实例登录后回放。
8. 已完成：补 `redis_large_file_route` 结构化日志，覆盖 offer/claim/delivered/failed/cleanup 的结果、原因和关键维度。
9. 已完成：补不完整 delivered 回执保留兜底测试，确保 confirmedBytes 不足时不会清理离线队列或对象。
10. 已完成：补 delivered 丢失后的对账任务设计，明确 receipt 匹配条件、对象 TTL 后队列保留策略和误删保护。
11. 已完成：把 filesystem helper 抽象到最小 `ObjectStore` 接口，覆盖写入、校验、读取、删除和 TTL 清理，并让远端下发改用通用 `QIODevice` 读取对象。
12. 已完成：新增 ObjectStore 工厂边界，统一处理默认 filesystem、缺根目录和未支持后端错误，让服务端通过配置创建后端。
13. 已完成：补 S3/MinIO 后端配置、凭据/TLS 边界、失败回退和测试替身设计，不一次引入完整云存储依赖。
14. 已完成：补 `InMemoryObjectStore` 测试替身并复用 ObjectStore 契约测试，覆盖写入、读取、删除、size/hash 不一致和 TTL no-op 行为。
15. 已完成：补 S3 配置校验骨架和测试，覆盖 endpoint、bucket、access key、secret key、prefix、TLS flag、缺配置错误和错误信息不泄露凭据。
16. 已完成：补 `S3ObjectStore` 薄适配占位类和 fail-closed 测试，真实网络上传/下载未接入前仍不发布可用后端。
17. 已完成：补 S3 path-style URL 纯函数和 Signature V4/Qt Network 实现计划，明确 PUT/GET/HEAD/DELETE、payload hash、TLS 和日志脱敏边界。
18. 已完成：补 AWS Signature V4 纯函数和固定测试向量，不联网、不接真实 S3。
19. 已完成：补 Qt Network S3 请求构造测试，验证方法、URL、host、x-amz-date、x-amz-content-sha256、Authorization、空 region 默认值、非法 objectKey 拒绝和 TLS 校验开关边界，仍不连接真实 S3。
20. 已完成：补可选 MinIO 手动验证脚本，覆盖 bucket 创建和对象 PUT/HEAD/GET/DELETE smoke，不把真实 S3 作为默认 CTest 前置条件。
21. 已完成：收紧 S3 对象请求方法边界，签名请求只允许第一阶段 `PUT`、`GET`、`HEAD`、`DELETE`，拒绝 `POST` 等非目标方法。
22. 已完成：补 S3 HTTP 状态分类纯函数，区分成功、对象不存在、凭据/权限错误、可重试状态、客户端错误、服务端错误和未知状态，为真实网络请求失败回退做准备。
23. 已完成：补 `QTNETWORKCHAT_OBJECT_S3_TIMEOUT_MS` 配置解析，默认 30 秒并限制在 1 秒到 5 分钟，避免后续真实 S3 请求无限挂起。
24. 已完成：补可选 `QTNETWORKCHAT_OBJECT_S3_SESSION_TOKEN` 解析和 `x-amz-security-token` 签名头，支持后续临时凭据接入且不进入 Redis/日志/离线队列。
25. 已完成：`s3SignedObjectRequest()` 把有界超时写入 `QNetworkRequest::transferTimeout()`，真实网络执行可直接继承该超时边界。
26. 已完成：补不联网的 S3 请求执行结果结构和错误脱敏 helper，统一 HTTP 状态码、网络错误、超时、TLS/权限失败到 `S3HttpResult`/对象路由失败语义，避免真实网络接入后错误路径发散。
27. 已完成：补注入式 `HEAD`/`DELETE` 执行边界，`validateObject()` 可通过 HEAD 响应校验 Content-Length 和 `x-amz-meta-sha256`，`removeObject()` 可通过 DELETE 成功结果删除；默认构造与工厂仍 fail-closed，CTest 不依赖真实 S3/MinIO。
28. 已完成：补真实 Qt Network 请求执行器薄层，继续复用签名请求、结果归一化和错误脱敏；默认测试只覆盖非法请求在网络 I/O 前 fail-closed，不把真实 S3/MinIO 作为默认 CTest 前置条件。
29. 已完成：补注入式 `PUT`/`GET` 上传下载边界，`writeObject()` 生成安全 objectKey、本地计算标准 SHA-256 并通过 PUT 上传，`openObject()` 通过 GET 返回只读设备；PUT/GET 失败会 fail-closed 且错误脱敏，默认构造与工厂仍不发布真实后端。
30. 已完成：补 GET 后 size/hash 校验整合，`validateObject()` 在 HEAD 快速检查后读取 GET 响应体计算标准 SHA-256，缺 HEAD hash 时仍可用 body hash 校验，并明确不信任 ETag。
31. 已完成：补真实后端发布 gating，`createObjectStore(s3)` 默认仍 fail-closed；只有配置完整且 `QTNETWORKCHAT_OBJECT_S3_ENABLE=1` 时才创建带 Qt Network 执行器的 S3 后端，错误信息不泄露凭据。
32. 已完成：补 S3 失败 reason 聚合 helper，覆盖 timeout、network、tls、auth、not_found、retryable、client、server、unknown、size 和 hash，helper 只返回固定字符串，不携带 endpoint、bucket、凭据或 Authorization。
33. 已完成：补 delivered receipt 只读对账脚本，复用 cleaned/retained 固定 reason 语义评估 receipt 与源实例兜底摘要，默认不连接外部服务、不修改任何队列，并对输入做敏感字段扫描。
34. 已完成：把 S3 validation reason helper 接入服务端跨实例大文件失败路径，远端对象校验失败发布固定 reason 桶，避免把底层错误文本写入 `large_file_failed.reason` 或结构化日志。
35. 已完成：补 S3 真实后端显式启用后的 MinIO 手动运行说明，明确先跑 smoke、再设置 `QTNETWORKCHAT_OBJECT_STORE=s3` 和 `QTNETWORKCHAT_OBJECT_S3_ENABLE=1`，并强调失败回退与日志脱敏边界。
36. 已完成：补服务端大文件对象路由日志字段扩展，发布、校验、写入、删除和兜底路径会输出 `storeType` 与 `operation` 等安全维度，继续避免 endpoint、bucket、对象 URL、凭据和 Authorization 进入日志、Redis 事件或离线队列。
37. 已完成：补真实 S3/MinIO 启用路径的人工验收清单，明确 smoke、双实例投递、失败注入、离线兜底、回源回放、脱敏检查和 delivered receipt 只读对账演练步骤，仍不把真实 S3/MinIO 纳入默认 CTest。
38. 已完成：补 `redis_large_file_route` 日志聚合和脱敏扫描辅助脚本，人工验收时可统计事件、结果、reason、storeType 和 operation，并对敏感字段泄露 fail-fast；脚本已支持未来 `delivered_reconcile` 只读日志事件的 cleaned/retained 聚合。
39. 已完成：扩展日志分析脚本的 delivered/failed 对账候选摘要，按 `transferId/objectKey/receiverId` 聚合 delivered cleanup、retained fallback 和缺失 cleanup 线索，不做任何自动删除。
40. 已完成：补 S3 失败回退演练可选场景脚本，列出 network、auth、missing-object 和 receiver-disconnect 的注入方式、预期固定 reason、日志事件与兜底检查点，并可接入日志分析器。
41. 已完成：补 delivered 对账脚本的 `-EmitRouteLog` 输出，可生成只读 `redis_large_file_route event=delivered_reconcile` 行并复用日志分析器聚合 cleaned/retained 候选。
42. 已完成：补服务端 `delivered_reconcile` 只读日志事件和集成测试，源实例收到 delivered 回执时先输出 cleaned/retained 固定 reason，再沿用原有 cleanup 行为。
43. 已完成：补离线兜底摘要到 fallback 对账输入的只读导出脚本，便于把源实例仍保留的 objectStoreKey 记录交给 delivered receipt 对账，不修改队列或对象。
44. 已完成：补 delivered receipt/fallback/reconcile 一键只读编排脚本，落盘中间摘要和对账日志，方便人工验收复现。
45. 已完成：补 delivered 对账脱敏样例生成脚本，可生成样例 route log/离线队列摘要并直接运行一键只读对账链路。
46. 已完成：补 delivered receipt 摘要持久化原型，源实例可通过 `QTNETWORKCHAT_DELIVERED_RECEIPT_DIR` 显式追加脱敏 JSONL receipt 摘要。
47. 已完成：一键只读对账脚本支持 `-ReceiptPath` 直接消费持久化 `delivered-receipts.jsonl`，也保留 `-RouteLogPath` 日志导出路径。
48. 已完成：补 receipt 摘要轮转/压缩脚本，可按保留条数和天数归档旧 receipt，且继续执行敏感字段扫描。
49. 已完成：把 receipt 轮转纳入脱敏样例脚本，`-RunRotate` 可一键演练归档压缩路径。
50. 已完成：轮转脚本支持 `-SummaryPath` 落盘机器可读摘要，便于定时任务采集 archived/retained/sensitiveHits。
51. 已完成：轮转脚本支持 `QTNETWORKCHAT_DELIVERED_RECEIPT_*` 环境变量默认值，便于 Windows 计划任务统一配置保留策略。
52. 下一步：转向下一条 Redis/S3 治理可观测性小切片，或补轮转摘要告警阈值说明，继续保持默认 CTest 不依赖真实 S3/MinIO。

## 当前保护边界

- 已有代码会拒绝发布编码后超过 1 MB 的 Redis message event。
- 已有测试覆盖大文件和编码后超限文件不经 Pub/Sub，并回落源实例离线队列。
- 已有测试覆盖源实例为大文件和编码超限文件发布小体积 `large_file_offer`，且 offer 指向对象的 size/hash 与原始附件一致。
- 已有测试覆盖远端实例仅在本地在线收件人存在时认领 `large_file_offer`，并从 filesystem ObjectStore 校验后分片下发给客户端。
- 已有测试覆盖远端完整 ACK 后发布 `large_file_delivered`，源实例清理对应对象并避免收件人回源实例后重复收到已跨实例投递的大文件。
- 已有测试覆盖对象缺失时远端发布 `large_file_failed` 且不 claim、不下发，并覆盖源实例收到失败事件后继续保留对象和离线兜底、后续可回源实例回放。
- 已有测试覆盖非法 objectKey/分片元数据不下发、非 filesystem store 不消费，以及不在线收件人不 claim、不 delivered。
- 已有测试覆盖未 delivered 对象过期后由 ObjectStore TTL 清理，同时离线附件兜底队列仍可回放。
- 已有结构化日志覆盖跨实例大文件 offer/claim/delivered/failed/cleanup，可按 `event/result/reason` 聚合治理指标。
- 已有测试覆盖不完整 delivered 回执不会清理源实例兜底。
- 已有 delivered 丢失对账任务设计，强调只有完整 receipt 匹配才能清队列，对象 TTL 清理不等同于投递成功。
- 已有最小 `ObjectStore` 接口和工厂边界，filesystem 后端仍保留安全 objectKey 和本地路径解析能力，服务端远端下发已通过通用读取接口消费对象。
- 已有 S3/MinIO 后端配置和安全边界设计，明确凭据不进日志/Redis/离线队列、TLS 默认校验、ETag 不作为 SHA-256 依据，以及失败时保留离线兜底。
- 已有测试专用 `InMemoryObjectStore` 契约替身，覆盖通用 ObjectStore 行为和 TTL no-op 边界。
- 已有 S3 配置校验骨架，覆盖 endpoint/bucket/凭据/session token/prefix/TLS/请求超时/显式启用开关解析和错误脱敏；真实后端默认保持关闭。
- 已有 `S3ObjectStore` 薄适配类，默认构造和工厂未显式启用时 fail-closed 且不泄露凭据；测试注入执行器可覆盖 PUT/GET/HEAD/DELETE 语义。
- 已有 S3 path-style URL 生成、Signature V4 纯函数、固定 AWS 测试向量、不联网 Qt Network 请求构造测试、对象方法白名单、transfer timeout、HTTP 状态分类、请求结果归一化、错误脱敏、失败 reason 聚合、注入式 PUT/GET/HEAD/DELETE 边界、GET 响应体 size/hash 校验、真实 Qt Network 执行器薄层和显式发布 gating，真实后端默认关闭。
- 后续进入真实 S3/MinIO 治理闭环时，优先转向下一条 Redis/S3 治理可观测性小切片，或补轮转摘要告警阈值说明，并保持 fail-closed 和离线兜底安全边界。
