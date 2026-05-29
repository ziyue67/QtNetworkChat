# S3/MinIO 手动验收清单

本文用于验证 `QTNETWORKCHAT_OBJECT_STORE=s3` 和 `QTNETWORKCHAT_OBJECT_S3_ENABLE=1` 显式启用后的真实网络路径。该流程是可选人工验收，不属于默认 CTest 或 CI 前置条件。

## 前置条件

- `cmake --build build-qt6-mingw` 和 `ctest --test-dir build-qt6-mingw --output-on-failure` 已在本地通过。
- Redis 可被两个服务端实例连接。
- MinIO 或 S3 兼容服务可用，并已准备专用测试 bucket、access key、secret key 和可清理的 prefix。
- 测试账号至少包含一个发送者和一个接收者；接收者只登录远端服务端实例，确保触发跨实例路由。

## 1. 先跑对象存储 smoke

优先使用仓库脚本验证 SigV4、bucket 和对象 PUT/HEAD/GET/DELETE：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/minio-s3-smoke.ps1 `
  -Endpoint "http://127.0.0.1:9000" `
  -Bucket "qtchat-large-files" `
  -Region "us-east-1" `
  -AccessKey "<access-key>" `
  -SecretKey "<secret-key>" `
  -Prefix "qtchat/manual-acceptance"
```

如果 MinIO 已由外部环境启动，追加 `-SkipContainer`。脚本通过后再启动 QtNetworkChat 服务端；脚本失败时不要启用真实后端。

## 2. 启动 Redis 和两个服务端实例

两个服务端实例都设置相同的 Redis 与 S3/MinIO 配置，并使用不同端口：

```powershell
$env:QTNETWORKCHAT_REDIS_HOST = "127.0.0.1"
$env:QTNETWORKCHAT_REDIS_PORT = "6379"
$env:QTNETWORKCHAT_REDIS_ENABLED = "1"
$env:QTNETWORKCHAT_LARGE_FILE_ROUTING = "1"

$env:QTNETWORKCHAT_OBJECT_STORE = "s3"
$env:QTNETWORKCHAT_OBJECT_S3_ENABLE = "1"
$env:QTNETWORKCHAT_OBJECT_S3_ENDPOINT = "http://127.0.0.1:9000"
$env:QTNETWORKCHAT_OBJECT_S3_BUCKET = "qtchat-large-files"
$env:QTNETWORKCHAT_OBJECT_S3_REGION = "us-east-1"
$env:QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY = "<access-key>"
$env:QTNETWORKCHAT_OBJECT_S3_SECRET_KEY = "<secret-key>"
$env:QTNETWORKCHAT_OBJECT_S3_PREFIX = "qtchat/manual-acceptance"
$env:QTNETWORKCHAT_OBJECT_S3_TIMEOUT_MS = "30000"
```

TLS 使用自签名 MinIO 时，优先把证书加入本机信任链。只有本机临时调试时才设置 `QTNETWORKCHAT_OBJECT_S3_TLS_VERIFY=0`，并确认日志只出现 TLS 校验关闭的 warning，不输出证书、endpoint 或凭据值。

## 3. 成功投递路径

1. 在源实例登录发送者，在远端实例登录接收者。
2. 发送一个原始大小大于 `1 MB` 的文件，或一个 base64/JSON 编码后会超过 Redis Pub/Sub 上限的文件。
3. 确认 Redis 控制事件只包含小体积元数据：`large_file_offer`、`large_file_claim`、`large_file_delivered`。
4. 确认接收端收到完整文件，SHA-256 与发送端一致。
5. 确认源实例在收到完整 delivered 回执后清理本地离线兜底队列，并向 S3/MinIO 发起对象删除。
6. 确认 `redis_large_file_route` 日志出现 `offer`、`claim`、`delivered`、`delivered_cleanup`，并包含 `storeType=s3`、`operation=publish|delete`、`objectKey`、`receiverId`、`transferId`、`bytes` 等逻辑字段。

## 4. 失败回退路径

任选一个失败注入方式，每次只改一个变量，便于定位：

- 停止 MinIO 或断开网络，验证上传、HEAD、GET 或 DELETE 失败时固定 reason 聚合为 `timeout`、`network`、`retryable`、`server` 或 `unknown`。
- 把 access key 或 secret key 改为无效值，验证 reason 聚合为 `auth`。
- 删除对象或改动对象内容，验证远端发布 `large_file_failed`，reason 聚合为 `not_found`、`size` 或 `hash`。
- 断开接收端客户端，验证源实例保留离线附件兜底，后续接收者回源实例登录时仍可回放。

失败场景都必须满足：

- 不向客户端下发不完整文件。
- 源实例离线附件队列和本地附件兜底仍保留。
- `large_file_failed.reason` 只使用固定 reason 桶或固定协议原因。
- 不因为对象 TTL 清理而删除离线附件队列。

## 5. 脱敏检查

检查服务端日志、Redis Pub/Sub payload、离线队列 JSONL/SQLite payload。允许出现：

- `objectKey`
- `transferId`
- `receiverId`
- `fileName`
- `fileHash`
- `storeType`
- `operation`
- 固定 `reason`

不得出现：

- endpoint
- bucket
- object URL
- access key
- secret key
- session token
- `Authorization`
- `Credential`
- `Signature`

服务端日志可用辅助脚本做聚合和敏感字段扫描：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/analyze-large-file-route-logs.ps1 `
  -Path ".\logs\source-server.log", ".\logs\remote-server.log"
```

脚本只读取日志，不连接 Redis 或 S3/MinIO；它会统计 `event/result/reason/storeType/operation`，并按 `transferId/objectKey/receiverId` 输出 delivered cleanup 与 failed fallback 的对账候选摘要。脚本在 `redis_large_file_route` 行里发现 endpoint、bucket、object URL、access key、secret key、session token、Authorization、Credential 或 Signature 时返回失败。

## 6. 验收结论记录

建议记录以下信息即可，不记录凭据：

```text
date:
commit:
objectStore: s3
prefix: <logical-prefix-only>
success path: passed|failed
fallback path: passed|failed
log redaction: passed|failed
notes:
```

若任一失败路径没有保留离线兜底，或者日志/Redis/离线队列出现敏感配置，应立即关闭 `QTNETWORKCHAT_OBJECT_S3_ENABLE`，回退到默认 fail-closed 状态。
