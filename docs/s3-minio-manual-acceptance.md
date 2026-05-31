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
$env:QTNETWORKCHAT_DELIVERED_RECEIPT_DIR = ".\logs\delivered-receipts"
```

TLS 使用自签名 MinIO 时，优先把证书加入本机信任链。只有本机临时调试时才设置 `QTNETWORKCHAT_OBJECT_S3_TLS_VERIFY=0`，并确认日志只出现 TLS 校验关闭的 warning，不输出证书、endpoint 或凭据值。

## 3. 成功投递路径

1. 在源实例登录发送者，在远端实例登录接收者。
2. 发送一个原始大小大于 `1 MB` 的文件，或一个 base64/JSON 编码后会超过 Redis Pub/Sub 上限的文件。
3. 确认 Redis 控制事件只包含小体积元数据：`large_file_offer`、`large_file_claim`、`large_file_delivered`。
4. 确认接收端收到完整文件，SHA-256 与发送端一致。
5. 确认源实例在收到完整 delivered 回执后清理本地离线兜底队列，并向 S3/MinIO 发起对象删除。
6. 确认 `large_file_offer` 元数据包含 `storeType=s3`，远端实例也配置为 `QTNETWORKCHAT_OBJECT_STORE=s3` 且显式启用后才会 claim；如果远端仍是 filesystem 或 storeType 不支持，应发布固定 reason 的 `large_file_failed`，并且不出现 claim/delivered。
7. 确认 `redis_large_file_route` 日志出现 `offer`、`claim`、`delivered`、`delivered_cleanup`，并包含 `storeType=s3`、`operation=publish|delete`、`objectKey`、`receiverId`、`transferId`、`bytes` 等逻辑字段；若下发阶段失败，应出现 `event=offer_delivery operation=deliver` 和固定 reason。

## 4. 失败回退路径

任选一个失败注入方式，每次只改一个变量，便于定位：

- 停止 MinIO 或断开网络，验证上传、HEAD、GET 或 DELETE 失败时固定 reason 聚合为 `timeout`、`network`、`retryable`、`server` 或 `unknown`；HEAD/GET 校验失败不应退回泛化的 `validation_error`，远端读取对象失败也应输出 `operation=read` 的 `offer_read` route log，并使用 S3 GET/open 的固定 reason，而不是泛化的 `object-open-failed`；源实例 delivered cleanup 或队列持久化回滚删除对象失败时应输出 `event=object_delete operation=delete`，reason 仍为固定桶且不影响离线兜底判定。
- 把 access key 或 secret key 改为无效值，验证 reason 聚合为 `auth`。
- 删除对象或改动对象内容，验证远端发布 `large_file_failed`，reason 聚合为 `not_found`、`size` 或 `hash`。
- 断开接收端客户端，或让测试客户端拒绝分片 ACK，验证远端发布 `large_file_failed`，并输出 `event=offer_delivery operation=deliver`；reason 只能是 `receiver-disconnected`、`chunk-rejected`、`chunk-ack-timeout`、`object-read-failed` 或 `object-seek-failed`。`large_file_failed.reason` 可保留面向协议的拒绝说明，但 route log 必须只保留固定 reason 桶，源实例离线附件兜底仍保留，后续接收者回源实例登录时仍可回放。

失败场景都必须满足：

- 不向客户端下发不完整文件。
- 源实例离线附件队列和本地附件兜底仍保留。
- `large_file_failed.reason` 只使用固定 reason 桶或固定协议原因。
- 不因为对象 TTL 清理而删除离线附件队列。

可先用只读辅助脚本列出场景、预期 reason 和兜底检查点：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/s3-failure-drill.ps1 -Scenario all
```

如果已经收集了服务端日志，也可以追加 `-LogPath ".\logs\source-server.log", ".\logs\remote-server.log"`，脚本会调用日志分析器做脱敏扫描和 delivered/failed 对账候选摘要。该脚本不连接 Redis、S3 或 MinIO，也不会修改离线队列。

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
  -Path ".\logs\source-server.log", ".\logs\remote-server.log" `
  -SummaryPath ".\logs\large-file-route-summary.json"
```

