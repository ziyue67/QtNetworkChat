# QtNetworkChat

QtNetworkChat is a Qt6/C++17 (Qt5-compatible) desktop network chat application with a QQNT-style UI. It combines a local TCP chat server, optional Redis-backed cross-instance presence and routing, local SQLite account/history storage, resumable file transfer, and an end-to-end (E2E) encryption evidence layer, together with PostgreSQL migration tooling and operational validation scripts.

## Features

- **QQNT-style desktop UI** built with Qt Widgets (light/dark themes via `ThemeManager`).
- **Embedded TCP chat server** — each app instance can host a local service on port `8888`, or join an existing one, enabling two-instance testing on one machine.
- **Redis cross-instance routing/presence** (optional) for message routing and online presence across multiple server instances.
- **Local SQLite storage** for accounts, login credentials, and chat history.
- **Resumable file transfer** with chunked send, resume, cancel, and acknowledgement.
- **E2E encryption evidence layer** — a compiled draft backend plus an optional, review-gated OpenSSL production adapter (disabled by default, fails closed).
- **Operational tooling** — SQLite→PostgreSQL migration, large-file governance, and release validation scripts.

## Requirements

- **Qt 6.8.3** (`mingw_64`) or a Qt5-compatible install.
- **Qt-matched MinGW toolchain** — builds must use `D:/Qt/Tools/mingw1310_64/bin/g++.exe`. Do **not** mix with msys2/ucrt64: ABI mismatches cause tests to link but crash at exit (`0xc0000374`, heap corruption) or the linker to fail with no diagnostic.
- **CMake 3.16+** and **Ninja**.
- **Redis** (optional, for cross-instance routing) reachable at `127.0.0.1:6379`.

## Quick Start

Build the desktop application from the repository root:

```powershell
cmake --build build-qt6-mingw --target QtNetworkChat
```

Launch the app with Redis (starts Redis if it is not already running):

```powershell
scripts/start-with-redis.ps1
```

The first window hosts a local chat service on port `8888` and lets you log in or register. Open a second instance to join the same service as another account and test messaging between them.

### Configuring a fresh build directory

```powershell
cmake -S . -B build-qt6-mingw -G Ninja `
  -DCMAKE_CXX_COMPILER=D:/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/mingw_64
