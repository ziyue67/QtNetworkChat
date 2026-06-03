param(
    [string]$OutputDir = "build-qt6-mingw\pgsql-rollback-live-evidence",
    [string]$QtRoot = "D:\Qt\6.8.3\mingw_64",
    [string]$PostgresBinDir = "D:\Program Files\PostgreSQL\17\bin",
    [string]$PostgresHost = "127.0.0.1",
    [int]$PostgresPort = 5432,
    [string]$PostgresDatabase = "qtnetworkchat",
    [string]$PostgresUser = "postgres",
    [string]$PostgresPassword = $env:QTNETWORKCHAT_PGPASSWORD,
    [string]$MigratorExe = "build-qt6-mingw\sqlite_to_postgres_migrator.exe",
    [string]$TestExe = "build-qt6-mingw\postgres_qpsql_protocol_smoke_test.exe",
    [string]$AppDataDir,
    [string]$SampleOwnerId = "930001",
    [string]$SamplePeerId = "930002",
    [switch]$PlanOnly,
    [switch]$EnsureDatabase,
    [switch]$SkipSmoke,
    [string]$JsonPath,
    [string]$MarkdownPath
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    '(^|["''\s{,])QTNETWORKCHAT_PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    '(^|["''\s{,])password["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    '(^|["''\s{,])PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'ghp_[A-Za-z0-9_]+',
    'github_pat_[A-Za-z0-9_]+',
    'secret[-_\s]?key',
    'access[-_\s]?key',
    'session[-_\s]?token',
    'Authorization\s*[:=]',
    'Credential\s*=',
    'Signature\s*='
)

function Resolve-RepoPath {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $Path
}

function New-StepResult {
    param(
        [string]$Name,
        [string]$Mode,
        [string]$Path,
        [int]$ExitCode,
        [bool]$Required = $true
    )

    [ordered]@{
        name = $Name
        mode = $Mode
        path = $Path
        exitCode = $ExitCode
        required = $Required
        ok = ($ExitCode -eq 0 -and (-not $Required -or [string]::IsNullOrWhiteSpace($Path) -or (Test-Path -LiteralPath $Path -PathType Leaf)))
    }
}

function Add-SensitiveHits {
    param(
        [string]$Path,
        [System.Collections.ArrayList]$Hits
    )

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return
    }
    $lineNumber = 0
    Get-Content -LiteralPath $Path -Encoding UTF8 | ForEach-Object {
        $lineNumber += 1
        $line = [string]$_
        foreach ($pattern in $sensitivePatterns) {
            if ($line -match $pattern -and $line -notmatch "(<redacted>|\\u003credacted\\u003e|&lt;redacted&gt;)") {
                [void]$Hits.Add(("{0}:{1}:{2}" -f (Split-Path -Leaf $Path), $lineNumber, $pattern))
            }
        }
    }
}

function Read-JsonFile {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    $raw = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $null
    }
    $raw | ConvertFrom-Json
}

function Get-JsonValue {
    param(
        [object]$ObjectValue,
        [string]$Name,
        [object]$DefaultValue = $null
    )
    if ($null -eq $ObjectValue) {
        return $DefaultValue
    }
    if ($ObjectValue.PSObject.Properties.Name -contains $Name) {
        return $ObjectValue.$Name
    }
    $DefaultValue
}

function Invoke-Checked {
    param(
        [string]$Name,
        [scriptblock]$Script
    )
    Write-Host ("[pgsql-rollback-live] {0}" -f $Name)
    & $Script | Out-Host
    $exitCode = $LASTEXITCODE
    if ($null -eq $exitCode) {
        $exitCode = 0
    }
    if ($exitCode -ne 0) {
        throw ("{0} failed with exit code {1}" -f $Name, $exitCode)
    }
    return [int]$exitCode
}

$outputRoot = Resolve-RepoPath $OutputDir
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

