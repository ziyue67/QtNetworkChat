# Testing Coverage

The default CMake suite contains 54 CTest entries. Run the complete suite
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
- Offscreen login-window interaction across login, registration, local password
  reset, agreement gating, and light/dark rendering. The test saves three
  screenshots beside the test executable and checks that they are nonblank.
- Offscreen group-info component interaction covers displayed metadata and
  announcement, action-button clicks, notification toggling, member editing
  restrictions, and nonblank light/dark screenshots. It does not exercise
  MainWindow's network callbacks or persist changes to a live group.
- Headless engine startup, protocol drift, and end-to-end private messaging.

The Linux and Windows workflows build the app, run focused checks, and upload
the login/group-info smoke screenshots as Actions artifacts. Tagged
desktop releases also run installer smoke checks; the container workflow builds
and publishes the server image. A successful installer smoke check is not a
code-signing certificate: the Windows installer is currently unsigned.

`E2EEnvelopeProtocol` includes provider-not-linked assertions and is registered
only when the adapter is not linked. Linked production builds use
`E2EProductionAdapterRuntime` for provider behavior instead.

## Remaining Gaps

The certificate-pin unit test checks correct and incorrect certificate
fingerprints, but does not currently run a full local TLS server to assert
login-frame ordering. Window menu, notification dialogs, complete group-settings
save flows, and file transfer workflow are compiled but not driven by an
automated GUI interaction test. Offscreen screenshots do not replace manual desktop checks on Linux and
Windows. Public WSS and the pinned
production server must be checked separately from local CTest after deployment
changes; publishing a new image does not update the pinned server.

## Manual Windows Screenshots

`capture-screenshot.ps1` is the shared desktop capture helper. Run it in an
interactive Windows session after building or installing the app:

```powershell
./capture-screenshot.ps1 -ExePath ./build/Release/QtNetworkChat.exe -NewInstance
./capture-screenshot.ps1 -ExePath ./bin/QtNetworkChat.exe -NewInstance -NoStop -StartupDelay 8 -OutputPath ./screenshots/autologin-verify.png
```

The second command replaces the removed duplicate
`capture-autologin-screenshot.ps1`. Auto-login still depends on the app's saved
credentials; this helper only launches and captures the window. It creates the
output directory, selects a visible window belonging to the chosen process,
and only stops a process it launched. Windows CI checks the helper's syntax;
desktop capture itself requires an interactive session.