```

## Runtime Configuration

The app and server read configuration from environment variables (see `src/main.cpp`, `src/redisclient.cpp`):

| Variable | Default | Purpose |
| --- | --- | --- |
| `QTNETWORKCHAT_REDIS` | off | Set to `1` to enable Redis-backed routing |
| `QTNETWORKCHAT_REDIS_HOST` | `127.0.0.1` | Redis host |
| `QTNETWORKCHAT_REDIS_PORT` | `6379` | Redis port |
| `QTNETWORKCHAT_REDIS_PREFIX` | `qtchat` | Key prefix |
| `QTNETWORKCHAT_REDIS_PASSWORD` | — | Redis password (if required) |

When a reachable local Redis is detected, the desktop app auto-enables Redis mode and clears the `stop-writes-on-bgsave-error` flag so a MISCONF state does not block the embedded server. Account data is always persisted in SQLite regardless of Redis.

## Build Targets

All targets are defined in `CMakeLists.txt`:

- **`QtNetworkChat`** — the GUI desktop app (entry: `src/main.cpp`).
- **`qqnt_server`** — headless TCP + Redis chat server (`tools/qqnt_server.cpp`).
- **`qqnt_engine`** — headless NDJSON IPC engine (`tools/qqnt_engine.cpp`) speaking the protocol in `docs/qqnt-ipcv1.md` over stdin/stdout.
- **`sqlite_to_postgres_migrator`** — SQLite→PostgreSQL migration tool.
- **`e2e_rollout_observability_exporter`** — E2E rollout evidence exporter.

## Architecture

Headers live in `include/`, sources in `src/` (with mirrored subdirectories `views/`, `windows/`, `dialogs/`, `widgets/`, `theme/`).

- **Networking core** — `server.cpp` / `client.cpp` (TCP), `redisclient.cpp` + `qqnt_redis_service.cpp` (Redis routing/presence), `heartbeatmonitor.cpp`, `message.cpp` (wire serialization).
- **IPC engine** — `qqnt_client_bridge.cpp`, `qqnt_engine_command_router.cpp` (NDJSON command routing/validation), `qqnt_backend_service.cpp` (local/screenshot/file backend).
- **Persistence** — `clientstorage.cpp`, `logincredentialstore.cpp`, `historyservice.cpp` / `historymetadata.cpp` / `historyattachmentparser.cpp`, `objectstore.cpp` (SQLite; a `qsqlite.dll` plugin is copied into the output `sqldrivers/`).
- **File transfer** — `transfermanager.cpp`, `filetransferstatus.cpp`, `localfilemanager.cpp`, `transferchatitemrenderer.cpp` (chunked transfer with resume/cancel/ack).
- **E2E crypto** — `e2eenvelope.cpp` + `e2e_openssl_provider.cpp`. The compiled draft backend is `draft-qt-hmac-stream-v1`; the OpenSSL production adapter is gated behind CMake options `QTNETWORKCHAT_E2E_ENABLE_PRODUCTION_CRYPTO` / `QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER` (both default OFF, fail closed). Provider contract: `include/qtnetworkchat_e2e_provider_api.h`.
- **UI (Qt Widgets)** — `mainwindow.cpp` is the central orchestrator holding the real backend wiring. UI is organized into `views/`, `windows/`, `dialogs/`, `widgets/`, and `theme/`. MainWindow responsibilities are split into manager classes (`chatsessionmanager`, `composermanager`, `windowstatemanager`, `friendmanager`, `groupmanager`, `notificationpanelmanager`, `chatcontextmanager`).

QSS stylesheets live in `ui/style-qqnt.qss` / `ui/style-qqnt-dark.qss` and are copied next to the executable at build time. Styling is applied dynamically via `ThemeManager` rather than hardcoded colors.

### Protocol source of truth

`docs/qqnt-ipcv1.md` is the single source of truth for the backend IPC protocol. Update it **before** changing any command, event, or field in the C++ implementation. The `QQNTProtocolDrift` test fails if the docs, C++ fixtures (`tests/fixtures/`), router helpers, and expectations disagree on the protocol version or payload shapes.

## Build Systems

CMake (`CMakeLists.txt`) is the primary build system. A qmake project (`QtNetworkChat.pro`) is maintained in parallel and must be kept in sync when sources are added or removed.

## Testing

The project uses CTest (~101 tests). CTest test names differ from executable names — see the `add_test` entries in `CMakeLists.txt`.

Run the focused validation suite:

```powershell
ctest --test-dir build-qt6-mingw -R "(QtNetworkChatExecutableExists|MessageSerializationRoundTrip|RedisServerReadiness|RedisStartupRequirement|RedisPresenceCommandFlow|RedisCrossInstanceChatRouting|E2EPrivateMessageDelivery)" --output-on-failure
```

Run a single test by name:

```powershell
ctest --test-dir build-tests-qt -R QQNTEngineSmoke --output-on-failure
```

The full suite exceeds a typical 5-minute command timeout; run it in numbered slices:

```powershell
ctest --test-dir build-tests-qt -I 1,25 --output-on-failure
ctest --test-dir build-tests-qt -I 26,50 --output-on-failure
ctest --test-dir build-tests-qt -I 51,75 --output-on-failure
ctest --test-dir build-tests-qt -I 76,101 --output-on-failure
```

Backend/app validation is scoped to C++ (`QtNetworkChat`, server/engine, Redis/service behavior), protocol fixtures in `tests/fixtures/`, and packaging scripts.

## Operational Scripts

Operational PowerShell scripts live in `scripts/`, including:

- `start-with-redis.ps1`, `start-redis.ps1` — launch the app / Redis.
- `migrate-sqlite-to-postgres.ps1`, `start-local-postgres.ps1` — PostgreSQL migration and local setup.
- `run-large-file-governance.ps1` and related — large-file governance workflows.
- `package-*.ps1` — release evidence and delivery packaging.

## Documentation Index

- `docs/qqnt-ipcv1.md` — backend IPC protocol (source of truth).
- `docs/testing-coverage.md` — validation lanes summary.
- `docs/automation-status.md` — automation and closeout state.
- `docs/postgresql-operations.md` — PostgreSQL operations and acceptance follow-up.
- `docs/large-file-governance.md` — large-file governance status.
- `docs/e2e-hardening-status.md` — E2E hardening status.
- `docs/release-closeout.md` — release closeout scope and handoff state.

## Repository Scope

This repository keeps the QtNetworkChat desktop application and shared infrastructure. Historical rewrite artifacts are intentionally not part of this repository scope.

## License

Copyright (C) 2026 ziyue67

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version. See the [LICENSE](LICENSE) file for the full text.

Because this is an AGPL-3.0 project, network users who interact with a modified
version must be offered access to its corresponding source code.
