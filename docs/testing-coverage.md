# Testing Coverage

The default Linux CMake suite contains 58 CTest entries; a linked production
build contains 57 because the unlinked-only envelope case is not registered.
Two real-service cases skip with exit code 77 unless explicitly enabled.
Windows registers
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
- `MainWindowExtendedGui` drives create/menu-bar actions, friend and group
  approvals, screenshot confirmation/cancellation, and a real 2 MiB transfer
  cancellation followed by reselection/resend with exact-byte verification.
  Message and member context menus also check arguments and local effects.
  Member-menu signal dispatch is not proof of every server-side mutation.
- Native file, avatar, directory and export dialogs run separately on GTK/X11
  and Win32. The GTK driver uses process-scoped X11 input; the Windows driver
  uses UI Automation Value/Invoke patterns on the owning process's HWND.
  Native dialogs remain enabled, and assertions check their actual results.
  Windows first runs `NativeDialogDriverSmoke`: the OS Save picker must return
  the requested temporary path instead of the initial Documents filename, and
  a file written through that result must be readable at the requested path.
  This small integration test runs before the complete Windows build so driver
  failures report their returned path without waiting for the desktop build.
- `TlsLoginFrameOrdering` uses real local TLS and WSS servers for ten scenarios:
  correct/wrong pin, untrusted/trusted CA and wrong hostname on each transport.
  Rejected connections must deliver zero login frames; accepted ones exactly one.

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

`PostgresQpsqlProtocolSmoke` and `RealRedisCrossInstanceRouting` return 77
(CTest skipped) unless their real-service switches and connection settings are
supplied. Linux CI enables both against isolated PostgreSQL 16 and Redis 7
services. The QPSQL case exercises account/group/friend protocols, file
cancellation/resume, offline integrity/quota/expiry/replay and restart.
The Redis case uses two actual Server instances for presence/PubSub, chat,
ordinary/object-store files, receipts, cleanup and offline replay after an
instance switch. Most other Redis fixtures still use an in-process test service.
Neither CI isolation nor local test skips prove production services healthy.
The dated local verification record is in
[refactoring-closeout.md](refactoring-closeout.md).

The October 4 production upgrade separately verified strict public TLS/HTTP
101, protocol registration/login, private delivery and offline replay, actual
PostgreSQL account/message persistence, Redis presence and observed Pub/Sub
publication. The backup was restored into a disposable database. See the
[deployment evidence](production-deployment-2026-10-04.md). This was a live
smoke check with a standard WebSocket client, not the complete QPSQL suite,
cross-instance Redis regression or manual Qt desktop acceptance.

## Results And Limits

The dated results and CI links are in [plan-completion-status.md](plan-completion-status.md).
Both v1.1.5 tag builds passed the 19 focused checks and actual native picker
flows. Linux additionally passed isolated PostgreSQL/Redis tests and five
consecutive engine runs. The published installer also passed
[Windows Release Smoke](https://github.com/ziyue67/QtNetworkChat/actions/runs/37214462329):
downloaded bytes matched GitHub's SHA-256; Authenticode was NotSigned; a runner
without a Qt SDK installed, launched for eight seconds and uninstalled it.
This does not establish SmartScreen reputation.
Production configuration tests now compare generated flags with actual CMake
options; rollout evidence checks the linked/unlinked gate separately. The
production private-delivery case executes real provider text/file/rotation/
trust-persistence behavior instead of default unlinked-only assertions.

Engine friend-event checks require an acknowledged delivery and a received
event in separate stages while pumping both peers. On failure they print
protocol diagnostics. Repeated passes are evidence of the measured runs, not
proof that the previously observed rare failure has a known root cause.

GUI checks cover the named scenarios, not every theme, DPI, multi-monitor,
tray/avatar/contact permutation or OS notification policy. Native automation
does not constitute manual desktop acceptance on every supported machine.
Protocol cancellation/resume tests independently cover wire behavior; GUI
reselection/resend is not same-transferId resume. Public WSS and the pinned
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
