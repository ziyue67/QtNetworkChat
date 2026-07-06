# QtNetworkChat

QtNetworkChat is a Qt/C++ network chat application with Redis-backed routing, local account storage, file transfer, E2E evidence lanes, PostgreSQL migration tooling, and operational validation scripts.

## Quick Start

Build the desktop application from the repository root:

```powershell
cmake --build build-qt6-mingw --target QtNetworkChat
```

Run the focused validation suite for the remaining QtNetworkChat application and infrastructure lanes:

```powershell
ctest --test-dir build-qt6-mingw -R "(QtNetworkChatExecutableExists|MessageSerializationRoundTrip|RedisServerReadiness|RedisStartupRequirement|RedisPresenceCommandFlow|RedisCrossInstanceChatRouting|E2EPrivateMessageDelivery)" --output-on-failure
```

## Documentation Index

- `docs/automation-status.md` records automation and closeout state.
- `docs/testing-coverage.md` summarizes the validation lanes.
- `docs/postgresql-operations.md` records PostgreSQL operations and acceptance follow-up.
- `docs/large-file-governance.md` records large-file governance status.
- `docs/e2e-hardening-status.md` records E2E hardening status.
- `docs/release-closeout.md` records release closeout scope and handoff state.

## Repository Scope

This repository keeps the QtNetworkChat desktop application and shared infrastructure. Historical rewrite artifacts are intentionally not part of this repository scope.
