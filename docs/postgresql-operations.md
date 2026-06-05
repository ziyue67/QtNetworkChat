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

Health JSON includes `reconnectPolicy`, `queryMetrics`, `threadPolicy`, `summary.readiness`, `summary.operatorAction`, and `auditSummary.releaseGate`. Status and dashboard scripts pass through pooled connection counts, peak pooled connections, pool thread counts, idle/overflow cleanup counters, cross-thread checkout prevention, cross-thread release detection, slow-query warnings, and query-failure warnings. Use `QTNETWORKCHAT_DB_POOL`, `QTNETWORKCHAT_DB_POOL_MAX`, `QTNETWORKCHAT_DB_POOL_IDLE_MS`, `QTNETWORKCHAT_DB_RECONNECT_BACKOFF_MS`, and `QTNETWORKCHAT_DB_SLOW_QUERY_MS` to tune the pool and query evidence.

## Connection Pool Policy

PostgreSQL defaults to `QTNETWORKCHAT_DB_POOL=1`. Pool settings include maximum connections, idle cleanup, reconnect backoff, and slow query thresholds. The server records thread-affine pool evidence including pooled connections, peak pooled connections, thread count, cross-thread checkout prevention, and cross-thread release detection.

## Real Smoke Boundary

`scripts/run-pgsql-protocol-smoke.ps1 -EnsureDatabase` exercises real QPSQL protocol behavior, including offline attachment recovery, chunk metadata failures, partial ACK resume, stale resume fallback, confirmed chunk gaps, online transfer metadata, retry boundaries, and release acceptance evidence. Always provide the password through `QTNETWORKCHAT_PGPASSWORD` or a parameter value that is never written to committed files.

`scripts/start-local-postgres.ps1` can bootstrap a local database under `build-qt6-mingw\pg-real-data` or reuse an existing local service. Smoke evidence records bootstrap readiness, audit release gates, coverage surfaces, boundary scenarios, recovery summaries, retry/resume counts, cleanup proof, and redacted evidence bundle paths. Default CTest verifies plan output, redaction, Markdown generation, and coverage declarations without connecting to real PostgreSQL.

## Migration And Rollback

`scripts/migrate-sqlite-to-postgres.ps1` supports `plan`, `execute`, `validate`, `diff`, and `rollback`. Reports include per-table row counts, `diffSummary`, `reportSummary.executionReadiness`, `operatorAction`, `auditSummary`, rollback preview risk, fallback key review counts, and rollback audit before/after evidence. Run `plan` first, review the redacted JSON/Markdown/HTML and rollback preview, then use `execute` only after backing up `accounts.sqlite3`.

## Release Acceptance Task

`scripts/register-pgsql-release-acceptance-task.ps1` writes a preview and launcher by default. The launcher reads the password from `QTNETWORKCHAT_PGPASSWORD`, runs health/status/dashboard, real QPSQL smoke, migration plan/diff, rollback preview/audit, acceptance summary, task history/ack, and redacted evidence packaging. Register the Windows task only after reviewing the preview; use `-PlanOnly` when validating local runtime and artifact paths without connecting to PostgreSQL.
