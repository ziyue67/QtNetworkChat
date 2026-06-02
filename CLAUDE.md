# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

QtNetworkChat is a C++17 Qt Widgets desktop LAN chat application with QQ-style login/registration, local server hosting, client login, group chat, private chat, friend search/requests, file/image transfer, chat history, offline messages, tray notifications, heartbeat/reconnect, SQLite persistence, and optional TLS transport.

The app is intentionally self-contained: one process starts a local `Server` on port `8888` when available, then creates `Client` instances that connect to `127.0.0.1:8888`. If the port is already in use, the process behaves as a client connecting to the existing local service.

## Build and test commands

Preferred CMake workflow:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows/MSVC-style CI workflow:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DQt6_DIR="$Qt6_DIR"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

If CMake cannot find Qt, pass the Qt install prefix, for example:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/mingw_64"
```

Run a single CTest test:

```bash
ctest --test-dir build -R MessageSerializationRoundTrip --output-on-failure
ctest --test-dir build -R HeartbeatMonitorTest --output-on-failure
```

qmake alternative:

```bash
qmake QtNetworkChat.pro
make
```

On Windows, use the make tool for the selected Qt Kit (`mingw32-make`, `nmake`, or `jom`).

Windows packaging:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1 -BuildDir build-qt6-mingw
```

## Dependencies and build system

- Qt 5.15+ or Qt 6.x
- Qt modules: Core, Widgets, Network, Sql
- C++17 compiler
- CMake project is authoritative for CI and tests; `QtNetworkChat.pro` is kept for Qt Creator/qmake users.
- `CMAKE_AUTOMOC`, `CMAKE_AUTOUIC`, and `CMAKE_AUTORCC` are enabled; `ui/` is in the AUTOUIC search path.

## Coding style

- 4-space indentation, braces on the same line for functions and control blocks.
- `camelCase` for variables and methods, `PascalCase` for classes and enums.
- Headers in `include/`, matching implementations in `src/`.
- Prefer Qt types already used in the codebase: `QString`, `QByteArray`, `QJsonObject`, signals/slots, Qt SQL/Network classes.
- New tests go under `tests/` and are registered with `add_test()` in `CMakeLists.txt`.

## Commit conventions

Use Conventional Commits with a type prefix: `feat:`, `fix:`, `test:`, `docs:`, `chore:`. Keep messages short and imperative. Examples: `feat: 显示文件分片接收进度`, `fix: 离线附件过期清理逻辑`.

## High-level architecture

### Entry point and application flow

`src/main.cpp` owns startup UI and mode selection. It creates a `Server` first, attempts to listen on port `8888`, then presents login/register flows. Registration and login create a `Client`, set account credentials, connect to the local server, wait synchronously for login result, and then show `MainWindow`.

`LoginDialog` is implemented inside `main.cpp`. It stores recent login data in `QStandardPaths::AppDataLocation/login_accounts.sqlite3`, falling back to `QSettings` for some values.

### Network protocol

Communication is newline-delimited JSON over `QTcpSocket`/`QSslSocket`. `Server::onClientReadyRead()` buffers socket data until `\n`, parses each JSON object, and dispatches by the string `type` field (`login`, `message`, `private`, `file`, `file_chunk`, friend events, `heartbeat`). `Client::sendJson()` follows the same newline framing.

`include/message.h` and `src/message.cpp` define `Message`, `MessageType`, and JSON round-trip serialization for chat/file payload metadata. The CTest target `MessageSerializationRoundTrip` verifies this serialization, including file hash and chunk metadata.

### Server responsibilities

`Server` in `include/server.h` / `src/server.cpp` manages connected sockets, online user mappings, account registration/login, friend events, message routing, offline delivery, file transfer forwarding, and SQLite server-side persistence.

Key state:

- `m_clients`: socket to `ChatUser`
- `m_userSockets`: user ID to socket for direct/private delivery
- `m_pendingFileTransfers`: partial inbound file chunks keyed per sender/transfer

