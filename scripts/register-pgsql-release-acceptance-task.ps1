[CmdletBinding()]
param(
    [string]$TaskName = "QtNetworkChatPgsqlReleaseAcceptance",

    [ValidateSet("Daily", "Hourly")]
    [string]$Schedule = "Daily",

    [string]$At = "04:45",

    [int]$EveryHours = 1,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$TaskDir,

    [string]$QtRoot = "D:\Qt\6.8.3\mingw_64",

    [string]$PostgresBinDir = "D:\Program Files\PostgreSQL\17\bin",

    [string]$PostgresHost = "127.0.0.1",

    [int]$PostgresPort = 5432,

    [string]$PostgresDatabase = "qtnetworkchat",

    [string]$PostgresUser = "postgres",

    [string]$SQLitePath = "accounts.sqlite3",

    [string]$TestExe = "build-qt6-mingw\postgres_qpsql_protocol_smoke_test.exe",

    [string]$MigratorExe = "build-qt6-mingw\sqlite_to_postgres_migrator.exe",

    [string]$AppDataDir,

    [ValidateSet("plan", "execute", "validate", "diff", "rollback")]
    [string]$MigrationMode = "plan",

    [switch]$PlanOnly,

    [switch]$EnsureDatabase,

    [switch]$InjectSlowQueryProbe,

    [int]$SlowQueryProbeSeconds = 1,

    [ValidateSet("none", "query", "schema", "auth", "network", "tls")]
    [string]$InjectQueryFailureReason = "none",

    [string]$DatabaseHealthDashboardPath,

    [string]$SmokeJsonPath,

    [string]$MigrationJsonPath,

    [string]$RollbackPreviewPath,

    [string]$RollbackAuditPath,

    [switch]$FailOnUnhealthy,

    [switch]$SkipEvidencePackage,

    [switch]$Register,

    [int]$AckExpiryHours = 72,

    [string]$User = "SYSTEM"
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    'password["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'secret[-_\s]?key',
    'access[-_\s]?key',
    'Authorization',
    'Credential',
    'Signature'
)

function Assert-NoSensitiveValue([string]$Label, [string[]]$Values) {
    foreach ($value in $Values) {
        if ([string]::IsNullOrWhiteSpace($value)) {
            continue
        }
        foreach ($pattern in $sensitivePatterns) {
            if ($value -match $pattern -and $value -notmatch "<redacted>") {
                throw ("{0} contains sensitive-looking text rejected by PostgreSQL release acceptance scheduled task helper: {1}" -f $Label, $pattern)
            }
        }
    }
}

function Quote-PSString([string]$Value) {
    "'" + $Value.Replace("'", "''") + "'"
}

function Add-ScalarArg([System.Collections.ArrayList]$Lines, [string]$Name, [string]$Value) {
    if (-not [string]::IsNullOrWhiteSpace($Value)) {
        [void]$Lines.Add(("    -{0} {1} ``" -f $Name, (Quote-PSString $Value)))
    }
}

function Add-IntArg([System.Collections.ArrayList]$Lines, [string]$Name, [int]$Value) {
    if ($Value -gt 0) {
        [void]$Lines.Add(("    -{0} {1} ``" -f $Name, $Value))
    }
}

function Add-SwitchArg([System.Collections.ArrayList]$Lines, [string]$Name, [bool]$Enabled) {
    if ($Enabled) {
        [void]$Lines.Add(("    -{0} ``" -f $Name))
    }
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    throw "OutputDir is required."
}
if ($AckExpiryHours -lt 1) {
    throw "AckExpiryHours must be greater than zero."
}
if ($Schedule -eq "Hourly" -and $EveryHours -lt 1) {
    throw "EveryHours must be greater than zero."
}

