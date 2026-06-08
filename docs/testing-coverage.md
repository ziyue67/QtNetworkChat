# Testing Coverage

QtNetworkChat uses CTest as the release-level local verification surface. The default suite includes the executable smoke check, protocol serialization, E2E fail-closed behavior, file transfer retry/resume/cancel, offline attachment replay, Redis routing, object store governance, PostgreSQL plans, automation status, and Windows packaging checks.

## Core Coverage

- Message JSON round-trip and executable presence.
- ClientStorage local profile/friends/local group persistence for AppData paths, legacy file read/write, profile SQLite writes, friend request SQLite writes, and local group SQLite writes.
- Account password KDF migration, legacy hash login compatibility, failed-login non-upgrade, and local credential storage without plaintext passwords.
- FriendManager contact display state for known-user/remark/id fallback, online state, friend notification badge copy, filter matching, and relation labels.
- GroupManager group notice badge copy, local owner resolution, local owner checks, server owner/admin management checks, and local/server member role-action labels.
- TLS certificate SHA-256 pinning and fixed failure diagnostics.
- E2E envelope/key agreement serialization, encrypted private text/file behavior, trust pin recovery, rotation gates, history metadata, production backend status, production adapter fail-closed evidence, operation harness/execution-plan/invocation/slot/dispatch/callable evidence gates, provider table/preflight/call-frame/dry-run/result/decision/callback/vector/slot/path/sandbox/vector-result/invocation execution control gates, reviewed execution candidate, reviewed call handoff, reviewed operation stub boundary, reviewed callable table bridge, reviewed operation callable interface, reviewed callable runtime preflight, reviewed invocation arming, reviewed invocation execution acceptance, production data-plane bridge evidence, and public primitive execution evidence, explicit provider invocation probe vector contract/hash/classification, explicit provider round-trip execution probe, explicit public primitive execution probe, size-only execution-frame evidence, known-answer output shape evidence, per-operation vector/fixture mismatch matrix, status/material/pointer/table-blocked scope classification, output-shape mismatch fail-closed classification, and optional OpenSSL-backed runtime coverage for all eight provider operations, including session-key generation, identity-key generation, public-key derivation, agreement signing, signature verification, session derivation, payload encryption, and payload decryption plus signature/ciphertext tamper rejection. Linked OpenSSL runtime gate now verifies `selectedBackendId`, `productionReady=true`, public API provider dispatch, AES-GCM payload round trip, tamper rejection, 8/8 default reviewed handoff/data-plane/public primitive readiness, `productionAcceptance.accepted=true` / `releaseGate=production-crypto-accepted`, the client production rotation local rebind path that generates production identity material while clearing draft sessions without sensitive evidence export, and the connected-client production flow that re-announces identities, re-verifies trust pins, derives a signed production session, sends encrypted private text, and sends encrypted private file payloads on `openssl-reviewed-adapter-v1`.
- HistoryService local persistence for SQLite save/load, encrypted history labels, date filtering, export formatting, clearing, sanitized paths, and legacy text fallback/import.
- Group member update protocol, public/private group snapshots, private group creation/invitation/removal, private scoped messages/files, group audit snapshots, rejected-operation reason audit, owner/admin permission boundaries, removed-member read-only history, history visibility fields, and group send/file permission fields.

## File And Routing Coverage

- File chunk ACK progress, duplicate ACK de-duplication, invalid chunk metadata rejection, hash mismatch cleanup, forged sender rejection, cancel cleanup, retry after transient failure, and resume from the earliest missing chunk.
- TransferManager recovery UI state for empty/resumable/disconnected/E2E resend saved transfers, file transfer status event diagnostics/copy-action state, and send/resume progress labels, percent clamping, cancel guidance, and manifest summaries.
- Offline attachment quota, TTL cleanup, bad metadata cleanup, interrupted replay retention, partial ACK resume, stale resume fallback, and full-confirmation cleanup.
- Redis RESP, presence command flow, Pub/Sub route validation, cross-instance chat routing, E2E control routing, large-file object offer/claim/delivered/failed evidence, and Redis unavailable fallback.
- Filesystem object store path safety, S3 SigV4 pure functions, request classification, timeout configuration, injected request execution, response size/hash validation, and delivered receipt governance.

## Automation Coverage

- Automation status generation, default automation task bootstrap/readback, task history, task acknowledgement, database health tasks, PostgreSQL release evidence, large-file governance tasks, governance dashboard/status/report generation, and README information architecture checks.
- Windows package manifest validation for versioned output directories, ZIP naming, manifest fields, runtime dependency checks, and packaged README inclusion.

Run locally with:

```powershell
D:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build-qt6-mingw --output-on-failure
```
