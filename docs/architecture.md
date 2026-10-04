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
  Views/settings, group panel, group member sidebar, friend manager, search,
  menus, and notifications have separate compilation units. The quick-add
  entry point uses the shared add-friend dialog; its unreachable legacy dialog
  was removed. The remaining window file is still large and needs incremental
  extraction with UI regression checks.
- `src/server.cpp` owns startup, Redis routing, persistence, and transfer
  handling. TCP connection lifecycle and frame dispatch are in
  `src/server_transport.cpp`; friend and group operations are in
  `src/server_friend.cpp` and `src/server_group.cpp`. The remaining server
  file is still large, especially database and offline-transfer paths.
- `QQNTRedisService` is a startup requirement: without Redis, cross-instance
  presence and delivery cannot be guaranteed, so startup fails closed.
- PostgreSQL holds production account/chat state; Redis handles presence,
  Pub/Sub routing, and transient coordination. The deployment keeps its own
  database and Redis key prefix, separate from the older service.

## Security And Delivery

The default E2E backend is a draft protocol, not production cryptography.
Production-capable builds link the OpenSSL provider for X25519, HKDF-SHA256,
AES-256-GCM, and Ed25519; runtime selection still requires
`QTNETWORKCHAT_E2E_CRYPTO_BACKEND=production`. The provider status code in
`src/e2eenvelope.cpp` remains a large maintenance hotspot;
`src/e2e_envelope_codec.cpp` contains JSON encoding and validation, while
`src/e2e_crypto_primitives.cpp` contains draft primitives and session-material
encoding.

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
