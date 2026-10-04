# Testing Coverage

The default Linux CMake suite contains 55 CTest entries. Windows registers
additional platform-specific script checks; the CI focused list is a subset.
Run the complete suite
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
- `MainWindowNetworkWorkflow` links the same `qtnetworkchat_desktop` library as
  the executable. It drives the login form, the real group-profile save/cancel
  and speaking-rule chooser, checks member editing restrictions, asserts the
  persisted SQLite values and the other client's snapshot, sends chat text,
  cancels a file picker, sends/receives/saves a 600 KiB multi-chunk file, checks
  exact bytes and the receiver's card, renders light/dark screenshots, and
  verifies draft preservation and delivery after reconnect. The fixture uses
  a real local TCP server and an in-process Redis test service, temporary
  storage, and disposable accounts; it does not connect to production.
- Headless engine startup, protocol drift, and end-to-end private messaging.

The Linux and Windows workflows build the app, run focused checks including
account KDF migration, offline quota/replay/resume, duplicate acknowledgements,
upload cancellation, and engine startup/command acknowledgements. They upload
login/group-info/main-window screenshots as Actions artifacts. They also run
`scripts/test-script-maintenance.ps1` to parse all maintained PowerShell scripts
and exercise shared path/hash helpers, two actual evidence consumers and README
documentation checks.
Tagged desktop
releases also run installer smoke checks; the container workflow builds
and publishes the server image. A successful installer smoke check is not a
code-signing certificate: the Windows installer is currently unsigned.

Windows GUI tests load the Noto Sans CJK SC font from the pinned Noto CJK
`Sans2.004` commit and check that it contains Chinese glyphs. Linux CI installs
`fonts-noto-cjk`. This is a test dependency and is not bundled with the app.
Screenshots check device scale and scan the image for nonblank content;
assertion failures go directly to stderr so Windows CTest captures the reason.

`E2EEnvelopeProtocol` includes provider-not-linked assertions and is registered
only when the adapter is not linked. Linked production builds use
`E2EProductionAdapterRuntime` for provider behavior instead.

`PostgresQpsqlProtocolSmoke` returns success without contacting PostgreSQL
unless `QTNETWORKCHAT_RUN_REAL_QPSQL_TEST=1` and the required database settings
are supplied. The local 55/55 result includes this default bypass. Redis
integration fixtures use the repository's in-process test service. Neither
result proves the production PostgreSQL or Redis deployment is healthy.
The dated local verification record is in
[refactoring-closeout.md](refactoring-closeout.md).

The October 4 production upgrade separately verified strict public TLS/HTTP
101, protocol registration/login, private delivery and offline replay, actual
PostgreSQL account/message persistence, Redis presence and observed Pub/Sub
publication. The backup was restored into a disposable database. See the
[deployment evidence](production-deployment-2026-10-04.md). This was a live
smoke check with a standard WebSocket client, not the complete QPSQL suite,
cross-instance Redis regression or manual Qt desktop acceptance.

## Remaining Gaps

The certificate-pin unit test checks correct and incorrect certificate
fingerprints, but does not currently run a full local TLS server to assert
login-frame ordering. The network GUI scenario covers login, group profile and
speaking-rule persistence, file save and reconnect; it does not drive every
menu, notification, screenshot capture, approval, file-retry/cancel-in-progress,
or platform-native dialog interaction. Protocol-level transfer cancellation
and retry tests cover those wire behaviors independently. Offscreen screenshots
do not replace manual desktop checks on Linux and
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