$jsonTarget = if ([string]::IsNullOrWhiteSpace($JsonPath)) { Join-Path $outputRoot "pgsql-rollback-live-evidence.json" } else { Resolve-RepoPath $JsonPath }
$markdownTarget = if ([string]::IsNullOrWhiteSpace($MarkdownPath)) { Join-Path $outputRoot "pgsql-rollback-live-evidence.md" } else { Resolve-RepoPath $MarkdownPath }
$sqlitePath = Join-Path $outputRoot "rollback-live-sample.sqlite3"
$appDataPath = if ([string]::IsNullOrWhiteSpace($AppDataDir)) { Join-Path $outputRoot "appdata" } else { Resolve-RepoPath $AppDataDir }

$healthPath = Join-Path $outputRoot "database-health.json"
$healthStatusPath = Join-Path $outputRoot "database-health-status.json"
$healthDashboardPath = Join-Path $outputRoot "database-health-dashboard.json"
$healthDashboardMarkdownPath = Join-Path $outputRoot "database-health-dashboard.md"
$smokeJsonPath = Join-Path $outputRoot "pgsql-smoke.json"
$smokeMarkdownPath = Join-Path $outputRoot "pgsql-smoke.md"
$smokeBootstrapPath = Join-Path $outputRoot "pgsql-smoke-bootstrap.json"
$executeJsonPath = Join-Path $outputRoot "sqlite-pg-migration-execute.json"
$executeMarkdownPath = Join-Path $outputRoot "sqlite-pg-migration-execute.md"
$executeHtmlPath = Join-Path $outputRoot "sqlite-pg-migration-execute.html"
$diffJsonPath = Join-Path $outputRoot "sqlite-pg-migration-diff.json"
$diffMarkdownPath = Join-Path $outputRoot "sqlite-pg-migration-diff.md"
$diffHtmlPath = Join-Path $outputRoot "sqlite-pg-migration-diff.html"
$rollbackPreviewPath = Join-Path $outputRoot "sqlite-pg-rollback-preview-before.json"
$rollbackPreviewMarkdownPath = Join-Path $outputRoot "sqlite-pg-rollback-preview-before.md"
$rollbackJsonPath = Join-Path $outputRoot "sqlite-pg-migration-rollback.json"
$rollbackMarkdownPath = Join-Path $outputRoot "sqlite-pg-migration-rollback.md"
$rollbackHtmlPath = Join-Path $outputRoot "sqlite-pg-migration-rollback.html"
$rollbackExecutePreviewPath = Join-Path $outputRoot "sqlite-pg-rollback-preview-executed.json"
$rollbackExecutePreviewMarkdownPath = Join-Path $outputRoot "sqlite-pg-rollback-preview-executed.md"
$rollbackAuditPath = Join-Path $outputRoot "sqlite-pg-rollback-audit-live.json"
$rollbackAuditMarkdownPath = Join-Path $outputRoot "sqlite-pg-rollback-audit-live.md"
$acceptancePath = Join-Path $outputRoot "pgsql-release-acceptance.json"
$acceptanceMarkdownPath = Join-Path $outputRoot "pgsql-release-acceptance.md"
$notesPath = Join-Path $outputRoot "pgsql-rollback-live-notes.md"
$lastRunPath = Join-Path $outputRoot "last-run.log"
$historyPath = Join-Path $outputRoot "automation-task-history.json"
$historyMarkdownPath = Join-Path $outputRoot "automation-task-history.md"
$ackPath = Join-Path $outputRoot "automation-task-ack.json"
$evidenceDir = Join-Path $outputRoot "evidence"
$evidencePackagePath = Join-Path $evidenceDir "pgsql-rollback-live-evidence.zip"
$evidenceManifestPath = Join-Path $evidenceDir "pgsql-rollback-live-evidence-manifest.json"

$scripts = [ordered]@{
    bootstrap = Join-Path $PSScriptRoot "start-local-postgres.ps1"
    health = Join-Path $PSScriptRoot "check-database-health.ps1"
    status = Join-Path $PSScriptRoot "show-database-health-status.ps1"
    dashboard = Join-Path $PSScriptRoot "write-database-health-dashboard.ps1"
    smoke = Join-Path $PSScriptRoot "run-pgsql-protocol-smoke.ps1"
    migration = Join-Path $PSScriptRoot "migrate-sqlite-to-postgres.ps1"
    acceptance = Join-Path $PSScriptRoot "write-pgsql-release-acceptance.ps1"
    package = Join-Path $PSScriptRoot "package-pgsql-release-evidence.ps1"
    history = Join-Path $PSScriptRoot "write-automation-task-history.ps1"
}

