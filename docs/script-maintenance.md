# Script Maintenance

The repository retains PowerShell helpers used by CMake checks, packaging,
evidence generation, and operations. A filename reference audit is an initial
filter; a manually invoked tool can still be useful without an internal caller.
Deleting scripts to reach an arbitrary count would remove supported checks.

The October 2026 cleanup removed the duplicate
`capture-autologin-screenshot.ps1` and the unreferenced `start-redis.ps1`,
`start-redis-6380.ps1`, and their private-path `redis-6380.conf`. The two old
Redis starters stopped existing processes and hardcoded a personal installation.
These tracked files can be restored from Git history if needed. The earlier
cleanup also removed the unreferenced root `screenshot.ps1`.

Use [capture-screenshot.ps1](../capture-screenshot.ps1) for desktop captures;
examples are in [testing-coverage.md](testing-coverage.md).

Use [start-with-redis.ps1](../scripts/start-with-redis.ps1) for a local Windows
Redis-backed client. It locates Redis through PATH or an explicit directory,
checks PING, starts a local instance only when needed, and selects the client
from the requested build/configuration:

```powershell
./scripts/start-with-redis.ps1 -RedisDir C:/Tools/Redis -BuildDir build -Port 6380 -Prefix qtchat-local
```

To use an existing Redis endpoint, pass `-NoStartRedis` and its host/port.
This tool does not stop existing Redis processes. Build directories must
already be configured with CMake. Windows CI parses the maintained desktop
helpers; running Redis and capturing the desktop require a Windows session.

[s3-failure-drill.ps1](../scripts/s3-failure-drill.ps1) remains a manual
operations helper: it prints failure scenarios and optionally analyzes supplied
logs. It does not inject failures automatically. The remaining packaging and
evidence scripts retain their supported command interfaces.

The current consolidation removes 43 duplicated path/hash implementations
from 25 evidence/operations scripts. They dot-source
[qt-script-common.ps1](../scripts/qt-script-common.ps1). Repo-relative paths,
optional paths relative to the caller's working directory, empty-input behavior,
SHA-256 formatting and stream disposal are preserved. S3-specific sensitive-field
policies stay in `qt-governance-common.ps1`; they are not interchangeable with
release/evidence policies.

Run [test-script-maintenance.ps1](../scripts/test-script-maintenance.ps1) with
PowerShell 7 or Windows PowerShell:

```powershell
./scripts/test-script-maintenance.ps1
```

It parses 69 maintained scripts (including the root screenshot helper), checks
path resolution from another working directory, a known SHA-256 result and
missing-file behavior, confirms the hash stream closes, and actually runs
local verification/publication-record helpers against disposable fixtures.
Both Linux and Windows CI run it. The publication fixture writes local evidence
only; it does not publish a release. The maintained README checker also runs,
using current build commands and documentation links rather than obsolete
personal build-directory names. See [scripts/README.md](../scripts/README.md)
for supported entry points and responsibilities.