脚本只读取日志，不连接 Redis 或 S3/MinIO；它会统计 `event/result/reason/storeType/operation`，并按 `transferId/objectKey/receiverId` 输出 delivered cleanup、未来 `delivered_reconcile` 只读事件与 failed fallback 的对账候选摘要。`-SummaryPath` 会额外写出机器可读 JSON，包含 `failedFallbackRetained`、`failedWithoutFallback`、`deliveredWithoutCleanup`、`reasonCounts` 和 `sensitiveHits` 等字段，便于后续趋势或阈值告警。脚本在 `redis_large_file_route` 行里发现 endpoint、bucket、object URL、access key、secret key、session token、Authorization、Credential 或 Signature 时返回失败。

如果需要把 summary 接入计划任务或监控，可再运行只读阈值分析：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/analyze-large-file-route-summary.ps1 `
  -SummaryPath ".\logs\large-file-route-summary.json" `
  -WarnFailedWithoutFallback 0 `
  -WarnDeliveredWithoutCleanup 0
```

该脚本只读取 summary JSON；默认任何 `sensitiveHits > 0` 都会失败，`failedWithoutFallback` 或 `deliveredWithoutCleanup` 超过传入阈值也会非零退出。追加 `-AlertSummaryPath` 时会写出统一格式的告警 JSON，包含 `kind`、`ok`、`warnings` 和 `metrics`，便于 Windows 计划任务或外部监控直接采集。

如果只需要关注真实 S3/MinIO 后端的失败 reason 分布，可直接分析 route log 中的 `storeType=s3` 行。服务端对象写入、校验、读取、删除和远端下发失败会使用固定 reason 桶，例如 `object-store-unavailable`、`timeout`、`network`、`tls`、`auth`、`not_found`、`retryable`、`client`、`server`、`unknown`、`hash`、`write_failed`、`receiver-disconnected`、`chunk-rejected` 或 `chunk-ack-timeout`；远端 GET/open 失败会以 `event=offer_read operation=read` 进入同一分析链路，源实例 DELETE/remove 失败会以 `event=object_delete operation=delete` 进入同一分析链路，下发阶段失败会以 `event=offer_delivery operation=deliver` 进入同一分析链路，不会把 endpoint、bucket、object URL、凭据或签名文本写入 route log：

默认 CTest 还包含不联网的服务端注入式 S3 替身场景：源实例模拟 PUT/write timeout 时预期出现 `object_write operation=write reason=timeout`，不发布 offer、不保留半写对象且仍可从源实例离线兜底回放；源实例以 `storeType=s3` 写入对象并发布 offer 后，远端还会模拟 TLS 校验失败、GET/open network 失败和 DELETE/remove server 失败，预期分别出现 `offer_validation operation=validate reason=tls`、`offer_read operation=read reason=network`、`object_delete operation=delete reason=server`；源实例收到 failed 后还会输出 `failed_received operation=fallback reason=tls` 并保留兜底。真实验收时可以用同一组字段核对 route log，但不需要把 endpoint、bucket 或凭据写入日志。

```powershell
powershell -ExecutionPolicy Bypass -File scripts/analyze-s3-request-results.ps1 `
  -Path ".\logs\source.log", ".\logs\remote.log" `
  -SummaryPath ".\logs\s3-request-results-summary.json" `
  -WarnTimeout 0 `
  -WarnRetryable 0 `
  -WarnAuth 0 `
  -WarnTls 0 `
  -WarnHash 0 `
  -WarnSize 0
```

该脚本只读取日志，不连接 Redis 或 S3/MinIO；它会跳过 publish/reconcile 等控制面事件，只聚合对象请求相关 operation 的固定 reason 桶、operation 和 result，遇到未知 reason 或 endpoint、bucket、object URL、access key、secret key、session token、Authorization、Credential、Signature 等敏感字段时默认失败。追加 `-AlertSummaryPath` 时同样会写出统一告警 JSON，`kind` 为 `s3-request-results`，`metrics` 包含 S3 route 行数、reason/operation/result/event:operation 计数和敏感命中数。

## 6. 可选 delivered 对账演练

如果需要离线复核“远端 receipt 是否足以清理源实例兜底”，先从安全日志、人工记录或脱敏导出的队列摘要中整理两个 JSON/JSONL 文件：