The server persists accounts, sessions, messages, and friend events in SQLite under `QStandardPaths::AppDataLocation`; legacy account/offline text files still exist as fallback/migration paths.

### Client responsibilities

`Client` in `include/client.h` / `src/client.cpp` wraps the socket connection and exposes signal-driven APIs to the UI. It handles login state, heartbeat, reconnect attempts, friend search/request/response messages, outgoing file chunking, incoming file reassembly, and emits progress signals for transfers.

File payloads are chunked at 256 KiB, capped at 80 MiB total, with SHA-256 hash metadata and a maximum of 4096 incoming chunks.

### HeartbeatMonitor

`HeartbeatMonitor` in `include/heartbeatmonitor.h` / `src/heartbeatmonitor.cpp` tracks connection liveness via periodic heartbeat timeouts. Used by both server and client to detect stale connections.

### Main window and local client data

`MainWindow` in `include/mainwindow.h` / `src/mainwindow.cpp` owns the chat UI, models, tray icon, contact lists, local groups, unread state, history actions, file/image send flows, and all signal connections to `Client`.

Client-side data is persisted per user in `QStandardPaths::AppDataLocation/client_<userId>.sqlite3`, including chat history, friends, local groups, profile, and friend request state. Older text files such as `chat_history_<peer>.txt` and `friends_<account>.txt` are still read as legacy fallback and then migrated where possible.

Received images/files are saved under the user's Downloads folder in `QtNetworkChat/Images` and `QtNetworkChat/Files`.

### Optional Redis presence service

Redis is optional and disabled by default. When enabled, the server writes presence keys (`qtchat:presence:<userId>`) with short TTL, maintains a `qtchat:presence:users` online index, and merges Redis presence with the in-memory online list. It also supports Pub/Sub cross-instance message routing for text and small files (encoded payload ≤ 1 MiB). Large files fall back to offline attachment queue with optional ObjectStore + control-plane offer/claim/delivered flow.

Enable via environment variables before starting the server:

```bash
QTNETWORKCHAT_REDIS=1
QTNETWORKCHAT_REDIS_HOST=127.0.0.1
QTNETWORKCHAT_REDIS_PORT=6379
# Optional:
QTNETWORKCHAT_REDIS_PASSWORD=your_password
QTNETWORKCHAT_REDIS_PREFIX=qtchat
```

A convenience script `scripts/start-with-redis.ps1` starts a local Redis and launches the app with Redis enabled.

When Redis is unavailable the server falls back to in-memory presence and local-only forwarding without affecting single-instance LAN usage.

### ObjectStore for cross-instance large files

`include/objectstore.h` / `src/objectstore.cpp` defines the `ObjectStore` interface with `FilesystemObjectStore` (default) and `S3ObjectStore` (fail-closed unless explicitly enabled) implementations. Used by the Redis large-file-offer flow to store oversized file payloads that cannot travel via Pub/Sub.

Configure via environment variables:

```bash
QTNETWORKCHAT_OBJECT_STORE=filesystem          # or "s3"
QTNETWORKCHAT_OBJECT_S3_ENABLE=1               # must be explicit for S3
QTNETWORKCHAT_OBJECT_S3_ENDPOINT=http://127.0.0.1:9000
QTNETWORKCHAT_OBJECT_S3_BUCKET=qtchat-large-files
QTNETWORKCHAT_OBJECT_S3_REGION=us-east-1
QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY=qtchat-dev
QTNETWORKCHAT_OBJECT_S3_SECRET_KEY=qtchat-dev-secret
QTNETWORKCHAT_OBJECT_S3_PREFIX=qtchat/manual-smoke
QTNETWORKCHAT_OBJECT_S3_TIMEOUT_MS=30000
```

Manual S3/MinIO smoke test: `scripts/minio-s3-smoke.ps1`. Full acceptance checklist: `docs/s3-minio-manual-acceptance.md`.