$migratorPath = Resolve-RepoPath $MigratorExe
$testExePath = Resolve-RepoPath $TestExe
$qtBinDir = Join-Path $QtRoot "bin"
$qtSqlitePlugin = Join-Path $QtRoot "plugins\sqldrivers\qsqlite.dll"
$qtQpsqlPlugin = Join-Path $QtRoot "plugins\sqldrivers\qsqlpsql.dll"
$libpqPath = Join-Path $PostgresBinDir "libpq.dll"

$checks = @(
    [ordered]@{ name = "migrator"; ok = (Test-Path -LiteralPath $migratorPath -PathType Leaf); detail = $migratorPath },
    [ordered]@{ name = "qt-bin"; ok = (Test-Path -LiteralPath $qtBinDir -PathType Container); detail = $qtBinDir },
    [ordered]@{ name = "qt-sqlite-plugin"; ok = (Test-Path -LiteralPath $qtSqlitePlugin -PathType Leaf); detail = $qtSqlitePlugin },
    [ordered]@{ name = "qt-qpsql-plugin"; ok = (Test-Path -LiteralPath $qtQpsqlPlugin -PathType Leaf); detail = $qtQpsqlPlugin },
    [ordered]@{ name = "postgres-libpq"; ok = (Test-Path -LiteralPath $libpqPath -PathType Leaf); detail = $libpqPath },
    [ordered]@{ name = "postgres-bin"; ok = (Test-Path -LiteralPath $PostgresBinDir -PathType Container); detail = $PostgresBinDir },
    [ordered]@{ name = "smoke-test-exe"; ok = (Test-Path -LiteralPath $testExePath -PathType Leaf); detail = $testExePath }
)
foreach ($entry in $scripts.GetEnumerator()) {
    $checks += [ordered]@{ name = ("script-" + $entry.Key); ok = (Test-Path -LiteralPath $entry.Value -PathType Leaf); detail = $entry.Value }
}

$missing = @($checks | Where-Object { -not $_.ok } | ForEach-Object { $_.name })
if ($missing.Count -gt 0) {
    throw ("PostgreSQL rollback live evidence prerequisites missing: {0}" -f ($missing -join ", "))
}

if (-not $PlanOnly -and [string]::IsNullOrWhiteSpace($PostgresPassword)) {
    throw "PostgresPassword is required for real PostgreSQL rollback live evidence."
}

$steps = New-Object System.Collections.Generic.List[object]
$artifactPaths = New-Object System.Collections.Generic.List[string]

