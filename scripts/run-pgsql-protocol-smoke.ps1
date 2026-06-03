param(
    [string]$TestExe = "build-qt6-mingw\postgres_qpsql_protocol_smoke_test.exe",
    [string]$QtRoot = "D:\Qt\6.8.3\mingw_64",
    [string]$PostgresBinDir = "D:\Program Files\PostgreSQL\17\bin",
    [string]$PostgresHost = "127.0.0.1",
    [int]$PostgresPort = 5432,
    [string]$PostgresDatabase = "qtnetworkchat",
    [string]$PostgresUser = "postgres",
    [string]$PostgresPassword = $env:QTNETWORKCHAT_PGPASSWORD,
    [string]$AppDataDir,
    [switch]$PlanOnly,
    [switch]$EnsureDatabase,
    [string]$BootstrapJsonPath,
    [string]$JsonPath,
    [string]$MarkdownPath
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $Path
}

function New-Check {
    param(
        [string]$Name,
        [bool]$Ok,
        [string]$Detail
    )

    [pscustomobject]@{
        name = $Name
        ok = $Ok
        detail = $Detail
    }
}

function Get-SmokeOperatorAction {
    param(
        [bool]$PlanOnly,
        [bool]$EnsureDatabase,
        [bool]$Ok,
        [int]$FailedChecks
    )

    if (-not $Ok) {
        return "Fix prerequisite failures before running PostgreSQL QPSQL smoke."
    }
    if ($PlanOnly -and $EnsureDatabase) {
        return "Bootstrap prerequisites look ready; next run can enable real smoke with EnsureDatabase."
    }
    if ($PlanOnly) {
        return "Runtime prerequisites look ready; next run can enable real smoke against the target PostgreSQL."
    }
    if ($FailedChecks -gt 0) {
        return "Review prerequisite failures and rerun smoke after fixing local runtime or database access."
    }
    "Archive the redacted evidence and use it for PostgreSQL release readiness review."
}

