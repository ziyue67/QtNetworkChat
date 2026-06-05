# Testing Coverage

QtNetworkChat uses CTest as the release-level local verification surface. The default suite includes the executable smoke check, protocol serialization, E2E fail-closed behavior, file transfer retry/resume/cancel, offline attachment replay, Redis routing, object store governance, PostgreSQL plans, automation status, and Windows packaging checks.

## Core Coverage

- Message JSON round-trip and executable presence.
- Account password KDF migration, legacy hash login compatibility, failed-login non-upgrade, and local credential storage without plaintext passwords.
- TLS certificate SHA-256 pinning and fixed failure diagnostics.
- E2E envelope/key agreement serialization, encrypted private text/file behavior, trust pin recovery, rotation gates, history metadata, production backend status, and production adapter fail-closed evidence.
- HistoryService local persistence for SQLite save/load, encrypted history labels, date filtering, export formatting, clearing, sanitized paths, and legacy text fallback/import.
- Group member update protocol, public/private group snapshots, private group creation/invitation/removal, private scoped messages/files, group audit snapshots, rejected-operation reason audit, owner/admin permission boundaries, removed-member read-only history, history visibility fields, and group send/file permission fields.

## File And Routing Coverage

- File chunk ACK progress, duplicate ACK de-duplication, invalid chunk metadata rejection, hash mismatch cleanup, forged sender rejection, cancel cleanup, retry after transient failure, and resume from the earliest missing chunk.
- Offline attachment quota, TTL cleanup, bad metadata cleanup, interrupted replay retention, partial ACK resume, stale resume fallback, and full-confirmation cleanup.
- Redis RESP, presence command flow, Pub/Sub route validation, cross-instance chat routing, E2E control routing, large-file object offer/claim/delivered/failed evidence, and Redis unavailable fallback.
- Filesystem object store path safety, S3 SigV4 pure functions, request classification, timeout configuration, injected request execution, response size/hash validation, and delivered receipt governance.

## Automation Coverage

- Automation status generation, task history, task acknowledgement, database health tasks, PostgreSQL release evidence, large-file governance tasks, governance dashboard/status/report generation, and README information architecture checks.
- Windows package manifest validation for versioned output directories, ZIP naming, manifest fields, runtime dependency checks, and packaged README inclusion.

Run locally with:

```powershell
D:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build-qt6-mingw --output-on-failure
```
