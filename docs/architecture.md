# Architecture

QtNetworkChat has two desktop-facing transports and one newline-delimited JSON
application protocol. The Qt Widgets client uses `Client` for TCP/TLS or
WebSocket/WSS; the server itself accepts TCP. The WSS deployment uses OpenResty
for TLS and a WebSocket-to-TCP gateway for frame conversion. OpenResty alone
does not translate WebSocket frames into the server protocol.

## Runtime Boundaries

- `src/client.cpp` owns transport selection, connection state, TLS pinning,
  login frames, file transfer, and E2E session use. The desktop window consumes
  its signals and must not write directly to a socket.
- `src/mainwindow.cpp` owns window lifecycle and remaining chat workflows.
  Initialization/layout, transfer workflows, history/favorites, message actions,
  views/settings, group panel, group member sidebar, friend manager, search,
  menus, and notifications have separate compilation units. The quick-add
  entry point uses the shared add-friend dialog; its unreachable legacy dialog
  was removed. The remaining window file is still large and needs incremental
  extraction with UI regression checks. `mainwindow_setup.cpp` owns the initial
  layout and synchronized session controls; `mainwindow_transfers.cpp` owns file
  selection, progress/cancellation/recovery and received-file persistence;
  `mainwindow_history.cpp` owns history dialogs and favorites;
  `mainwindow_chat_actions.cpp` owns forwarding, media, screenshots and message
  actions. Presentation helpers have one implementation in
  `mainwindow_support.cpp`.
  `mainwindow_composer.cpp` owns command expansion, send decisions and outgoing
  message presentation;
  `mainwindow_contacts.cpp` owns contact lists and friend selection. These
  further extractions reduce the main file to about 2,539 lines without
  introducing another owner of window state.
  The group-info view is built by `src/group_info_panel_ui.cpp`; the window
  wires permissions, manager settings, personal settings, and leave/dissolve
  actions separately in `src/mainwindow_group_panel.cpp`.
- `src/server.cpp` owns startup and local message routing. Redis readiness,
  presence, Pub/Sub dispatch and cross-instance message/control routing are in
  `server_redis_routing.cpp`. Object-store offers, claims, receipts, validation,
  chunk delivery and cleanup are in `server_large_file_routing.cpp`.
  Group membership/permissions/audit queries and snapshots are in
  `server_group_repository.cpp`; friendship persistence and queries are in
  `server_friend_repository.cpp`. TCP connection lifecycle and frame
  dispatch are in `src/server_transport.cpp`; friend and group operations are in
  `src/server_friend.cpp` and `src/server_group.cpp`. The remaining server
  database driver differences, connection pool, health checks, and schema
  initialization are in `src/server_database.cpp`.
  `src/server_account.cpp` owns login, password KDF/legacy migration, profile
  persistence, session records, and account deactivation/re-registration.
  `src/server_file_transfer.cpp` owns incoming file validation, upload state,
  cancellation/timeout, outgoing chunks, and matching acknowledgements.
  `src/server_offline_delivery.cpp` owns attachment storage/quota/expiry,
  queue persistence/fallback, replay validation, and confirmed resume progress.
  `src/server_delivery_support.cpp` shares E2E wire-field validation, delivery
  notices, and route logging without copying the implementations.
  These units keep the same `Server` state and private methods; ownership is
  separated by compilation unit, not by introducing another server object.
  A disconnect removes account presence, heartbeat and socket routing only
  when that socket still owns the current route. An older login still cleans
  up its own session/transfer references without deleting a newer login's route.
- `QQNTRedisService` is a startup requirement: without Redis, cross-instance
  presence and delivery cannot be guaranteed, so startup fails closed.
- PostgreSQL holds production account/chat state; Redis handles presence,
  Pub/Sub routing, and transient coordination. The deployment keeps its own
  database and Redis key prefix, separate from the older service.

## Security And Delivery

The default E2E backend is a draft protocol, not production cryptography.
Production-capable builds link the OpenSSL provider for X25519, HKDF-SHA256,
AES-256-GCM, and Ed25519; runtime selection still requires
`QTNETWORKCHAT_E2E_CRYPTO_BACKEND=production`.
`src/e2eenvelope.cpp` now owns the public cryptographic operations rather than
the status/reporting layers;
`src/e2e_envelope_codec.cpp` contains JSON encoding and validation, while
`src/e2e_crypto_primitives.cpp` contains draft primitives and session-material
encoding.
`src/e2e_provider_runtime.cpp` validates provider tables, dispatches ABI calls,
and runs positive/negative runtime self-tests against an explicitly supplied
table. `e2e_backend_selection.cpp` owns selection, registration and the shared
status cache. Contracts, provider-table status, probes, invocation reports,
review handoff and acceptance/rollout reports have distinct compilation units.
Their internal declarations are in `e2e_backend_status_p.h`; public API and
report schemas stay compatible. The reporting graph remains substantial; this
extraction separates its responsibilities without deleting evidence fields.
Shared report helpers in `e2e_backend_status_p.h` now write exact common
provider identities and no-secret-export fields; computed report fields remain
explicit. Repeated production compile guards use `productionOperationCompiled`.

The internal `E2EProductionSuite` names the production provider's wire suite.
It is not the default suite for draft encryption. `e2eDefaultSuite()` and
generated envelopes advertise `draft-placeholder` on the draft path, and
`x25519-hkdf-sha256-aes-256-gcm` when the selected production provider passes
runtime checks. Regression checks assert both literal suite names on actual
encryption output.

A configured TLS certificate pin is an explicit trust root for self-signed
installations. Both TLS and WSS compare the peer certificate before sending
login data. Without a pin, normal certificate-chain and hostname verification
is controlled by `QTNETWORKCHAT_TLS_VERIFY`.

File chunk acknowledgements identify a transfer by `transferId` and
`chunkIndex`. Resume state records accepted indexes and confirmed byte counts;
it is not keyed by `clientMessageId`, which is a message-level identifier.

## Verification

Run `cmake --build build --parallel` and
`ctest --test-dir build --output-on-failure` after changing shared protocol
or transport code. See [testing-coverage.md](testing-coverage.md) for the
current test scope and gaps. Production deployment is deliberately pinned to a
validated image tag; publishing `latest` does not upgrade that server.