Assert-NoSensitiveValue "TaskName" @($TaskName)
Assert-NoSensitiveValue "OutputDir" @($OutputDir)
Assert-NoSensitiveValue "TaskDir" @($TaskDir)
Assert-NoSensitiveValue "QtRoot" @($QtRoot)
Assert-NoSensitiveValue "PostgresBinDir" @($PostgresBinDir)
Assert-NoSensitiveValue "PostgresHost" @($PostgresHost)
Assert-NoSensitiveValue "PostgresDatabase" @($PostgresDatabase)
Assert-NoSensitiveValue "PostgresUser" @($PostgresUser)
Assert-NoSensitiveValue "SQLitePath" @($SQLitePath)
Assert-NoSensitiveValue "TestExe" @($TestExe)
Assert-NoSensitiveValue "MigratorExe" @($MigratorExe)
Assert-NoSensitiveValue "AppDataDir" @($AppDataDir)
Assert-NoSensitiveValue "DatabaseHealthDashboardPath" @($DatabaseHealthDashboardPath)
Assert-NoSensitiveValue "SmokeJsonPath" @($SmokeJsonPath)
Assert-NoSensitiveValue "MigrationJsonPath" @($MigrationJsonPath)
Assert-NoSensitiveValue "RollbackPreviewPath" @($RollbackPreviewPath)
Assert-NoSensitiveValue "RollbackAuditPath" @($RollbackAuditPath)
if ($PostgresPort -lt 1 -or $PostgresPort -gt 65535) {
    throw "PostgresPort must be between 1 and 65535."
}

if ([string]::IsNullOrWhiteSpace($TaskDir)) {
    $TaskDir = Join-Path $OutputDir "pgsql-release-acceptance-task"
}

$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
$resolvedTaskDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($TaskDir)
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null
New-Item -ItemType Directory -Path $resolvedTaskDir -Force | Out-Null

$healthScript = Join-Path $PSScriptRoot "check-database-health.ps1"
$statusScript = Join-Path $PSScriptRoot "show-database-health-status.ps1"
$dashboardScript = Join-Path $PSScriptRoot "write-database-health-dashboard.ps1"
$smokeScript = Join-Path $PSScriptRoot "run-pgsql-protocol-smoke.ps1"
$migrationScript = Join-Path $PSScriptRoot "migrate-sqlite-to-postgres.ps1"
$acceptanceScript = Join-Path $PSScriptRoot "write-pgsql-release-acceptance.ps1"
$packageScript = Join-Path $PSScriptRoot "package-pgsql-release-evidence.ps1"
$historyScript = Join-Path $PSScriptRoot "write-automation-task-history.ps1"
foreach ($scriptInfo in @(
        @{ Label = "Database health checker"; Path = $healthScript },
        @{ Label = "Database health status script"; Path = $statusScript },
        @{ Label = "Database health dashboard script"; Path = $dashboardScript },
        @{ Label = "PostgreSQL smoke runner"; Path = $smokeScript },
        @{ Label = "SQLite to PostgreSQL migrator launcher"; Path = $migrationScript },
        @{ Label = "PostgreSQL release evidence packager"; Path = $packageScript }
    )) {
    if (-not (Test-Path -LiteralPath $scriptInfo.Path -PathType Leaf)) {
        throw ("{0} not found: {1}" -f $scriptInfo.Label, $scriptInfo.Path)
    }
}
if (-not (Test-Path -LiteralPath $acceptanceScript -PathType Leaf)) {
    throw "PostgreSQL release acceptance writer not found: $acceptanceScript"
}
if (-not (Test-Path -LiteralPath $historyScript -PathType Leaf)) {
    throw "Automation task history writer not found: $historyScript"
}