### TLS mode

Transport defaults to plain TCP. Set these environment variables before starting server/client to request TLS:

```bash
QTNETWORKCHAT_TLS=1
QTNETWORKCHAT_TLS_CERT=/path/to/server.crt
QTNETWORKCHAT_TLS_KEY=/path/to/server.key
```

Clients allow self-signed certificates by default for local testing. Set `QTNETWORKCHAT_TLS_VERIFY=1` to require certificate verification. If TLS is requested but Qt/OpenSSL or certificate files are unavailable, the server falls back to plain TCP and reports that in `transportSecurityDescription()`.

### Offline attachment environment variables

| Variable | Default | Description |
|---|---|---|
| `QTNETWORKCHAT_OFFLINE_ATTACHMENT_QUOTA_MB` | 512 MB | Total disk quota for offline attachment storage |
| `QTNETWORKCHAT_OFFLINE_ATTACHMENT_TTL_DAYS` | 14 days | Expiry for offline attachments; still-referenced but expired attachments are also cleaned |
| `QTNETWORKCHAT_OFFLINE_RESUME_TTL_HOURS` | 24 hours | Trust window for `resumeUpdatedAt`; expired progress falls back to full replay |

Invalid values (empty, non-numeric, out-of-range) silently fall back to defaults.

## Tests and CI

`CMakeLists.txt` registers these CTest tests when `BUILD_TESTING` is enabled (all run via `cmake/run_qt_test.cmake` which adds Qt bin to `PATH` on Windows):

- `QtNetworkChatExecutableExists` — smoke test that the GUI executable exists and is non-empty.
- `MessageSerializationRoundTrip` — message JSON serialization round-trip.
- `ServerGroupMemberUpdateProtocol` — group member add/remove/permission protocol.
- `FileTransferCancelProtocol` — file transfer cancel and cleanup.
- `FileChunkDuplicateAckProtocol` — duplicate chunk ACK dedup.
- `FileChunkRetryProgress` — chunk retry and progress tracking.
- `OfflineAttachmentQuota` — offline attachment quota enforcement and replay.
- `RedisClientRespProtocol` — Redis RESP command encoding and response parsing.
- `ObjectStoreFilesystem` — filesystem ObjectStore key generation, validation, TTL cleanup.
- `RedisUnavailableFallback` — Redis unavailable graceful degradation.
- `RedisPresenceCommandFlow` — Redis presence write/read command flow.
- `RedisCrossInstanceChatRouting` — cross-instance chat/file routing via Pub/Sub.
- `HeartbeatMonitorTest` — heartbeat timeout tracking and liveness.
- `S3RequestResultAnalysis` — S3/MinIO request result analysis script validation.
- `RouteLogAnalysis` — route log aggregation script validation.
- `RouteSummaryAlerts` — route summary threshold alert script validation.
- `ReceiptRotationAlerts` — receipt rotation alert script validation.
- `ReconcileRunnerS3Analysis` — reconcile runner with S3 analysis integration.
- `LargeFileAcceptancePackage` — acceptance package generation validation.
- `LargeFileGovernanceRunner` — governance runner end-to-end validation.
- `LargeFileGovernanceTask` — scheduled task helper command generation and sensitive field rejection.
- `AggregateGovernanceAlerts` — governance alert aggregation script validation.
- `GovernanceHealthCheck` — governance health check script validation.
- `GovernanceSamplePipeline` — full governance sample pipeline integration test.
- `NotifyGovernanceUnhealthy` — unhealthy notification script validation.

GitHub Actions (`.github/workflows/windows-build.yml`) builds on `main` pushes and PRs using Qt 6.8.3 / MSVC 2022, then runs CTest. CI wraps the Release build in a 600-second timeout and CTest in a 900-second timeout, uploads `windows-build-diagnostics` with `build/ci-logs/`, CTest `Testing/`, and CMake logs when the job fails or is cancelled, and writes a short GitHub Step Summary for each run. Manual workflow dispatch also runs the Windows package script and uploads the zip artifact. Local automation still uses the MinGW `build-qt6-mingw` tree plus the PowerShell timeout wrappers before committing.