function Get-SmokeReleaseGate {
    param(
        [bool]$PlanOnly,
        [bool]$EnsureDatabase,
        [string]$Readiness
    )

    if ($Readiness -eq "blocked") {
        return "blocked"
    }
    if ($PlanOnly) {
        if ($EnsureDatabase) {
            return "await-real-smoke-with-bootstrap"
        }
        return "await-real-smoke"
    }
    if ($EnsureDatabase) {
        return "can-review-real-smoke-evidence"
    }
    "can-review-direct-smoke-evidence"
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$testExePath = Resolve-RepoPath $TestExe
$qtBinDir = Join-Path $QtRoot "bin"
$qtPluginDir = Join-Path $QtRoot "plugins"
$qpsqlPluginPath = Join-Path $qtPluginDir "sqldrivers\qsqlpsql.dll"
$libpqPath = Join-Path $PostgresBinDir "libpq.dll"
$bootstrapScript = Join-Path $PSScriptRoot "start-local-postgres.ps1"
$runtimeAppData = if ([string]::IsNullOrWhiteSpace($AppDataDir)) {
    Join-Path $repoRoot "build-qt6-mingw\pgsql-protocol-smoke-runtime"
} else {
    Resolve-RepoPath $AppDataDir
}

$checks = @(
    (New-Check "test-executable" (Test-Path -LiteralPath $testExePath -PathType Leaf) $testExePath),
    (New-Check "qt-bin" (Test-Path -LiteralPath $qtBinDir -PathType Container) $qtBinDir),
    (New-Check "qt-qpsql-plugin" (Test-Path -LiteralPath $qpsqlPluginPath -PathType Leaf) $qpsqlPluginPath),
    (New-Check "postgres-bin" (Test-Path -LiteralPath $PostgresBinDir -PathType Container) $PostgresBinDir),
    (New-Check "postgres-libpq-runtime" (Test-Path -LiteralPath $libpqPath -PathType Leaf) $libpqPath),
    (New-Check "bootstrap-script" (Test-Path -LiteralPath $bootstrapScript -PathType Leaf) $bootstrapScript)
)

$coverageSurfaces = @(
    "register-login",
    "private-message-persistence",
    "friend-search-request-accept",
    "friend-boundary-events",
    "public-group-announcement-audit",
    "public-group-member-role-audit",
    "public-group-remove-readd-marker",
    "file-chunk-metadata-persistence",
    "online-file-chunk-invalid-ack-retry",
    "offline-private-queue-replay",
    "offline-attachment-queue-replay",
    "offline-attachment-partial-ack-resume",
    "offline-attachment-expired-resume-fallback",
    "offline-attachment-confirmed-chunks-gap-resume",
    "offline-attachment-all-confirmed-cleanup",
    "restart-login-kdf-session"
)

$boundaryScenarios = @(
    [pscustomobject]@{
        name = "offline-private-queue-replay"
        category = "offline-message"
        persistence = "offline_messages"
        expectedEvidence = "offline queue row is inserted while the peer is offline and removed after replay"
    },
    [pscustomobject]@{
        name = "offline-attachment-chunk-metadata"
        category = "offline-attachment"
        persistence = "offline_messages.payload"
        expectedEvidence = "offline attachment payload keeps fileName, fileSize, fileHash, chunkSize and chunkCount"
    },
    [pscustomobject]@{
        name = "offline-attachment-replay-cleanup"
        category = "offline-attachment"
        persistence = "offline_messages"
        expectedEvidence = "offline attachment is replayed to the relogged peer and queue rows are cleared"
    },
    [pscustomobject]@{
        name = "offline-attachment-missing-file-cleanup"
        category = "offline-attachment-failure"
        persistence = "offline_messages"
        expectedEvidence = "queued attachment file is deleted before replay, peer receives a missing-file notice, and the bad PostgreSQL queue row is cleared"
    },
    [pscustomobject]@{
        name = "offline-attachment-size-hash-chunk-cleanup"
        category = "offline-attachment-failure"
        persistence = "offline_messages"
        expectedEvidence = "queued attachment size, hash and chunk metadata are corrupted before replay, peer receives fixed failure notices, and bad PostgreSQL queue rows are cleared"
    },
    [pscustomobject]@{
        name = "offline-attachment-partial-ack-resume"
        category = "offline-attachment-resume"
        persistence = "offline_messages.payload"
        expectedEvidence = "raw receiver confirms the first chunk then disconnects, PostgreSQL stores confirmedBytes/confirmedChunks/resumeUpdatedAt, and retry resumes from the first unconfirmed chunk"
    },
    [pscustomobject]@{
        name = "offline-attachment-expired-resume-fallback"
        category = "offline-attachment-resume"
        persistence = "offline_messages.payload"
        expectedEvidence = "expired resumeUpdatedAt makes PostgreSQL ignore stale confirmedBytes/confirmedChunks, replay from chunk 0, and clear the queue after delivery"
    },
    [pscustomobject]@{
        name = "offline-attachment-confirmed-chunks-gap-resume"
        category = "offline-attachment-resume"
        persistence = "offline_messages.payload"
        expectedEvidence = "duplicate confirmedChunks are deduplicated, non-contiguous gaps resume from the first missing chunk, and the PostgreSQL queue is cleared after delivery"
    },
    [pscustomobject]@{
        name = "offline-attachment-all-confirmed-cleanup"
        category = "offline-attachment-resume"
        persistence = "offline_messages.payload"
        expectedEvidence = "confirmedChunks covering every chunk skip replay and clear the PostgreSQL queue without resending the attachment"
    },
    [pscustomobject]@{
        name = "file-chunk-metadata-persistence"
        category = "file-chunk"
        persistence = "messages"
        expectedEvidence = "online file transfer persists file hash, chunk size and chunk count in PostgreSQL"
    },
    [pscustomobject]@{
        name = "online-file-chunk-invalid-ack-retry"
        category = "file-chunk-retry"
        persistence = "messages"
        expectedEvidence = "raw receiver sends an invalid first ACK progress, sender retries chunk 0, transfer completes, and PostgreSQL keeps file chunk metadata"
    },
    [pscustomobject]@{
        name = "restart-login-kdf-session"
        category = "restart-boundary"
        persistence = "accounts,user_sessions"
        expectedEvidence = "server restart keeps PBKDF2 account hash and records a new login session"
    }
)

$result = [ordered]@{
    format = "qtnetworkchat-pgsql-protocol-smoke-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    planOnly = [bool]$PlanOnly
    ok = @($checks | Where-Object { -not $_.ok }).Count -eq 0
    checks = $checks
    coverageSurfaces = $coverageSurfaces
    boundaryScenarios = $boundaryScenarios
    testExecutable = $testExePath
    qtRoot = $QtRoot
    postgresBinDir = $PostgresBinDir
    appDataDir = $runtimeAppData
    ensureDatabase = [bool]$EnsureDatabase
    bootstrapJsonPath = ""
    bootstrapExitCode = $null
    environment = [ordered]@{
        QT_PLUGIN_PATH = $qtPluginDir
        QTNETWORKCHAT_RUN_REAL_QPSQL_TEST = "1"
        QTNETWORKCHAT_DB_DRIVER = "QPSQL"
        QTNETWORKCHAT_PGHOST = $PostgresHost
        QTNETWORKCHAT_PGPORT = "$PostgresPort"
        QTNETWORKCHAT_PGDATABASE = $PostgresDatabase
        QTNETWORKCHAT_PGUSER = $PostgresUser
        QTNETWORKCHAT_PGPASSWORD = "<redacted>"
    }
    exitCode = $null
}

if (-not [string]::IsNullOrWhiteSpace($BootstrapJsonPath)) {
    $result.bootstrapJsonPath = Resolve-RepoPath $BootstrapJsonPath
}

$failedChecks = @($checks | Where-Object { -not $_.ok })
$offlineAttachmentScenarios = @($boundaryScenarios | Where-Object { [string]$_.category -like "offline-attachment*" })
$resumeScenarios = @($boundaryScenarios | Where-Object { [string]$_.category -eq "offline-attachment-resume" })
$failureScenarios = @($boundaryScenarios | Where-Object { [string]$_.category -eq "offline-attachment-failure" })
$auditFocus = New-Object System.Collections.Generic.List[string]
if ($EnsureDatabase) { [void]$auditFocus.Add("verify-bootstrap-readiness") }
if ($offlineAttachmentScenarios.Count -gt 0) { [void]$auditFocus.Add("offline-attachment-recovery") }
if ($resumeScenarios.Count -gt 0) { [void]$auditFocus.Add("resume-gap-and-expiry") }
if ($failureScenarios.Count -gt 0) { [void]$auditFocus.Add("attachment-corruption-cleanup") }
if (@($boundaryScenarios | Where-Object { [string]$_.category -eq "file-chunk-retry" }).Count -gt 0) { [void]$auditFocus.Add("online-chunk-retry") }
if (@($boundaryScenarios | Where-Object { [string]$_.category -eq "restart-boundary" }).Count -gt 0) { [void]$auditFocus.Add("restart-kdf-session") }
$result.summary = [ordered]@{
    readiness = if ($result.ok) { "ready" } else { "blocked" }
    failedCheckCount = $failedChecks.Count
    coverageSurfaceCount = $coverageSurfaces.Count
    boundaryScenarioCount = $boundaryScenarios.Count
    bootstrapMode = if ($EnsureDatabase) { "ensure-database" } else { "direct-connect" }
    operatorAction = Get-SmokeOperatorAction -PlanOnly ([bool]$PlanOnly) -EnsureDatabase ([bool]$EnsureDatabase) -Ok ([bool]$result.ok) -FailedChecks $failedChecks.Count
}
$result.auditSummary = [ordered]@{
    releaseGate = Get-SmokeReleaseGate -PlanOnly ([bool]$PlanOnly) -EnsureDatabase ([bool]$EnsureDatabase) -Readiness $result.summary.readiness
    evidenceBundle = @("json", "markdown", "boundary-scenarios")
    boundaryCategories = @(@($boundaryScenarios | ForEach-Object { [string]$_.category } | Sort-Object -Unique))
    bootstrapRequired = [bool]$EnsureDatabase
    offlineAttachmentScenarioCount = $offlineAttachmentScenarios.Count
    resumeScenarioCount = $resumeScenarios.Count
    failureScenarioCount = $failureScenarios.Count
    auditFocus = @($auditFocus)
}

if (-not $PlanOnly) {
    if (-not $result.ok) {
        $missing = @($checks | Where-Object { -not $_.ok } | ForEach-Object { $_.name })
        throw ("PostgreSQL protocol smoke prerequisites missing: {0}" -f ($missing -join ", "))
    }
    if ([string]::IsNullOrWhiteSpace($PostgresPassword)) {
        throw "PostgresPassword is required for real PostgreSQL protocol smoke"
    }

    New-Item -ItemType Directory -Force -Path $runtimeAppData | Out-Null

    if ($EnsureDatabase) {
        $bootstrapJsonTarget = if ([string]::IsNullOrWhiteSpace($BootstrapJsonPath)) {
            Join-Path $runtimeAppData "local-postgres-bootstrap.json"
        } else {
            Resolve-RepoPath $BootstrapJsonPath
        }
        $bootstrapParent = Split-Path -Parent $bootstrapJsonTarget
        if (-not [string]::IsNullOrWhiteSpace($bootstrapParent)) {
            New-Item -ItemType Directory -Force -Path $bootstrapParent | Out-Null
        }
        & powershell -ExecutionPolicy Bypass -File $bootstrapScript `
            -PostgresBinDir $PostgresBinDir `
            -Database $PostgresDatabase `
            -User $PostgresUser `
            -Password $PostgresPassword `
            -HostAddress $PostgresHost `
            -Port $PostgresPort `
            -JsonPath $bootstrapJsonTarget
        $result.bootstrapJsonPath = $bootstrapJsonTarget
        $result.bootstrapExitCode = $LASTEXITCODE
        if ($LASTEXITCODE -ne 0) {
            $result.ok = $false
            throw ("PostgreSQL database bootstrap failed with exit code {0}. See redacted bootstrap JSON: {1}" -f $LASTEXITCODE, $bootstrapJsonTarget)
        }
    }

    $oldPath = $env:PATH
    $oldPluginPath = $env:QT_PLUGIN_PATH
    $oldRun = $env:QTNETWORKCHAT_RUN_REAL_QPSQL_TEST
    $oldDriver = $env:QTNETWORKCHAT_DB_DRIVER
    $oldHost = $env:QTNETWORKCHAT_PGHOST
    $oldPort = $env:QTNETWORKCHAT_PGPORT
    $oldDatabase = $env:QTNETWORKCHAT_PGDATABASE
    $oldUser = $env:QTNETWORKCHAT_PGUSER
    $oldPassword = $env:QTNETWORKCHAT_PGPASSWORD
    $oldAppData = $env:QTNETWORKCHAT_APPDATA_DIR
    try {
        $env:PATH = "$qtBinDir;$PostgresBinDir;$oldPath"
        $env:QT_PLUGIN_PATH = $qtPluginDir
        $env:QTNETWORKCHAT_RUN_REAL_QPSQL_TEST = "1"
        $env:QTNETWORKCHAT_DB_DRIVER = "QPSQL"
        $env:QTNETWORKCHAT_PGHOST = $PostgresHost
        $env:QTNETWORKCHAT_PGPORT = "$PostgresPort"
        $env:QTNETWORKCHAT_PGDATABASE = $PostgresDatabase
        $env:QTNETWORKCHAT_PGUSER = $PostgresUser
        $env:QTNETWORKCHAT_PGPASSWORD = $PostgresPassword
        $env:QTNETWORKCHAT_APPDATA_DIR = $runtimeAppData

        & $testExePath
        $result.exitCode = $LASTEXITCODE
        $result.ok = $result.ok -and ($LASTEXITCODE -eq 0)
        $result.summary.readiness = if ($result.ok) { "verified" } else { "blocked" }
        $result.summary.operatorAction = Get-SmokeOperatorAction -PlanOnly $false -EnsureDatabase ([bool]$EnsureDatabase) -Ok ([bool]$result.ok) -FailedChecks (@($checks | Where-Object { -not $_.ok }).Count)
        $result.auditSummary.releaseGate = Get-SmokeReleaseGate -PlanOnly $false -EnsureDatabase ([bool]$EnsureDatabase) -Readiness $result.summary.readiness
    } finally {
        $env:PATH = $oldPath
        $env:QT_PLUGIN_PATH = $oldPluginPath
        $env:QTNETWORKCHAT_RUN_REAL_QPSQL_TEST = $oldRun
        $env:QTNETWORKCHAT_DB_DRIVER = $oldDriver
        $env:QTNETWORKCHAT_PGHOST = $oldHost
        $env:QTNETWORKCHAT_PGPORT = $oldPort
        $env:QTNETWORKCHAT_PGDATABASE = $oldDatabase
        $env:QTNETWORKCHAT_PGUSER = $oldUser
        $env:QTNETWORKCHAT_PGPASSWORD = $oldPassword
        $env:QTNETWORKCHAT_APPDATA_DIR = $oldAppData
    }
}

if (-not [string]::IsNullOrWhiteSpace($JsonPath)) {
    $jsonTarget = Resolve-RepoPath $JsonPath
    $parent = Split-Path -Parent $jsonTarget
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonTarget -Encoding UTF8
}

if (-not [string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $markdownTarget = Resolve-RepoPath $MarkdownPath
    $parent = Split-Path -Parent $markdownTarget
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }

    $failedChecks = @($checks | Where-Object { -not $_.ok })
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("# PostgreSQL QPSQL Protocol Smoke Evidence")
    $lines.Add("")
    $lines.Add(("- Generated at: {0}" -f $result.generatedAt))
    $lines.Add(("- Plan only: {0}" -f $result.planOnly))
    $lines.Add(("- OK: {0}" -f $result.ok))
    $lines.Add(("- Exit code: {0}" -f ($(if ($null -eq $result.exitCode) { "not-run" } else { $result.exitCode }))))
    $lines.Add(("- Ensure database: {0}" -f $result.ensureDatabase))
    $lines.Add(("- Bootstrap exit code: {0}" -f ($(if ($null -eq $result.bootstrapExitCode) { "not-run" } else { $result.bootstrapExitCode }))))
    $lines.Add(("- Bootstrap JSON: {0}" -f ($(if ([string]::IsNullOrWhiteSpace($result.bootstrapJsonPath)) { "n/a" } else { $result.bootstrapJsonPath }))))
    $lines.Add(("- PostgreSQL target: {0}:{1}/{2} as {3}" -f $PostgresHost, $PostgresPort, $PostgresDatabase, $PostgresUser))
    $lines.Add("- PostgreSQL password: <redacted>")
    $lines.Add(("- Failed prerequisite checks: {0}" -f $failedChecks.Count))
    $lines.Add(("- Readiness: {0}" -f $result.summary.readiness))
    $lines.Add(("- Coverage surfaces: {0}" -f $result.summary.coverageSurfaceCount))
    $lines.Add(("- Boundary scenarios: {0}" -f $result.summary.boundaryScenarioCount))
    $lines.Add(("- Bootstrap mode: {0}" -f $result.summary.bootstrapMode))
    $lines.Add(("- Operator action: {0}" -f $result.summary.operatorAction))
    $lines.Add(("- Audit release gate: {0}" -f $result.auditSummary.releaseGate))
    $lines.Add(("- Audit bootstrap required: {0}" -f $result.auditSummary.bootstrapRequired))
    $lines.Add(("- Audit offline-attachment scenarios: {0}" -f $result.auditSummary.offlineAttachmentScenarioCount))
    $lines.Add(("- Audit resume scenarios: {0}" -f $result.auditSummary.resumeScenarioCount))
    $lines.Add(("- Audit failure scenarios: {0}" -f $result.auditSummary.failureScenarioCount))
    $lines.Add(("- Audit evidence bundle: {0}" -f (@($result.auditSummary.evidenceBundle) -join ", ")))
    $lines.Add(("- Audit focus: {0}" -f (@($result.auditSummary.auditFocus) -join ", ")))
    $lines.Add("")
    $lines.Add("## Runtime Checks")
    $lines.Add("")
    $lines.Add("| Check | OK | Detail |")
    $lines.Add("|---|---:|---|")
    foreach ($check in $checks) {
        $detail = [string]$check.detail
        $detail = $detail.Replace("|", "\|")
        $lines.Add(("| {0} | {1} | {2} |" -f $check.name, $check.ok, $detail))
    }
    $lines.Add("")
    $lines.Add("## Coverage Surfaces")
    $lines.Add("")
    foreach ($surface in $coverageSurfaces) {
        $lines.Add(("- {0}" -f $surface))
    }
    $lines.Add("")
    $lines.Add("## Audit Summary")
    $lines.Add("")
    $lines.Add("| Audit metric | Value |")
    $lines.Add("|---|---|")
    $lines.Add(("| Release gate | {0} |" -f $result.auditSummary.releaseGate))
    $lines.Add(("| Bootstrap required | {0} |" -f $result.auditSummary.bootstrapRequired))
    $lines.Add(("| Offline attachment scenarios | {0} |" -f $result.auditSummary.offlineAttachmentScenarioCount))
    $lines.Add(("| Resume scenarios | {0} |" -f $result.auditSummary.resumeScenarioCount))
    $lines.Add(("| Failure scenarios | {0} |" -f $result.auditSummary.failureScenarioCount))
    $lines.Add(("| Boundary categories | {0} |" -f (@($result.auditSummary.boundaryCategories) -join ", ")))
    $lines.Add(("| Evidence bundle | {0} |" -f (@($result.auditSummary.evidenceBundle) -join ", ")))
    $lines.Add(("| Audit focus | {0} |" -f (@($result.auditSummary.auditFocus) -join ", ")))
    $lines.Add("")
    $lines.Add("## Boundary Scenarios")
    $lines.Add("")
    $lines.Add("| Scenario | Category | Persistence | Expected evidence |")
    $lines.Add("|---|---|---|---|")
    foreach ($scenario in $boundaryScenarios) {
        $lines.Add(("| {0} | {1} | {2} | {3} |" -f
            $scenario.name,
            $scenario.category,
            $scenario.persistence,
            $scenario.expectedEvidence))
    }
    Set-Content -LiteralPath $markdownTarget -Value ($lines -join [Environment]::NewLine) -Encoding UTF8
}

$result | ConvertTo-Json -Depth 8
if (-not $result.ok) {
    exit 2
}
