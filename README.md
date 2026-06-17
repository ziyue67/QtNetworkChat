# QtNetworkChat QQNT Backend

This repository contains the QQ NT redesign backend work for QtNetworkChat. The current backend branch owns the C++ sidecars, Redis-backed server runtime, Tauri Rust bridge, IPC contract, packaging scripts, and backend validation.

## Quick Start

Build the C++ sidecars from the repository root:

```powershell
cmake --build build-qt6-mingw --target QQNTEngine QQNTServer
```

Run the focused backend validation used by this branch:

```powershell
ctest --test-dir build-qt6-mingw -R "(QQNTEngineSmoke|QQNTProtocolDrift|QQNTServerRedis|RedisServerReadiness|RedisStartupRequirement)" --output-on-failure
```

Run the Tauri Rust bridge tests:

```powershell
cd tauri-qqnt/src-tauri
cargo test
```

## Documentation Index

- `docs/qqnt-implementation-plan.md` tracks the full QQ NT redesign plan.
- `docs/qqnt-concurrent-branches-plan.md` defines the backend/frontend branch split.
- `docs/qqnt-ipcv1.md` is the IPC v1 contract shared by C++, Rust, and frontend code.
- `docs/automation-status.md` records automation and closeout state.
- `docs/testing-coverage.md` summarizes the validation lanes.
- `docs/postgresql-operations.md` records PostgreSQL operations and acceptance follow-up.
- `docs/large-file-governance.md` records large-file governance status.
- `docs/e2e-hardening-status.md` records E2E hardening status.
- `docs/release-closeout.md` records release closeout scope and handoff state.

## Branch Ownership

- Backend work happens on `codex/qqnt-backend` and should stay inside backend-owned files such as `include/qqnt_*`, `src/qqnt_*`, `tools/qqnt_*`, `tests/qqnt_*`, `scripts/`, `dev/`, `docs/qqnt-ipcv1.md`, and `tauri-qqnt/src-tauri/`.
- Frontend work happens on `codex/qqnt-frontend` and owns `tauri-qqnt/src/`.

Keep protocol changes document-first: update `docs/qqnt-ipcv1.md`, then update C++/Rust behavior and drift tests together.