- `receipts.jsonl`：每行包含 `sourceInstanceId`、`transferId`、`receiverId`、`objectKey`、`fileHash`、`confirmedBytes`。
- `fallbacks.jsonl`：每行包含同名元数据和 `fileSize`，代表源实例仍保留的离线兜底候选。

如果启动源实例前设置了 `QTNETWORKCHAT_DELIVERED_RECEIPT_DIR`，服务端收到 `large_file_delivered` 后会在该目录追加 `delivered-receipts.jsonl`。该文件只包含逻辑 receipt 字段、`cleaned|retained` 决策和固定 reason，不包含 endpoint、bucket、object URL 或凭据；可直接作为一键对账的 `-ReceiptPath` 输入，或与 route log receipt 导出互相校验。

没有真实日志时，可先生成脱敏样例并跑完整只读链路，确认本机 PowerShell 和脚本可用：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/write-large-file-reconcile-sample.ps1 `
  -OutputDir ".\logs\sample-reconcile" `
  -RunReconcile `
  -RunRotate `
  -RunS3RequestAnalysis
```

样例只包含逻辑 `objectKey`、`fileHash`、`transferId`、`receiverId` 和字节数，会同时产生一个 `cleaned` 候选、一个 `confirmed-bytes-insufficient` 保留候选、一个用于轮转归档的旧 receipt，以及一个 `sample-s3-request-summary.json`；不包含 endpoint、bucket、object URL 或凭据。

如果已有源实例和远端实例的 `redis_large_file_route` 日志，以及源实例离线队列 JSON/JSONL 摘要，可直接运行一键只读演练：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run-large-file-delivery-reconcile.ps1 `
  -RouteLogPath ".\logs\source-server.log", ".\logs\remote-server.log" `
  -QueuePath ".\logs\offline-queue.jsonl" `
  -SourceInstanceId "source-instance-id" `
  -OutputDir ".\logs\reconcile" `
  -EmitRouteLog `
  -RunS3Analysis
```

如果已有持久化 receipt 摘要，可改用 `-ReceiptPath ".\logs\delivered-receipts\delivered-receipts.jsonl"` 代替 `-RouteLogPath`，直接复用服务端落盘摘要。

该脚本只串联 receipt 输入、fallback 导出和对账判定，会写出 `receipts.jsonl`、`fallbacks.jsonl` 和 `reconcile.log`；加上 `-RunS3Analysis` 时还会从 `-RouteLogPath` 读取 S3 对象请求结果并写出 `s3-analysis-summary.json`。该分析需要原始 route log，因此使用 `-ReceiptPath` 时不能同时启用 `-RunS3Analysis`。它不会连接 Redis、S3/MinIO，也不会修改队列、附件或对象。

持久化 receipt 文件较大时，可以先做离线轮转：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/rotate-large-file-receipts.ps1 `
  -ReceiptPath ".\logs\delivered-receipts\delivered-receipts.jsonl" `
  -KeepRecords 10000 `
  -MaxAgeDays 30 `
  -CompressArchive `
  -SummaryPath ".\logs\delivered-receipts\rotate-summary.json"
```

轮转脚本会把超出保留条数或保留天数的旧摘要写到 `archive\delivered-receipts-*.jsonl` 或 `.zip`，再重写 active `delivered-receipts.jsonl`；`-SummaryPath` 会额外写出本次轮转摘要，包含 `totalRecords`、`retainedRecords`、`archivedRecords`、`archivePath` 和 `sensitiveHits`，便于定时任务收集。它只处理脱敏 receipt 摘要，不连接 Redis、S3/MinIO，也不会清理离线队列、附件或对象。若输入中出现 endpoint、bucket、object URL、access key、secret key、session token、Authorization、Credential 或 Signature，脚本默认失败。

轮转后可分析摘要并设置简单阈值告警：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/analyze-large-file-receipt-rotation.ps1 `
  -SummaryPath ".\logs\delivered-receipts\rotate-summary.json" `
  -WarnArchivedRecords 50000 `
  -WarnRetainedRecords 200000
```

