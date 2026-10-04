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
evidence scripts need further consolidation after their consumers are updated.