$launcherPath = Join-Path $resolvedTaskDir "run-pgsql-release-acceptance-task.ps1"
$previewPath = Join-Path $resolvedTaskDir "pgsql-release-acceptance-task-preview.json"
$logPath = Join-Path $resolvedTaskDir "last-run.log"
$historyPath = Join-Path $resolvedTaskDir "automation-task-history.json"
$historyMarkdownPath = Join-Path $resolvedTaskDir "automation-task-history.md"
$ackPath = Join-Path $resolvedTaskDir "automation-task-ack.json"
$healthPath = Join-Path $resolvedOutputDir "database-health.json"
$healthStatusPath = Join-Path $resolvedOutputDir "database-health-status.json"
$healthStatusMarkdownPath = Join-Path $resolvedOutputDir "database-health-status.md"
$jsonPath = Join-Path $resolvedOutputDir "pgsql-release-acceptance.json"
$markdownPath = Join-Path $resolvedOutputDir "pgsql-release-acceptance.md"
$smokeMarkdownPath = Join-Path $resolvedOutputDir "pgsql-smoke.md"
$smokeBootstrapPath = Join-Path $resolvedOutputDir "pgsql-smoke-bootstrap.json"
$migrationMarkdownPath = Join-Path $resolvedOutputDir "sqlite-pg-migration-plan.md"
$migrationHtmlPath = Join-Path $resolvedOutputDir "sqlite-pg-migration-plan.html"
$rollbackPreviewMarkdownPath = Join-Path $resolvedOutputDir "sqlite-pg-rollback-preview.md"
$rollbackAuditMarkdownPath = Join-Path $resolvedOutputDir "sqlite-pg-rollback-audit.md"
$evidenceDir = Join-Path $resolvedOutputDir "evidence"
$evidencePackagePath = Join-Path $evidenceDir "pgsql-release-evidence.zip"
$evidenceManifestPath = Join-Path $evidenceDir "pgsql-release-evidence-manifest.json"

if ([string]::IsNullOrWhiteSpace($DatabaseHealthDashboardPath)) {
    $DatabaseHealthDashboardPath = Join-Path $resolvedOutputDir "database-health-dashboard.json"
}
if ([string]::IsNullOrWhiteSpace($SmokeJsonPath)) {
    $SmokeJsonPath = Join-Path $resolvedOutputDir "pgsql-smoke.json"
}
if ([string]::IsNullOrWhiteSpace($MigrationJsonPath)) {
    $MigrationJsonPath = Join-Path $resolvedOutputDir "sqlite-pg-migration-plan.json"
}
if ([string]::IsNullOrWhiteSpace($RollbackPreviewPath)) {
    $RollbackPreviewPath = Join-Path $resolvedOutputDir "sqlite-pg-rollback-preview.json"
}
if ([string]::IsNullOrWhiteSpace($RollbackAuditPath)) {
    $RollbackAuditPath = Join-Path $resolvedOutputDir "sqlite-pg-rollback-audit.json"
}