分析脚本只读取 summary JSON，汇总 retained/archived/sensitiveHits；若 `sensitiveHits > 0`、`archivedRecords > 0` 但没有 `archivePath`，或超过传入阈值，会默认以非零退出，便于计划任务或 CI 采集告警。追加 `-AlertSummaryPath` 时会写出统一告警 JSON，`kind` 为 `large-file-receipt-rotation`，便于和 route summary、S3 request result 的告警输出并排归档。

计划任务中也可以用环境变量提供默认值，命令行参数始终优先：

```powershell
$env:QTNETWORKCHAT_DELIVERED_RECEIPT_KEEP_RECORDS = "10000"
$env:QTNETWORKCHAT_DELIVERED_RECEIPT_MAX_AGE_DAYS = "30"
$env:QTNETWORKCHAT_DELIVERED_RECEIPT_ARCHIVE_DIR = ".\logs\delivered-receipts\archive"
$env:QTNETWORKCHAT_DELIVERED_RECEIPT_COMPRESS_ARCHIVE = "1"
$env:QTNETWORKCHAT_DELIVERED_RECEIPT_SUMMARY_PATH = ".\logs\delivered-receipts\rotate-summary.json"
```

如果需要把完整治理入口交给 Windows Scheduled Task，先生成 preview 并人工确认命令不含 endpoint、bucket、object URL 或凭据：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/register-large-file-governance-task.ps1 `
  -TaskName "QtNetworkChatLargeFileGovernance" `
  -Schedule Daily `
  -At "03:00" `
  -RouteLogPath ".\logs\source-server.log", ".\logs\remote-server.log" `
  -QueuePath ".\logs\offline-queue.jsonl" `
  -SourceInstanceId "source-instance-id" `
  -OutputDir ".\logs\governance" `
  -ReceiptRotationPath ".\logs\delivered-receipts\delivered-receipts.jsonl" `
  -RotationKeepRecords 10000 `
  -RotationMaxAgeDays 30 `
  -CompressRotationArchive `
  -EmitRouteLog `
  -NoFailOnWarning `
  -PackageAcceptance
```

默认不会注册系统计划任务，只会在 `OutputDir\scheduled-task` 下写出 `run-large-file-governance-task.ps1` 和 `scheduled-task-preview.json`。确认 preview 只包含本地日志、队列摘要、receipt 摘要和输出目录后，再追加 `-Register` 创建或更新 Windows Scheduled Task。该入口仍只读运行治理聚合；除显式 receipt 轮转外，不会连接 Redis、S3/MinIO，也不会清理离线队列、附件或对象。若设置 `-RotationKeepRecords`、`-RotationMaxAgeDays` 或 `-CompressRotationArchive`，必须同时提供 `-ReceiptRotationPath`，避免计划任务看似成功但实际跳过 receipt 保留策略。

也可以分步执行。先从 route log 导出 delivered receipt 输入：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/export-large-file-receipts.ps1 `
  -Path ".\logs\source-server.log", ".\logs\remote-server.log" `
  -OutputPath ".\logs\receipts.jsonl"
```

导出脚本只读取 route log 中的 `delivered` 和 `delivered_reconcile` 成功/cleaned 事件，并只写出 `sourceInstanceId`、`transferId`、`receiverId`、`objectKey`、`fileHash`、`confirmedBytes`；它不会连接 Redis、S3/MinIO，也不会修改队列、附件或对象。

再从源实例离线队列 JSON/JSONL 摘要导出对账所需的 fallback 输入：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/export-large-file-fallbacks.ps1 `
  -QueuePath ".\logs\offline-queue.jsonl" `
  -SourceInstanceId "source-instance-id" `
  -OutputPath ".\logs\fallbacks.jsonl"
```

导出脚本只读取队列摘要，筛选带 `objectStoreKey` 的大文件兜底记录，并只写出 `sourceInstanceId`、`transferId`、`receiverId`、`objectKey`、`fileHash`、`fileSize`；它不会连接 Redis、S3/MinIO，也不会修改离线队列、附件或对象。

然后运行只读对账脚本：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/reconcile-large-file-delivery.ps1 `
  -ReceiptPath ".\logs\receipts.jsonl" `
  -FallbackPath ".\logs\fallbacks.jsonl" `
  -EmitRouteLog
```