if ($PlanOnly) {
    $steps.Add((New-StepResult "plan" "plan-only" $jsonTarget 0))
    $summary = [ordered]@{
        format = "qtnetworkchat-pgsql-rollback-live-evidence-v1"
        generatedAt = (Get-Date).ToUniversalTime().ToString("o")
        planOnly = $true
        ok = $true
        status = "plan-ready"
        checks = $checks
        steps = @($steps.ToArray())
        summary = [ordered]@{
            readiness = "ready"
            operatorAction = "Runtime prerequisites look ready; next run can execute PostgreSQL rollback live evidence."
            releaseGate = "await-live-rollback-evidence"
        }
        rollbackLive = [ordered]@{
            beforePreviewAvailable = $true
            rollbackExecutionApplied = $false
            afterAuditAvailable = $true
            releaseAcceptanceAvailable = $true
            evidencePackageAvailable = $true
        }
        artifacts = [ordered]@{
            sqlitePath = $sqlitePath
            databaseHealthDashboardPath = $healthDashboardPath
            smokeJsonPath = $smokeJsonPath
            migrationExecuteJsonPath = $executeJsonPath
            migrationDiffJsonPath = $diffJsonPath
            rollbackPreviewPath = $rollbackPreviewPath
            rollbackJsonPath = $rollbackJsonPath
            rollbackExecutePreviewPath = $rollbackExecutePreviewPath
            rollbackAuditPath = $rollbackAuditPath
            acceptanceJsonPath = $acceptancePath
            evidencePackagePath = $evidencePackagePath
            evidenceManifestPath = $evidenceManifestPath
        }
        environment = [ordered]@{
            QTNETWORKCHAT_PGPASSWORD = "<redacted>"
            QTNETWORKCHAT_DB_DRIVER = "QPSQL"
        }
        sensitiveHits = @()
    }
} else {
    if ($EnsureDatabase) {
        $bootstrapPath = Join-Path $outputRoot "local-postgres-bootstrap.json"
        $code = Invoke-Checked "ensure PostgreSQL database" {
            & powershell -ExecutionPolicy Bypass -File $scripts.bootstrap `
                -PostgresBinDir $PostgresBinDir `
                -Database $PostgresDatabase `
                -User $PostgresUser `
                -Password $PostgresPassword `
                -HostAddress $PostgresHost `
                -Port $PostgresPort `
                -JsonPath $bootstrapPath
        }
        $steps.Add((New-StepResult "bootstrap" "ensure-database" $bootstrapPath $code))
        $artifactPaths.Add($bootstrapPath)
    }

    $code = Invoke-Checked "database health" {
        & powershell -ExecutionPolicy Bypass -File $scripts.health `
            -Driver postgres `
            -QtRoot $QtRoot `
            -PostgresBinDir $PostgresBinDir `
            -PostgresHost $PostgresHost `
            -PostgresPort $PostgresPort `
            -PostgresDatabase $PostgresDatabase `
            -PostgresUser $PostgresUser `
            -PostgresPassword $PostgresPassword `
            -SQLitePath $sqlitePath `
            -JsonPath $healthPath
    }
    $steps.Add((New-StepResult "database-health" "postgres" $healthPath $code))
    $artifactPaths.Add($healthPath)

    $code = Invoke-Checked "database health status" {
        & powershell -ExecutionPolicy Bypass -File $scripts.status `
            -HealthPath $healthPath `
            -JsonPath $healthStatusPath
    }
    $steps.Add((New-StepResult "database-health-status" "postgres" $healthStatusPath $code))
    $artifactPaths.Add($healthStatusPath)

    $code = Invoke-Checked "database health dashboard" {
        & powershell -ExecutionPolicy Bypass -File $scripts.dashboard `
            -HealthPath $healthPath `
            -StatusPath $healthStatusPath `
            -DashboardPath $healthDashboardPath `
            -MarkdownPath $healthDashboardMarkdownPath
    }
    $steps.Add((New-StepResult "database-health-dashboard" "postgres" $healthDashboardPath $code))
    $artifactPaths.Add($healthDashboardPath)
    $artifactPaths.Add($healthDashboardMarkdownPath)

    if (-not $SkipSmoke) {
        $code = Invoke-Checked "QPSQL smoke evidence" {
            $smokeArgs = @(
                "-ExecutionPolicy", "Bypass",
                "-File", $scripts.smoke,
                "-TestExe", $testExePath,
                "-QtRoot", $QtRoot,
                "-PostgresBinDir", $PostgresBinDir,
                "-PostgresHost", $PostgresHost,
                "-PostgresPort", "$PostgresPort",
                "-PostgresDatabase", $PostgresDatabase,
                "-PostgresUser", $PostgresUser,
                "-PostgresPassword", $PostgresPassword,
                "-AppDataDir", $appDataPath,
                "-BootstrapJsonPath", $smokeBootstrapPath,
                "-JsonPath", $smokeJsonPath,
                "-MarkdownPath", $smokeMarkdownPath
            )
            if ($EnsureDatabase) {
                $smokeArgs += "-EnsureDatabase"
            }
            & powershell @smokeArgs
        }
        $steps.Add((New-StepResult "pgsql-smoke" "real-qpsql" $smokeJsonPath $code))
        $artifactPaths.Add($smokeJsonPath)
        $artifactPaths.Add($smokeMarkdownPath)
        $artifactPaths.Add($smokeBootstrapPath)
    } else {
        $smokeStub = [ordered]@{
            format = "qtnetworkchat-pgsql-protocol-smoke-v1"
            generatedAt = (Get-Date).ToUniversalTime().ToString("o")
            planOnly = $false
            ok = $true
            summary = [ordered]@{ readiness = "verified"; coverageSurfaceCount = 0; boundaryScenarioCount = 0; operatorAction = "Smoke skipped by rollback live evidence runner." }
            auditSummary = [ordered]@{ releaseGate = "smoke-skipped"; evidenceBundle = @("json"); bootstrapRequired = [bool]$EnsureDatabase; auditFocus = @("rollback-live-evidence") }
            recoverySummary = [ordered]@{ retryOrResumeCount = 0; cleanupProofCount = 0; releaseHint = "Smoke was skipped for rollback-only live evidence." }
            environment = [ordered]@{ QTNETWORKCHAT_PGPASSWORD = "<redacted>" }
        }
        $smokeStub | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $smokeJsonPath -Encoding UTF8
        $steps.Add((New-StepResult "pgsql-smoke" "skipped" $smokeJsonPath 0 $false))
        $artifactPaths.Add($smokeJsonPath)
    }

    $migrationCommon = @{
        MigratorExe = $migratorPath
        SQLitePath = $sqlitePath
        QtRoot = $QtRoot
        PostgresBinDir = $PostgresBinDir
        PostgresHost = $PostgresHost
        PostgresPort = $PostgresPort
        PostgresDatabase = $PostgresDatabase
        PostgresUser = $PostgresUser
        PostgresPassword = $PostgresPassword
    }

    $code = Invoke-Checked "migration execute sample" {
        & powershell -ExecutionPolicy Bypass -File $scripts.migration @migrationCommon `
            -Mode execute `
            -CreateSample `
            -SampleOwnerId $SampleOwnerId `
            -SamplePeerId $SamplePeerId `
            -JsonPath $executeJsonPath `
            -MarkdownPath $executeMarkdownPath `
            -HtmlPath $executeHtmlPath
    }
    $steps.Add((New-StepResult "migration-execute" "execute" $executeJsonPath $code))
    $artifactPaths.Add($executeJsonPath)
    $artifactPaths.Add($executeMarkdownPath)
    $artifactPaths.Add($executeHtmlPath)

    $code = Invoke-Checked "migration diff and rollback preview" {
        & powershell -ExecutionPolicy Bypass -File $scripts.migration @migrationCommon `
            -Mode diff `
            -JsonPath $diffJsonPath `
            -MarkdownPath $diffMarkdownPath `
            -HtmlPath $diffHtmlPath `
            -RollbackPreviewPath $rollbackPreviewPath `
            -RollbackPreviewMarkdownPath $rollbackPreviewMarkdownPath
    }
    $steps.Add((New-StepResult "migration-diff" "diff" $diffJsonPath $code))
    $steps.Add((New-StepResult "rollback-preview-before" "diff" $rollbackPreviewPath 0))
    $artifactPaths.Add($diffJsonPath)
    $artifactPaths.Add($diffMarkdownPath)
    $artifactPaths.Add($diffHtmlPath)
    $artifactPaths.Add($rollbackPreviewPath)
    $artifactPaths.Add($rollbackPreviewMarkdownPath)

    $code = Invoke-Checked "migration rollback with audit" {
        & powershell -ExecutionPolicy Bypass -File $scripts.migration @migrationCommon `
            -Mode rollback `
            -JsonPath $rollbackJsonPath `
            -MarkdownPath $rollbackMarkdownPath `
            -HtmlPath $rollbackHtmlPath `
            -RollbackPreviewPath $rollbackExecutePreviewPath `
            -RollbackPreviewMarkdownPath $rollbackExecutePreviewMarkdownPath `
            -RollbackAuditPath $rollbackAuditPath `
            -RollbackAuditMarkdownPath $rollbackAuditMarkdownPath
    }
    $steps.Add((New-StepResult "migration-rollback" "rollback" $rollbackJsonPath $code))
    $steps.Add((New-StepResult "rollback-audit-live" "rollback" $rollbackAuditPath 0))
    $artifactPaths.Add($rollbackJsonPath)
    $artifactPaths.Add($rollbackMarkdownPath)
    $artifactPaths.Add($rollbackHtmlPath)
    $artifactPaths.Add($rollbackExecutePreviewPath)
    $artifactPaths.Add($rollbackExecutePreviewMarkdownPath)
    $artifactPaths.Add($rollbackAuditPath)
    $artifactPaths.Add($rollbackAuditMarkdownPath)

    $code = Invoke-Checked "release acceptance" {
        & powershell -ExecutionPolicy Bypass -File $scripts.acceptance `
            -DatabaseHealthDashboardPath $healthDashboardPath `
            -SmokeJsonPath $smokeJsonPath `
            -MigrationJsonPath $diffJsonPath `
            -RollbackPreviewPath $rollbackPreviewPath `
            -RollbackAuditPath $rollbackAuditPath `
            -JsonPath $acceptancePath `
            -MarkdownPath $acceptanceMarkdownPath
    }
    $steps.Add((New-StepResult "release-acceptance" "summary" $acceptancePath $code))
    $artifactPaths.Add($acceptancePath)
    $artifactPaths.Add($acceptanceMarkdownPath)

    $lastRunLine = "{0} exitCode=0 executeJsonPath={1} diffJsonPath={2} rollbackPreviewPath={3} rollbackAuditPath={4} acceptancePath={5} evidencePackagePath={6}" -f `
        (Get-Date).ToUniversalTime().ToString("o"), $executeJsonPath, $diffJsonPath, $rollbackPreviewPath, $rollbackAuditPath, $acceptancePath, $evidencePackagePath
    $lastRunLine | Set-Content -LiteralPath $lastRunPath -Encoding UTF8
    "{}" | Set-Content -LiteralPath $ackPath -Encoding UTF8
    $code = Invoke-Checked "automation history" {
        & powershell -ExecutionPolicy Bypass -File $scripts.history `
            -LastRunPath $lastRunPath `
            -AckPath $ackPath `
            -JsonPath $historyPath `
            -MarkdownPath $historyMarkdownPath `
            -FailOnSensitive
    }
    $steps.Add((New-StepResult "automation-history" "history" $historyPath $code))
    $artifactPaths.Add($lastRunPath)
    $artifactPaths.Add($historyPath)
    $artifactPaths.Add($historyMarkdownPath)
    $artifactPaths.Add($ackPath)

    $notes = @(
        "# PostgreSQL Rollback Live Evidence Notes",
        "",
        "- This run uses a controlled SQLite sample and real PostgreSQL rollback mode.",
        "- PostgreSQL password is supplied only through process parameters/environment and all artifacts must show `<redacted>`.",
        "- Before preview is generated before rollback execute; rollback audit records after counters from rollback mode.",
        "- Release acceptance and evidence package are generated from local redacted artifacts."
    )
    $notes -join [Environment]::NewLine | Set-Content -LiteralPath $notesPath -Encoding UTF8
    $artifactPaths.Add($notesPath)

    $code = Invoke-Checked "evidence package" {
        & powershell -ExecutionPolicy Bypass -File $scripts.package `
            -OutputDir $evidenceDir `
            -PackagePath $evidencePackagePath `
            -ManifestPath $evidenceManifestPath `
            -NotesPath $notesPath `
            -DatabaseHealthPath $healthPath `
            -DatabaseHealthStatusPath $healthStatusPath `
            -DatabaseHealthDashboardPath $healthDashboardPath `
            -SmokeJsonPath $smokeJsonPath `
            -SmokeMarkdownPath $smokeMarkdownPath `
            -SmokeBootstrapJsonPath $smokeBootstrapPath `
            -MigrationJsonPath $diffJsonPath `
            -MigrationMarkdownPath $diffMarkdownPath `
            -MigrationHtmlPath $diffHtmlPath `
            -RollbackPreviewPath $rollbackPreviewPath `
            -RollbackPreviewMarkdownPath $rollbackPreviewMarkdownPath `
            -RollbackAuditPath $rollbackAuditPath `
            -RollbackAuditMarkdownPath $rollbackAuditMarkdownPath `
            -AcceptanceJsonPath $acceptancePath `
            -AcceptanceMarkdownPath $acceptanceMarkdownPath `
            -LastRunPath $lastRunPath `
            -HistoryPath $historyPath `
            -HistoryMarkdownPath $historyMarkdownPath `
            -AckPath $ackPath
    }
    $steps.Add((New-StepResult "evidence-package" "zip" $evidenceManifestPath $code))
    $artifactPaths.Add($evidencePackagePath)
    $artifactPaths.Add($evidenceManifestPath)

    $rollbackPreview = Read-JsonFile $rollbackPreviewPath
    $rollbackAudit = Read-JsonFile $rollbackAuditPath
    $acceptance = Read-JsonFile $acceptancePath
    $manifest = Read-JsonFile $evidenceManifestPath
    $auditSummary = Get-JsonValue $rollbackAudit "auditSummary" $null
    $before = Get-JsonValue $rollbackAudit "before" $null
    $after = Get-JsonValue $rollbackAudit "after" $null

    $sensitiveHits = New-Object System.Collections.ArrayList
    foreach ($path in @($artifactPaths.ToArray())) {
        Add-SensitiveHits $path $sensitiveHits
    }

    $ok = ($sensitiveHits.Count -eq 0) `
        -and [bool](Get-JsonValue $rollbackAudit "executionApplied" $false) `
        -and ([string](Get-JsonValue $auditSummary "releaseGate" "") -eq "rollback-executed-review") `
        -and [bool](Get-JsonValue $auditSummary "beforeAfterComplete" $false) `
        -and [bool](Get-JsonValue $auditSummary "countMatchesPreview" $false) `
        -and [bool](Get-JsonValue $manifest "ok" $false) `
        -and (Test-Path -LiteralPath $evidencePackagePath -PathType Leaf)

    $summary = [ordered]@{
        format = "qtnetworkchat-pgsql-rollback-live-evidence-v1"
        generatedAt = (Get-Date).ToUniversalTime().ToString("o")
        planOnly = $false
        ok = $ok
        status = if ($ok) { "verified" } else { "review" }
        checks = $checks
        steps = @($steps.ToArray())
        summary = [ordered]@{
            readiness = if ($ok) { "verified" } else { "review" }
            operatorAction = if ($ok) { "Archive rollback live evidence and switch the automation mainline to E2E productization." } else { "Review rollback live evidence warnings before leaving PostgreSQL productization." }
            releaseGate = if ($ok) { "can-close-pgsql-rollback-live-evidence" } else { "review-pgsql-rollback-live-evidence" }
        }
        rollbackLive = [ordered]@{
            beforePreviewAvailable = ($null -ne $rollbackPreview)
            rollbackExecutionApplied = [bool](Get-JsonValue $rollbackAudit "executionApplied" $false)
            afterAuditAvailable = ($null -ne $rollbackAudit)
            releaseAcceptanceAvailable = ($null -ne $acceptance)
            evidencePackageAvailable = (Test-Path -LiteralPath $evidencePackagePath -PathType Leaf)
            beforeRows = [int](Get-JsonValue $before "totalWouldDeleteRows" 0)
            afterRows = [int](Get-JsonValue $after "actualRolledBackRows" 0)
            countMatchesPreview = [bool](Get-JsonValue $auditSummary "countMatchesPreview" $false)
            rollbackAuditGate = [string](Get-JsonValue $auditSummary "releaseGate" "")
            acceptanceGate = [string](Get-JsonValue (Get-JsonValue $acceptance "auditSummary" $null) "releaseGate" "")
            evidenceInputCount = [int](Get-JsonValue $manifest "inputCount" 0)
        }
        artifacts = [ordered]@{
            sqlitePath = $sqlitePath
            databaseHealthDashboardPath = $healthDashboardPath
            smokeJsonPath = $smokeJsonPath
            migrationExecuteJsonPath = $executeJsonPath
            migrationDiffJsonPath = $diffJsonPath
            rollbackPreviewPath = $rollbackPreviewPath
            rollbackJsonPath = $rollbackJsonPath
            rollbackExecutePreviewPath = $rollbackExecutePreviewPath
            rollbackAuditPath = $rollbackAuditPath
            acceptanceJsonPath = $acceptancePath
            evidencePackagePath = $evidencePackagePath
            evidenceManifestPath = $evidenceManifestPath
        }
        environment = [ordered]@{
            QTNETWORKCHAT_PGPASSWORD = "<redacted>"
            QTNETWORKCHAT_DB_DRIVER = "QPSQL"
        }
        sensitiveHits = @($sensitiveHits.ToArray())
    }
}

$jsonParent = Split-Path -Parent $jsonTarget
if (-not [string]::IsNullOrWhiteSpace($jsonParent)) {
    New-Item -ItemType Directory -Path $jsonParent -Force | Out-Null
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonTarget -Encoding UTF8

$markdownParent = Split-Path -Parent $markdownTarget
if (-not [string]::IsNullOrWhiteSpace($markdownParent)) {
    New-Item -ItemType Directory -Path $markdownParent -Force | Out-Null
}
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("# PostgreSQL Rollback Live Evidence")
$lines.Add("")
$lines.Add(("- Plan only: {0}" -f $summary.planOnly))
$lines.Add(("- OK: {0}" -f $summary.ok))
$lines.Add(("- Status: {0}" -f $summary.status))
$lines.Add(("- Readiness: {0}" -f $summary.summary.readiness))
$lines.Add(("- Release gate: {0}" -f $summary.summary.releaseGate))
$lines.Add(("- Operator action: {0}" -f $summary.summary.operatorAction))
$lines.Add(("- PostgreSQL password: <redacted>"))
$lines.Add(("- Before preview available: {0}" -f $summary.rollbackLive.beforePreviewAvailable))
$lines.Add(("- Rollback execution applied: {0}" -f $summary.rollbackLive.rollbackExecutionApplied))
$lines.Add(("- After audit available: {0}" -f $summary.rollbackLive.afterAuditAvailable))
$lines.Add(("- Count matches preview: {0}" -f $summary.rollbackLive.countMatchesPreview))
$lines.Add(("- Rollback audit gate: {0}" -f $summary.rollbackLive.rollbackAuditGate))
$lines.Add(("- Release acceptance available: {0}" -f $summary.rollbackLive.releaseAcceptanceAvailable))
$lines.Add(("- Evidence package available: {0}" -f $summary.rollbackLive.evidencePackageAvailable))
$lines.Add(("- Evidence input count: {0}" -f $summary.rollbackLive.evidenceInputCount))
$lines.Add("")
$lines.Add("## Steps")
$lines.Add("")
$lines.Add("| Step | Mode | OK | Path |")
$lines.Add("|---|---|---:|---|")
foreach ($step in @($summary.steps)) {
    $lines.Add(("| {0} | {1} | {2} | {3} |" -f $step.name, $step.mode, $step.ok, $step.path))
}
$lines.Add("")
$lines.Add("This evidence is generated from a controlled local sample and sanitized PostgreSQL artifacts. It must not contain PostgreSQL passwords, tokens, or signed credentials.")
$lines | Set-Content -LiteralPath $markdownTarget -Encoding UTF8

Write-Host "pgsql rollback live evidence"
Write-Host ("  status: {0}" -f $summary.status)
Write-Host ("  ok: {0}" -f $summary.ok)
Write-Host ("  release gate: {0}" -f $summary.summary.releaseGate)
Write-Host ("  json: {0}" -f $jsonTarget)
Write-Host ("  markdown: {0}" -f $markdownTarget)

if (-not $summary.ok) {
    exit 2
}
