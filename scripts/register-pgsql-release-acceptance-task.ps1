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

    [string]$DatabaseHealthDashboardPath,

    [string]$SmokeJsonPath,

    [string]$MigrationJsonPath,

    [string]$RollbackPreviewPath,

    [switch]$FailOnUnhealthy,

    [switch]$Register,

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
if ($Schedule -eq "Hourly" -and $EveryHours -lt 1) {
    throw "EveryHours must be greater than zero."
}

Assert-NoSensitiveValue "TaskName" @($TaskName)
Assert-NoSensitiveValue "OutputDir" @($OutputDir)
Assert-NoSensitiveValue "TaskDir" @($TaskDir)
Assert-NoSensitiveValue "DatabaseHealthDashboardPath" @($DatabaseHealthDashboardPath)
Assert-NoSensitiveValue "SmokeJsonPath" @($SmokeJsonPath)
Assert-NoSensitiveValue "MigrationJsonPath" @($MigrationJsonPath)
Assert-NoSensitiveValue "RollbackPreviewPath" @($RollbackPreviewPath)

if ([string]::IsNullOrWhiteSpace($TaskDir)) {
    $TaskDir = Join-Path $OutputDir "pgsql-release-acceptance-task"
}

$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
$resolvedTaskDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($TaskDir)
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null
New-Item -ItemType Directory -Path $resolvedTaskDir -Force | Out-Null

$acceptanceScript = Join-Path $PSScriptRoot "write-pgsql-release-acceptance.ps1"
$historyScript = Join-Path $PSScriptRoot "write-automation-task-history.ps1"
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
$jsonPath = Join-Path $resolvedOutputDir "pgsql-release-acceptance.json"
$markdownPath = Join-Path $resolvedOutputDir "pgsql-release-acceptance.md"

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

$lines = New-Object System.Collections.ArrayList
[void]$lines.Add('$ErrorActionPreference = "Stop"')
[void]$lines.Add('$acceptanceScript = ' + (Quote-PSString $acceptanceScript))
[void]$lines.Add('$historyScript = ' + (Quote-PSString $historyScript))
[void]$lines.Add('$logPath = ' + (Quote-PSString $logPath))
[void]$lines.Add('$historyPath = ' + (Quote-PSString $historyPath))
[void]$lines.Add('$historyMarkdownPath = ' + (Quote-PSString $historyMarkdownPath))
[void]$lines.Add('$ackPath = ' + (Quote-PSString $ackPath))
[void]$lines.Add('$logDir = Split-Path -Parent $logPath')
[void]$lines.Add('if (-not [string]::IsNullOrWhiteSpace($logDir)) { New-Item -ItemType Directory -Path $logDir -Force | Out-Null }')
[void]$lines.Add('& powershell -ExecutionPolicy Bypass -File $acceptanceScript `')
Add-ScalarArg $lines "DatabaseHealthDashboardPath" $DatabaseHealthDashboardPath
Add-ScalarArg $lines "SmokeJsonPath" $SmokeJsonPath
Add-ScalarArg $lines "MigrationJsonPath" $MigrationJsonPath
Add-ScalarArg $lines "RollbackPreviewPath" $RollbackPreviewPath
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
[void]$lines.Add(('"{0} acceptanceExitCode=$acceptanceExitCode exitCode=$acceptanceExitCode jsonPath={1} markdownPath={2} historyPath={3} historyMarkdownPath={4} ackPath={5}" | Set-Content -LiteralPath $logPath -Encoding UTF8' -f (Get-Date).ToUniversalTime().ToString("o"), $jsonPath, $markdownPath, $historyPath, $historyMarkdownPath, $ackPath))
[void]$lines.Add('& powershell -ExecutionPolicy Bypass -File $historyScript `')
Add-ScalarArg $lines "LastRunPath" $logPath
Add-ScalarArg $lines "AckPath" $ackPath
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
[void]$lines.Add('$finalExitCode = if ($acceptanceExitCode -ne 0) { $acceptanceExitCode } else { $historyExitCode }')
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
    acceptanceScript = $acceptanceScript
    historyScript = $historyScript
    outputDir = $resolvedOutputDir
    databaseHealthDashboardPath = $DatabaseHealthDashboardPath
    smokeJsonPath = $SmokeJsonPath
    migrationJsonPath = $MigrationJsonPath
    rollbackPreviewPath = $RollbackPreviewPath
    statusArtifactPath = $jsonPath
    jsonPath = $jsonPath
    markdownPath = $markdownPath
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
    }
    failOnUnhealthy = $FailOnUnhealthy.IsPresent
    readOnly = $true
    notes = "Default mode writes this preview and launcher script only. The launcher reads local redacted database health dashboard, PostgreSQL smoke JSON, migration JSON, and rollback preview JSON, then writes a unified PostgreSQL release acceptance JSON/Markdown plus last-run and automation task history artifacts."
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