脚本会输出 `cleaned` 或 `retained` 候选及固定 reason：`cleaned`、`invalid-receipt`、`invalid-payload`、`receipt-not-matched`、`confirmed-bytes-insufficient`。加上 `-EmitRouteLog` 后还会生成只读 `redis_large_file_route event=delivered_reconcile` 行，可直接交给日志分析脚本聚合；`cleaned` 只表示摘要满足清理条件，不会删除离线队列、附件或对象，真实清理仍只能由后续显式治理任务执行。

输入摘要不得包含 endpoint、bucket、object URL、access key、secret key、session token、Authorization、Credential 或 Signature；导出和对账脚本默认发现这些字段都会失败。需要排查历史日志时可临时加 `-NoFailOnSensitive` 查看命中位置，但不能把该输出作为通过结果。

## 7. 验收结论记录

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

也可以把上述只读产物打成一个脱敏归档包，便于留存或交给后续治理任务：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-large-file-acceptance.ps1 `
  -RouteLogPath ".\logs\source-server.log", ".\logs\remote-server.log" `
  -RouteSummaryPath ".\logs\large-file-route-summary.json" `
  -S3SummaryPath ".\logs\s3-request-results-summary.json" `
  -ReconcileDir ".\logs\reconcile" `
  -RotationSummaryPath ".\logs\delivered-receipts\rotate-summary.json" `
  -NotesPath ".\logs\acceptance-notes.txt" `
  -OutputDir ".\logs\acceptance-package" `
  -PackagePath ".\logs\acceptance-package\large-file-acceptance.zip"
```

打包脚本只读取本地文件，会先扫描 endpoint、bucket、object URL、access key、secret key、session token、Authorization、Credential、Signature 等敏感字段；默认发现命中会失败且不生成通过包。归档内会包含 `manifest.json`，记录输入文件、字节数、敏感命中数和只读说明；脚本不会连接 Redis、S3/MinIO，也不会修改队列、附件或对象。

如果希望在人工验收或 Windows 计划任务里一次性完成日志聚合、S3 reason 分析、delivered 对账、receipt 轮转告警和验收包打包，可以使用治理入口脚本：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run-large-file-governance.ps1 `
  -RouteLogPath ".\logs\source-server.log", ".\logs\remote-server.log" `
  -QueuePath ".\logs\offline-queue.jsonl" `
  -SourceInstanceId "source-instance-id" `
  -ReceiptRotationPath ".\logs\delivered-receipts\delivered-receipts.jsonl" `
  -OutputDir ".\logs\governance" `
  -NotesPath ".\logs\acceptance-notes.txt" `
  -EmitRouteLog `
  -NoFailOnWarning `
  -CompressRotationArchive `
  -PackageAcceptance
```

该入口仍不连接 Redis、S3/MinIO，不修改离线队列、附件或对象；只有传入 `-ReceiptRotationPath` 时会对脱敏 receipt 摘要文件执行本地轮转。默认任何敏感字段命中都会失败；`-NoFailOnWarning` 只允许阈值告警继续产出，不会放过敏感字段。治理入口还会同时落盘 `large-file-route-alert-summary.json`、`s3-request-results-alert-summary.json` 和 `receipt-rotation-alert-summary.json`，三者都使用统一的 `kind/ok/warnings/metrics` 结构；计划任务可以只检查这些文件的 `ok` 字段和 `warnings` 列表，而不解析控制台文本。后续可用 `scripts/aggregate-governance-alerts.ps1` 汇总这些 alert summary 为 `governance-alert-overview.json`，再用 `scripts/check-governance-health.ps1` 生成 `last-health.json`；若状态不健康，可用 `scripts/notify-governance-unhealthy.ps1 -DryRun` 演练 EventLog/webhook 通知边界。上述脚本都会继续避免 endpoint、bucket、object URL、凭据和签名字段进入输出。

