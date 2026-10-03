# Testing Coverage

The current CMake suite contains 52 CTest entries. Run the complete suite
after changes to shared client, server, E2E, or transfer code:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

## Covered Paths

- E2E envelope validation, provider configuration/runtime gates, and private
  message delivery.
- TCP/WebSocket transport, TLS fingerprint helpers, login credential migration,
  Redis startup/readiness, cross-instance routing, and PostgreSQL protocol smoke.
- File transfer retry, duplicate chunk acknowledgements, resume state, offline
  quota, cancellation, and public image delivery.
- Desktop state managers for sessions, contacts, notifications, composer,
  transfer cards, and visual style consistency.
- Headless engine startup, protocol drift, and end-to-end private messaging.

The Linux and Windows workflows build the app and run focused checks. Tagged
desktop releases also run installer smoke checks; the container workflow builds
and publishes the server image. A successful installer smoke check is not a
code-signing certificate: the Windows installer is currently unsigned.

`E2EEnvelopeProtocol` includes provider-not-linked assertions and is registered
only when the adapter is not linked. Linked production builds use
`E2EProductionAdapterRuntime` for provider behavior instead.

## Remaining Gaps

The certificate-pin unit test checks correct and incorrect certificate
fingerprints, but does not currently run a full local TLS server to assert
login-frame ordering. Window menu and notification dialogs are compiled but
not driven by an automated GUI interaction test. Public WSS and the pinned
production server must be checked separately from local CTest after deployment
changes; publishing a new image does not update the pinned server.