$lines = New-Object System.Collections.ArrayList
[void]$lines.Add('$ErrorActionPreference = "Stop"')
[void]$lines.Add('$repoRoot = ' + (Quote-PSString (Resolve-Path (Join-Path $PSScriptRoot "..")).Path))
[void]$lines.Add('Set-Location -LiteralPath $repoRoot')
[void]$lines.Add('$runStartedAt = (Get-Date).ToUniversalTime().ToString("o")')
[void]$lines.Add('$healthScript = ' + (Quote-PSString $healthScript))
[void]$lines.Add('$statusScript = ' + (Quote-PSString $statusScript))
[void]$lines.Add('$dashboardScript = ' + (Quote-PSString $dashboardScript))
[void]$lines.Add('$smokeScript = ' + (Quote-PSString $smokeScript))
[void]$lines.Add('$migrationScript = ' + (Quote-PSString $migrationScript))
[void]$lines.Add('$acceptanceScript = ' + (Quote-PSString $acceptanceScript))
[void]$lines.Add('$packageScript = ' + (Quote-PSString $packageScript))
[void]$lines.Add('$historyScript = ' + (Quote-PSString $historyScript))
[void]$lines.Add('$logPath = ' + (Quote-PSString $logPath))
[void]$lines.Add('$historyPath = ' + (Quote-PSString $historyPath))
[void]$lines.Add('$historyMarkdownPath = ' + (Quote-PSString $historyMarkdownPath))
[void]$lines.Add('$ackPath = ' + (Quote-PSString $ackPath))
[void]$lines.Add('$logDir = Split-Path -Parent $logPath')
[void]$lines.Add('if (-not [string]::IsNullOrWhiteSpace($logDir)) { New-Item -ItemType Directory -Path $logDir -Force | Out-Null }')
[void]$lines.Add('& powershell -ExecutionPolicy Bypass -File $healthScript `')
Add-ScalarArg $lines "Driver" "postgres"
Add-ScalarArg $lines "QtRoot" $QtRoot
Add-ScalarArg $lines "PostgresBinDir" $PostgresBinDir
Add-ScalarArg $lines "PostgresHost" $PostgresHost
Add-IntArg $lines "PostgresPort" $PostgresPort
Add-ScalarArg $lines "PostgresDatabase" $PostgresDatabase
Add-ScalarArg $lines "PostgresUser" $PostgresUser
if ($PlanOnly.IsPresent) {
    Add-SwitchArg $lines "PlanOnly" $true
} else {
    [void]$lines.Add("    -PostgresPassword `$env:QTNETWORKCHAT_PGPASSWORD ``")
}
Add-ScalarArg $lines "SQLitePath" $SQLitePath
Add-SwitchArg $lines "FailOnUnhealthy" $FailOnUnhealthy.IsPresent
Add-SwitchArg $lines "InjectSlowQueryProbe" $InjectSlowQueryProbe.IsPresent
Add-IntArg $lines "SlowQueryProbeSeconds" $SlowQueryProbeSeconds
if ($InjectQueryFailureReason -ne "none") {
    Add-ScalarArg $lines "InjectQueryFailureReason" $InjectQueryFailureReason
}
Add-ScalarArg $lines "JsonPath" $healthPath
$lastIndex = $lines.Count - 1
if ($lastIndex -ge 0) {
    $lastLine = ([string]$lines[$lastIndex]).TrimEnd()
    if ($lastLine.EndsWith([string][char]0x60)) {
        $lastLine = $lastLine.Substring(0, $lastLine.Length - 1).TrimEnd()
    }
    $lines[$lastIndex] = $lastLine
}
[void]$lines.Add('$healthExitCode = $LASTEXITCODE')
[void]$lines.Add('& powershell -ExecutionPolicy Bypass -File $statusScript `')
Add-ScalarArg $lines "HealthPath" $healthPath
Add-ScalarArg $lines "JsonPath" $healthStatusPath
Add-ScalarArg $lines "MarkdownPath" $healthStatusMarkdownPath
Add-SwitchArg $lines "FailOnUnhealthy" $FailOnUnhealthy.IsPresent
$lastIndex = $lines.Count - 1
if ($lastIndex -ge 0) {
    $lastLine = ([string]$lines[$lastIndex]).TrimEnd()
    if ($lastLine.EndsWith([string][char]0x60)) {
        $lastLine = $lastLine.Substring(0, $lastLine.Length - 1).TrimEnd()
    }
    $lines[$lastIndex] = $lastLine
}
[void]$lines.Add('$healthStatusExitCode = $LASTEXITCODE')
[void]$lines.Add('& powershell -ExecutionPolicy Bypass -File $dashboardScript `')
Add-ScalarArg $lines "HealthPath" $healthPath
Add-ScalarArg $lines "StatusPath" $healthStatusPath
Add-ScalarArg $lines "TaskPreviewPath" $previewPath
Add-ScalarArg $lines "DashboardPath" $DatabaseHealthDashboardPath
Add-ScalarArg $lines "MarkdownPath" (Join-Path $resolvedOutputDir "database-health-dashboard.md")
Add-SwitchArg $lines "FailOnUnhealthy" $FailOnUnhealthy.IsPresent
$lastIndex = $lines.Count - 1
if ($lastIndex -ge 0) {
    $lastLine = ([string]$lines[$lastIndex]).TrimEnd()
    if ($lastLine.EndsWith([string][char]0x60)) {
        $lastLine = $lastLine.Substring(0, $lastLine.Length - 1).TrimEnd()
    }
    $lines[$lastIndex] = $lastLine
}
[void]$lines.Add('$dashboardExitCode = $LASTEXITCODE')
[void]$lines.Add('& powershell -ExecutionPolicy Bypass -File $smokeScript `')
Add-ScalarArg $lines "TestExe" $TestExe
Add-ScalarArg $lines "QtRoot" $QtRoot
Add-ScalarArg $lines "PostgresBinDir" $PostgresBinDir
Add-ScalarArg $lines "PostgresHost" $PostgresHost
Add-IntArg $lines "PostgresPort" $PostgresPort
Add-ScalarArg $lines "PostgresDatabase" $PostgresDatabase
Add-ScalarArg $lines "PostgresUser" $PostgresUser
if ($PlanOnly.IsPresent) {
    Add-SwitchArg $lines "PlanOnly" $true
} else {
    [void]$lines.Add("    -PostgresPassword `$env:QTNETWORKCHAT_PGPASSWORD ``")
}
Add-ScalarArg $lines "AppDataDir" $AppDataDir
Add-SwitchArg $lines "EnsureDatabase" $EnsureDatabase.IsPresent
Add-ScalarArg $lines "BootstrapJsonPath" $smokeBootstrapPath
Add-ScalarArg $lines "JsonPath" $SmokeJsonPath
Add-ScalarArg $lines "MarkdownPath" $smokeMarkdownPath
$lastIndex = $lines.Count - 1
if ($lastIndex -ge 0) {
    $lastLine = ([string]$lines[$lastIndex]).TrimEnd()
    if ($lastLine.EndsWith([string][char]0x60)) {
        $lastLine = $lastLine.Substring(0, $lastLine.Length - 1).TrimEnd()
    }
    $lines[$lastIndex] = $lastLine
}
[void]$lines.Add('$smokeExitCode = $LASTEXITCODE')
[void]$lines.Add('& powershell -ExecutionPolicy Bypass -File $migrationScript `')
Add-ScalarArg $lines "MigratorExe" $MigratorExe
Add-ScalarArg $lines "SQLitePath" $SQLitePath
Add-ScalarArg $lines "QtRoot" $QtRoot
Add-ScalarArg $lines "PostgresBinDir" $PostgresBinDir
Add-ScalarArg $lines "PostgresHost" $PostgresHost
Add-IntArg $lines "PostgresPort" $PostgresPort
Add-ScalarArg $lines "PostgresDatabase" $PostgresDatabase
Add-ScalarArg $lines "PostgresUser" $PostgresUser
if ($MigrationMode -ne "plan") {
    [void]$lines.Add("    -PostgresPassword `$env:QTNETWORKCHAT_PGPASSWORD ``")
}
Add-ScalarArg $lines "Mode" $MigrationMode
Add-ScalarArg $lines "JsonPath" $MigrationJsonPath
Add-ScalarArg $lines "MarkdownPath" $migrationMarkdownPath
Add-ScalarArg $lines "HtmlPath" $migrationHtmlPath
Add-ScalarArg $lines "RollbackPreviewPath" $RollbackPreviewPath
Add-ScalarArg $lines "RollbackPreviewMarkdownPath" $rollbackPreviewMarkdownPath
Add-ScalarArg $lines "RollbackAuditPath" $RollbackAuditPath
Add-ScalarArg $lines "RollbackAuditMarkdownPath" $rollbackAuditMarkdownPath
$lastIndex = $lines.Count - 1
if ($lastIndex -ge 0) {
    $lastLine = ([string]$lines[$lastIndex]).TrimEnd()
    if ($lastLine.EndsWith([string][char]0x60)) {
        $lastLine = $lastLine.Substring(0, $lastLine.Length - 1).TrimEnd()
    }
    $lines[$lastIndex] = $lastLine
}
[void]$lines.Add('$migrationExitCode = $LASTEXITCODE')
[void]$lines.Add('& powershell -ExecutionPolicy Bypass -File $acceptanceScript `')
Add-ScalarArg $lines "DatabaseHealthDashboardPath" $DatabaseHealthDashboardPath
Add-ScalarArg $lines "SmokeJsonPath" $SmokeJsonPath
Add-ScalarArg $lines "MigrationJsonPath" $MigrationJsonPath
Add-ScalarArg $lines "RollbackPreviewPath" $RollbackPreviewPath
Add-ScalarArg $lines "RollbackAuditPath" $RollbackAuditPath
Add-ScalarArg $lines "JsonPath" $jsonPath
Add-ScalarArg $lines "MarkdownPath" $markdownPath
Add-SwitchArg $lines "FailOnUnhealthy" $FailOnUnhealthy.IsPresent
$lastIndex = $lines.Count - 1
if ($lastIndex -ge 0) {
    $lastLine = ([string]$lines[$lastIndex]).TrimEnd()
    if ($lastLine.EndsWith([string][char]0x60)) {
        $lastLine = $lastLine.Substring(0, $lastLine.Length - 1).TrimEnd()
    }
    $lines[$lastIndex] = $lastLine
}
[void]$lines.Add('$acceptanceExitCode = $LASTEXITCODE')
[void]$lines.Add('$pipelineExitCode = if ($healthExitCode -ne 0) { $healthExitCode } elseif ($healthStatusExitCode -ne 0) { $healthStatusExitCode } elseif ($dashboardExitCode -ne 0) { $dashboardExitCode } elseif ($smokeExitCode -ne 0) { $smokeExitCode } elseif ($migrationExitCode -ne 0) { $migrationExitCode } else { $acceptanceExitCode }')
[void]$lines.Add('$packageExitCode = 0')
[void]$lines.Add('$exitCode = $pipelineExitCode')
    [void]$lines.Add(('"$runStartedAt healthExitCode=$healthExitCode healthStatusExitCode=$healthStatusExitCode dashboardExitCode=$dashboardExitCode smokeExitCode=$smokeExitCode migrationExitCode=$migrationExitCode acceptanceExitCode=$acceptanceExitCode packageExitCode=$packageExitCode exitCode=$exitCode healthPath={0} healthStatusPath={1} dashboardPath={2} smokeJsonPath={3} migrationJsonPath={4} rollbackPreviewPath={5} rollbackAuditPath={6} jsonPath={7} markdownPath={8} evidencePackagePath={9} evidenceManifestPath={10} historyPath={11} historyMarkdownPath={12} ackPath={13}" | Set-Content -LiteralPath $logPath -Encoding UTF8' -f $healthPath, $healthStatusPath, $DatabaseHealthDashboardPath, $SmokeJsonPath, $MigrationJsonPath, $RollbackPreviewPath, $RollbackAuditPath, $jsonPath, $markdownPath, $evidencePackagePath, $evidenceManifestPath, $historyPath, $historyMarkdownPath, $ackPath))
