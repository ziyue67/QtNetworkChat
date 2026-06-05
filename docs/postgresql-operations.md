# PostgreSQL Operations

QtNetworkChat keeps SQLite as the default storage engine. PostgreSQL is enabled only when `QTNETWORKCHAT_DB_DRIVER=QPSQL` is configured, so SQLite and PostgreSQL can coexist during rollout.

## Runtime Configuration

Set PostgreSQL connection values through environment variables, and pass the password from the environment rather than committing it to source, logs, or evidence:

```powershell
$env:QTNETWORKCHAT_DB_DRIVER = "QPSQL"
$env:QTNETWORKCHAT_PGHOST = "127.0.0.1"
$env:QTNETWORKCHAT_PGPORT = "5432"
$env:QTNETWORKCHAT_PGDATABASE = "qtnetworkchat"
$env:QTNETWORKCHAT_PGUSER = "postgres"
$env:QTNETWORKCHAT_PGPASSWORD = "<redacted>"
```

## Health And Release Evidence

- `scripts/check-database-health.ps1` generates redacted health JSON for SQLite or PostgreSQL.
- `scripts/show-database-health-status.ps1` turns health JSON into release-readable status.
- `scripts/write-database-health-dashboard.ps1` writes a dashboard with release gates, pool metrics, slow-query warnings, and query-failure warnings.
- `scripts/write-pgsql-release-acceptance.ps1` unifies health, real QPSQL smoke, migration diff, rollback preview, rollback audit, and evidence package readiness.

## Connection Pool Policy

PostgreSQL defaults to `QTNETWORKCHAT_DB_POOL=1`. Pool settings include maximum connections, idle cleanup, reconnect backoff, and slow query thresholds. The server records thread-affine pool evidence including pooled connections, peak pooled connections, thread count, cross-thread checkout prevention, and cross-thread release detection.

## Real Smoke Boundary

`scripts/run-pgsql-protocol-smoke.ps1 -EnsureDatabase` exercises real QPSQL protocol behavior, including offline attachment recovery, chunk metadata failures, partial ACK resume, stale resume fallback, confirmed chunk gaps, online transfer metadata, retry boundaries, and release acceptance evidence. Always provide the password through `QTNETWORKCHAT_PGPASSWORD` or a parameter value that is never written to committed files.