需要一次性归档治理总览、健康结果、关键 summary 和日志时，可在治理入口追加诊断包输出：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run-large-file-governance.ps1 `
  -RouteLogPath ".\logs\source-server.log", ".\logs\remote-server.log" `
  -QueuePath ".\logs\offline-queue.jsonl" `
  -SourceInstanceId "source-instance-id" `
  -ReceiptRotationPath ".\logs\delivered-receipts\delivered-receipts.jsonl" `
  -OutputDir ".\logs\governance" `
  -NotesPath ".\logs\acceptance-notes.txt" `
  -NoFailOnWarning `
  -WriteReport `
  -ReportPath ".\logs\governance\large-file-governance-report.md" `
  -HtmlReportPath ".\logs\governance\large-file-governance-report.html" `
  -PackageDiagnostics `
  -DiagnosticsPackagePath ".\logs\governance\large-file-governance-diagnostics.zip"
```

`scripts/write-large-file-governance-report.ps1` 可单独读取治理输出目录并生成 Markdown/HTML 运维报告；报告会汇总健康状态、alert overview、route/S3/receipt rotation/delivered reconcile 指标，并在发现 endpoint、bucket、object URL、凭据或签名字段时拒绝生成。`scripts/package-governance-diagnostics.ps1` 也可单独读取治理输出目录并生成诊断 zip；归档前会扫描敏感字段，生成 `manifest.json`，且不会连接 Redis、S3/MinIO 或修改真实队列、附件、对象。

如果只需要给计划任务、值班脚本或本地运维页面消费一个轻量入口，可生成治理 dashboard：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/write-large-file-governance-dashboard.ps1 `
  -GovernanceDir ".\logs\governance" `
  -DashboardPath ".\logs\governance\large-file-governance-dashboard.json" `
  -MarkdownPath ".\logs\governance\large-file-governance-dashboard.md"
```

dashboard JSON 使用 `qtnetworkchat-large-file-governance-dashboard-v1` 格式，汇总 `status/ok/reason/totalWarnings/alertCount`、route/S3/receipt/reconcile 核心指标、告警来源和关键产物相对路径；Markdown 版本便于人工快速查看。脚本只读取本地治理产物，发现 endpoint、bucket、object URL、access key、secret key、session token、Authorization、Credential 或 Signature 会拒绝生成。

计划任务或值班脚本只需要判断当前治理状态时，可以读取 dashboard、health 和 alert overview 生成轻量状态输出；`-FailOnUnhealthy` 会在 unhealthy 或 unknown 时返回非零退出码：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/show-large-file-governance-status.ps1 `
  -GovernanceDir ".\logs\governance" `
  -JsonPath ".\logs\governance\large-file-governance-status.json" `
  -MarkdownPath ".\logs\governance\large-file-governance-status.md" `
  -FailOnUnhealthy
```

该状态 CLI 只读取本地治理产物，不连接 Redis、S3/MinIO，不修改队列、附件、对象或 receipt；如果输入中出现 endpoint、bucket、object URL、access key、secret key、session token、Authorization、Credential 或 Signature，会拒绝输出。

如果需要在没有真实 S3/MinIO 故障窗口时演练 reason 分布和阈值告警，可生成批量失败样例并直接复用 S3 request result 分析器：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/write-s3-failure-batch-sample.ps1 `
  -OutputDir ".\logs\s3-failure-batch" `
  -CountPerReason 3 `
  -RunAnalysis `
  -NoFailOnWarning
```

该样例会生成脱敏 `s3-failure-batch-route.log`、`s3-failure-batch-summary.json` 和 `s3-failure-batch-alert-summary.json`，覆盖 timeout、network、tls、auth、retryable、server、hash、size、下发 ACK 超时、接收端断开和删除保留等固定桶；脚本只写本地样例文件，不连接 Redis、S3/MinIO，也不会修改队列、附件、对象或 receipt。

没有真实日志时，可以直接跑完整脱敏样例链路：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/write-large-file-reconcile-sample.ps1 `
  -OutputDir ".\logs\sample-governance" `
  -RunGovernance
```

该样例会生成 route log、离线队列摘要、receipt 摘要和治理输出，并串联 route 分析、S3 request 分析、delivered 对账、receipt 轮转、alert 聚合和健康检查；它不连接 Redis、S3/MinIO，也不会修改真实队列、附件或对象。

若任一失败路径没有保留离线兜底，或者日志/Redis/离线队列出现敏感配置，应立即关闭 `QTNETWORKCHAT_OBJECT_S3_ENABLE`，回退到默认 fail-closed 状态。