[void]$lines.Add('& powershell -ExecutionPolicy Bypass -File $historyScript `')
Add-ScalarArg $lines "LastRunPath" $logPath
Add-ScalarArg $lines "AckPath" $ackPath
Add-IntArg $lines "AckExpiryHours" $AckExpiryHours
Add-ScalarArg $lines "JsonPath" $historyPath
Add-ScalarArg $lines "MarkdownPath" $historyMarkdownPath
Add-SwitchArg $lines "FailOnSensitive" $true
$lastIndex = $lines.Count - 1
if ($lastIndex -ge 0) {
    $lastLine = ([string]$lines[$lastIndex]).TrimEnd()
    if ($lastLine.EndsWith([string][char]0x60)) {
        $lastLine = $lastLine.Substring(0, $lastLine.Length - 1).TrimEnd()
    }
    $lines[$lastIndex] = $lastLine
}
[void]$lines.Add('$historyExitCode = $LASTEXITCODE')
if (-not $SkipEvidencePackage.IsPresent) {
    [void]$lines.Add('& powershell -ExecutionPolicy Bypass -File $packageScript `')
    Add-ScalarArg $lines "OutputDir" $evidenceDir
    Add-ScalarArg $lines "PackagePath" $evidencePackagePath
    Add-ScalarArg $lines "ManifestPath" $evidenceManifestPath
    Add-ScalarArg $lines "DatabaseHealthPath" $healthPath
    Add-ScalarArg $lines "DatabaseHealthStatusPath" $healthStatusPath
    Add-ScalarArg $lines "DatabaseHealthDashboardPath" $DatabaseHealthDashboardPath
    Add-ScalarArg $lines "SmokeJsonPath" $SmokeJsonPath
    Add-ScalarArg $lines "SmokeMarkdownPath" $smokeMarkdownPath
    Add-ScalarArg $lines "SmokeBootstrapJsonPath" $smokeBootstrapPath
    Add-ScalarArg $lines "MigrationJsonPath" $MigrationJsonPath
    Add-ScalarArg $lines "MigrationMarkdownPath" $migrationMarkdownPath
    Add-ScalarArg $lines "MigrationHtmlPath" $migrationHtmlPath
    Add-ScalarArg $lines "RollbackPreviewPath" $RollbackPreviewPath
    Add-ScalarArg $lines "RollbackPreviewMarkdownPath" $rollbackPreviewMarkdownPath
    Add-ScalarArg $lines "RollbackAuditPath" $RollbackAuditPath
    Add-ScalarArg $lines "RollbackAuditMarkdownPath" $rollbackAuditMarkdownPath
    Add-ScalarArg $lines "AcceptanceJsonPath" $jsonPath
    Add-ScalarArg $lines "AcceptanceMarkdownPath" $markdownPath
    Add-ScalarArg $lines "LastRunPath" $logPath
    Add-ScalarArg $lines "HistoryPath" $historyPath
    Add-ScalarArg $lines "HistoryMarkdownPath" $historyMarkdownPath
    Add-ScalarArg $lines "AckPath" $ackPath
    $lastIndex = $lines.Count - 1
    if ($lastIndex -ge 0) {
        $lastLine = ([string]$lines[$lastIndex]).TrimEnd()
        if ($lastLine.EndsWith([string][char]0x60)) {
            $lastLine = $lastLine.Substring(0, $lastLine.Length - 1).TrimEnd()
        }
        $lines[$lastIndex] = $lastLine
    }
    [void]$lines.Add('$packageExitCode = $LASTEXITCODE')
}
[void]$lines.Add('$finalExitCode = if ($pipelineExitCode -ne 0) { $pipelineExitCode } elseif ($historyExitCode -ne 0) { $historyExitCode } else { $packageExitCode }')
[void]$lines.Add('exit $finalExitCode')