## Helper scripts

| Script | Purpose |
|---|---|
| `scripts/package-windows.ps1` | Build + stage + windeployqt + zip |
| `scripts/start-with-redis.ps1` | Start local Redis and launch app with Redis enabled |
| `scripts/minio-s3-smoke.ps1` | Docker-based MinIO S3 smoke test |
| `scripts/s3-failure-drill.ps1` | List S3 failure scenarios and expected fallback behavior |
| `scripts/analyze-large-file-route-logs.ps1` | Aggregate and sanitize `redis_large_file_route` structured logs |
| `scripts/analyze-large-file-route-summary.ps1` | Threshold alerts from route log summary JSON |
| `scripts/rotate-large-file-receipts.ps1` | Rotate `delivered-receipts.jsonl` by count/age |
| `scripts/analyze-large-file-receipt-rotation.ps1` | Threshold alerts from receipt rotation summary |
| `scripts/run-large-file-delivery-reconcile.ps1` | Read-only delivered receipt vs offline fallback reconciliation |
| `scripts/write-large-file-reconcile-sample.ps1` | Generate sanitized samples for reconcile/rotation testing |
| `scripts/analyze-s3-request-results.ps1` | Aggregate S3 request result reason buckets with threshold alerts |
| `scripts/export-large-file-receipts.ps1` | Export delivered receipts from route logs |
| `scripts/export-large-file-fallbacks.ps1` | Export offline fallback summaries from route logs |
| `scripts/reconcile-large-file-delivery.ps1` | Core delivered receipt vs offline fallback reconciliation logic |
| `scripts/aggregate-governance-alerts.ps1` | Aggregate multiple alert summary JSONs into single governance overview |
| `scripts/check-governance-health.ps1` | Read governance alert overview and emit healthy/unhealthy verdict for monitoring |
| `scripts/notify-governance-unhealthy.ps1` | Notify on unhealthy governance via Windows EventLog and optional webhook |

### Governance output directory structure

When `scripts/run-large-file-governance.ps1` runs with `-OutputDir <path>`, it produces the following layout:

```
<OutputDir>/
├── large-file-route-summary.json        # Route log aggregation (if RouteLogPath provided)
├── large-file-route-alert-summary.json   # Route summary threshold alerts
├── s3-request-results-summary.json       # S3 request result buckets
├── s3-request-results-alert-summary.json # S3 result threshold alerts
├── receipt-rotation-summary.json         # Receipt rotation stats (if ReceiptRotationPath provided)
├── receipt-rotation-alert-summary.json   # Rotation threshold alerts
├── governance-alert-overview.json        # Aggregated alert overview (all kinds)
├── last-health.json                      # Health check verdict (if HealthCheckPath provided)
├── route-analysis.log                    # Step stdout logs
├── route-summary-alerts.log
├── s3-request-analysis.log
├── reconcile-run.log
├── receipt-rotation.log
├── receipt-rotation-alerts.log
├── aggregate-alerts.log
├── health-check.log
├── acceptance-package.log                # (if PackageAcceptance)
├── reconcile/                            # Reconciliation output subdirectory
│   ├── reconcile-report.json
│   └── s3-analysis-summary.json
├── acceptance-package/                   # (if PackageAcceptance)
│   └── large-file-acceptance.zip
└── scheduled-task/                       # (if register-large-file-governance-task.ps1)
    ├── run-large-file-governance-task.ps1
    └── scheduled-task-preview.json
```

Retention guidance: governance outputs are ephemeral per-run artifacts. The scheduled task overwrites them each execution. For audit trails, use `-PackageAcceptance` to produce a timestamped zip, or archive `governance-alert-overview.json` and `last-health.json` externally before the next run.