$lines | Set-Content -LiteralPath $launcherPath -Encoding UTF8

$actionArgument = "-NoProfile -ExecutionPolicy Bypass -File `"$launcherPath`""
$preview = [pscustomobject]@{
    format = "qtnetworkchat-pgsql-release-acceptance-task-preview-v1"
    taskKind = "pgsql-release-acceptance"
    taskName = $TaskName
    taskDisplayName = "PostgreSQL release acceptance"
    taskSummary = "Read-only PostgreSQL release acceptance summary that merges redacted health, smoke, migration, rollback preview, last-run, and task history artifacts."
    register = $Register.IsPresent
    schedule = $Schedule
    at = $At
    everyHours = if ($Schedule -eq "Hourly") { $EveryHours } else { $null }
    user = $User
    action = "powershell.exe $actionArgument"
    launcherPath = $launcherPath
    healthScript = $healthScript
    statusScript = $statusScript
    dashboardScript = $dashboardScript
    smokeScript = $smokeScript
    migrationScript = $migrationScript
    acceptanceScript = $acceptanceScript
    packageScript = $packageScript
    historyScript = $historyScript
    outputDir = $resolvedOutputDir
    healthPath = $healthPath
    healthStatusPath = $healthStatusPath
    databaseHealthDashboardPath = $DatabaseHealthDashboardPath
    smokeJsonPath = $SmokeJsonPath
    smokeMarkdownPath = $smokeMarkdownPath
    smokeBootstrapJsonPath = $smokeBootstrapPath
    migrationJsonPath = $MigrationJsonPath
    migrationMarkdownPath = $migrationMarkdownPath
    migrationHtmlPath = $migrationHtmlPath
    rollbackPreviewPath = $RollbackPreviewPath
    rollbackPreviewMarkdownPath = $rollbackPreviewMarkdownPath
    rollbackAuditPath = $RollbackAuditPath
    rollbackAuditMarkdownPath = $rollbackAuditMarkdownPath
    statusArtifactPath = $jsonPath
    jsonPath = $jsonPath
    markdownPath = $markdownPath
    evidencePackagePath = $evidencePackagePath
    evidenceManifestPath = $evidenceManifestPath
    lastRunPath = $logPath
    logPath = $logPath
    historyPath = $historyPath
    historyArtifactPath = $historyPath
    historyMarkdownPath = $historyMarkdownPath
    ackPath = $ackPath
    ackArtifactPath = $ackPath
    artifactRoles = [ordered]@{
        status = "statusArtifactPath"
        lastRun = "lastRunPath"
        history = "historyArtifactPath"
        ack = "ackArtifactPath"
        evidence = "evidencePackagePath"
    }
    artifacts = [ordered]@{
        status = [ordered]@{
            path = $jsonPath
            markdownPath = $markdownPath
        }
        lastRun = [ordered]@{
            path = $logPath
        }
        history = [ordered]@{
            path = $historyPath
            markdownPath = $historyMarkdownPath
        }
        ack = [ordered]@{
            path = $ackPath
        }
        evidence = [ordered]@{
            path = $evidencePackagePath
            manifestPath = $evidenceManifestPath
        }
    }
    qtRoot = $QtRoot
    postgresBinDir = $PostgresBinDir
    postgresHost = $PostgresHost
    postgresPort = $PostgresPort
    postgresDatabase = $PostgresDatabase
    postgresUser = $PostgresUser
    sqlitePath = $SQLitePath
    testExe = $TestExe
    migratorExe = $MigratorExe
    appDataDir = $AppDataDir
    planOnly = $PlanOnly.IsPresent
    ensureDatabase = $EnsureDatabase.IsPresent
    migrationMode = $MigrationMode
    injectSlowQueryProbe = $InjectSlowQueryProbe.IsPresent
    slowQueryProbeSeconds = $SlowQueryProbeSeconds
    injectQueryFailureReason = $InjectQueryFailureReason
    packageEvidence = (-not $SkipEvidencePackage.IsPresent)
    passwordSource = "QTNETWORKCHAT_PGPASSWORD"
    failOnUnhealthy = $FailOnUnhealthy.IsPresent
    readOnly = $true
    notes = "Default mode writes this preview and launcher script only. The launcher reads PostgreSQL password from QTNETWORKCHAT_PGPASSWORD at run time, runs database health/status/dashboard, PostgreSQL QPSQL smoke, SQLite-to-PostgreSQL migration diff/rollback preview, writes unified release acceptance JSON/Markdown, packages redacted evidence, and records last-run plus automation task history/ack artifacts."
}
$preview | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $previewPath -Encoding UTF8

if ($Register) {
    $action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $actionArgument
    if ($Schedule -eq "Daily") {
        $trigger = New-ScheduledTaskTrigger -Daily -At $At
    } else {
        $trigger = New-ScheduledTaskTrigger -Once -At $At -RepetitionInterval (New-TimeSpan -Hours $EveryHours)
    }
    $description = "QtNetworkChat PostgreSQL release acceptance summary. Read-only; writes redacted release readiness artifacts."
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Description $description -User $User -Force | Out-Null
    Write-Host ("registered scheduled task: {0}" -f $TaskName)
} else {
    Write-Host "preview only; scheduled task was not registered"
}

Write-Host ("launcher: {0}" -f $launcherPath)
Write-Host ("preview: {0}" -f $previewPath)
