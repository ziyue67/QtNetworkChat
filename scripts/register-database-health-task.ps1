param(
    [string]$TaskName = "QtNetworkChatDatabaseHealth",

    [ValidateSet("Daily", "Hourly")]
    [string]$Schedule = "Daily",

    [string]$At = "03:15",

    [int]$EveryHours = 1,

    [ValidateSet("sqlite", "postgres")]
    [string]$Driver = "postgres",

    [string]$QtRoot = "D:\Qt\6.8.3\mingw_64",

    [string]$PostgresBinDir = "D:\Program Files\PostgreSQL\17\bin",

    [string]$PostgresHost = "127.0.0.1",

    [int]$PostgresPort = 5432,

    [string]$PostgresDatabase = "qtnetworkchat",

    [string]$PostgresUser = "postgres",

    [string]$SQLitePath = "accounts.sqlite3",

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$TaskDir,

    [int]$AckExpiryHours = 72,

    [switch]$PlanOnly,

    [switch]$FailOnUnhealthy,

    [switch]$WriteMarkdown,

    [switch]$WriteDashboard,

    [string]$DashboardPath,

    [string]$DashboardMarkdownPath,

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
                throw ("{0} contains sensitive-looking text rejected by database health scheduled task helper: {1}" -f $Label, $pattern)
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

function Add-RawArg([System.Collections.ArrayList]$Lines, [string]$Name, [string]$Value) {
    if (-not [string]::IsNullOrWhiteSpace($Value)) {
        [void]$Lines.Add(("    -{0} {1} ``" -f $Name, $Value))
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
if ($PostgresPort -lt 1 -or $PostgresPort -gt 65535) {
    throw "PostgresPort must be between 1 and 65535."
}

Assert-NoSensitiveValue "TaskName" @($TaskName)
Assert-NoSensitiveValue "QtRoot" @($QtRoot)
Assert-NoSensitiveValue "PostgresBinDir" @($PostgresBinDir)
Assert-NoSensitiveValue "PostgresHost" @($PostgresHost)
Assert-NoSensitiveValue "PostgresDatabase" @($PostgresDatabase)
Assert-NoSensitiveValue "PostgresUser" @($PostgresUser)
Assert-NoSensitiveValue "SQLitePath" @($SQLitePath)
Assert-NoSensitiveValue "OutputDir" @($OutputDir)
Assert-NoSensitiveValue "TaskDir" @($TaskDir)
Assert-NoSensitiveValue "DashboardPath" @($DashboardPath)
Assert-NoSensitiveValue "DashboardMarkdownPath" @($DashboardMarkdownPath)

if ([string]::IsNullOrWhiteSpace($TaskDir)) {
    $TaskDir = Join-Path $OutputDir "database-health-task"
}
if ($AckExpiryHours -lt 1) {
    throw "AckExpiryHours must be greater than zero."
}

$resolvedTaskDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($TaskDir)
New-Item -ItemType Directory -Path $resolvedTaskDir -Force | Out-Null

$healthScript = Join-Path $PSScriptRoot "check-database-health.ps1"
$statusScript = Join-Path $PSScriptRoot "show-database-health-status.ps1"
$dashboardScript = Join-Path $PSScriptRoot "write-database-health-dashboard.ps1"
$historyScript = Join-Path $PSScriptRoot "write-automation-task-history.ps1"
if (-not (Test-Path -LiteralPath $healthScript -PathType Leaf)) {
    throw "Database health checker not found: $healthScript"
}
if (-not (Test-Path -LiteralPath $statusScript -PathType Leaf)) {
    throw "Database health status script not found: $statusScript"
}
if (-not (Test-Path -LiteralPath $dashboardScript -PathType Leaf)) {
    throw "Database health dashboard script not found: $dashboardScript"
}
if (-not (Test-Path -LiteralPath $historyScript -PathType Leaf)) {
    throw "Automation task history writer not found: $historyScript"
}

$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

$launcherPath = Join-Path $resolvedTaskDir "run-database-health-task.ps1"
$previewPath = Join-Path $resolvedTaskDir "database-health-task-preview.json"
$logPath = Join-Path $resolvedTaskDir "last-run.log"
$historyPath = Join-Path $resolvedTaskDir "automation-task-history.json"
$historyMarkdownPath = Join-Path $resolvedTaskDir "automation-task-history.md"
$ackPath = Join-Path $resolvedTaskDir "automation-task-ack.json"
$healthPath = Join-Path $resolvedOutputDir "database-health.json"
$statusPath = Join-Path $resolvedOutputDir "database-health-status.json"
$markdownPath = if ($WriteMarkdown) { Join-Path $resolvedOutputDir "database-health-status.md" } else { "" }
$dashboardEnabled = $WriteDashboard.IsPresent -or -not [string]::IsNullOrWhiteSpace($DashboardPath) -or -not [string]::IsNullOrWhiteSpace($DashboardMarkdownPath)
$dashboardPath = if ($dashboardEnabled) {
    if ([string]::IsNullOrWhiteSpace($DashboardPath)) {
        Join-Path $resolvedOutputDir "database-health-dashboard.json"
    } else {
        $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DashboardPath)
    }
} else {
    ""
}
$dashboardMarkdownPath = if ($dashboardEnabled) {
    if ([string]::IsNullOrWhiteSpace($DashboardMarkdownPath)) {
        Join-Path $resolvedOutputDir "database-health-dashboard.md"
    } else {
        $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DashboardMarkdownPath)
    }
} else {
    ""
}

$lines = New-Object System.Collections.ArrayList
[void]$lines.Add('$ErrorActionPreference = "Stop"')
[void]$lines.Add('$repoRoot = ' + (Quote-PSString (Resolve-Path (Join-Path $PSScriptRoot "..")).Path))
[void]$lines.Add('Set-Location -LiteralPath $repoRoot')
[void]$lines.Add('$runStartedAt = (Get-Date).ToUniversalTime().ToString("o")')
[void]$lines.Add('$healthScript = ' + (Quote-PSString $healthScript))
[void]$lines.Add('$statusScript = ' + (Quote-PSString $statusScript))
[void]$lines.Add('$dashboardScript = ' + (Quote-PSString $dashboardScript))
[void]$lines.Add('$historyScript = ' + (Quote-PSString $historyScript))
[void]$lines.Add('$logPath = ' + (Quote-PSString $logPath))
[void]$lines.Add('$historyPath = ' + (Quote-PSString $historyPath))
[void]$lines.Add('$historyMarkdownPath = ' + (Quote-PSString $historyMarkdownPath))
[void]$lines.Add('$ackPath = ' + (Quote-PSString $ackPath))
[void]$lines.Add('$logDir = Split-Path -Parent $logPath')
[void]$lines.Add('if (-not [string]::IsNullOrWhiteSpace($logDir)) { New-Item -ItemType Directory -Path $logDir -Force | Out-Null }')
[void]$lines.Add('& powershell -ExecutionPolicy Bypass -File $healthScript `')
Add-ScalarArg $lines "Driver" $Driver
Add-ScalarArg $lines "QtRoot" $QtRoot
Add-ScalarArg $lines "PostgresBinDir" $PostgresBinDir
Add-ScalarArg $lines "PostgresHost" $PostgresHost
Add-IntArg $lines "PostgresPort" $PostgresPort
Add-ScalarArg $lines "PostgresDatabase" $PostgresDatabase
Add-ScalarArg $lines "PostgresUser" $PostgresUser
Add-RawArg $lines "PostgresPassword" '$env:QTNETWORKCHAT_PGPASSWORD'
Add-ScalarArg $lines "SQLitePath" $SQLitePath
Add-SwitchArg $lines "PlanOnly" $PlanOnly.IsPresent
Add-SwitchArg $lines "FailOnUnhealthy" $FailOnUnhealthy.IsPresent
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
Add-ScalarArg $lines "JsonPath" $statusPath
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
[void]$lines.Add('$statusExitCode = $LASTEXITCODE')
if ($dashboardEnabled) {
    [void]$lines.Add('& powershell -ExecutionPolicy Bypass -File $dashboardScript `')
    Add-ScalarArg $lines "HealthPath" $healthPath
    Add-ScalarArg $lines "StatusPath" $statusPath
    Add-ScalarArg $lines "TaskPreviewPath" $previewPath
    Add-ScalarArg $lines "DashboardPath" $dashboardPath
    Add-ScalarArg $lines "MarkdownPath" $dashboardMarkdownPath
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
} else {
    [void]$lines.Add('$dashboardExitCode = 0')
}
[void]$lines.Add('$exitCode = if ($healthExitCode -ne 0) { $healthExitCode } elseif ($statusExitCode -ne 0) { $statusExitCode } else { $dashboardExitCode }')
[void]$lines.Add(('"$runStartedAt healthExitCode=$healthExitCode statusExitCode=$statusExitCode dashboardExitCode=$dashboardExitCode exitCode=$exitCode healthPath={0} statusPath={1} dashboardPath={2} markdownPath={3} historyPath={4} historyMarkdownPath={5} ackPath={6}" | Set-Content -LiteralPath $logPath -Encoding UTF8' -f $healthPath, $statusPath, $dashboardPath, $markdownPath, $historyPath, $historyMarkdownPath, $ackPath))
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
[void]$lines.Add('$finalExitCode = if ($exitCode -ne 0) { $exitCode } else { $historyExitCode }')
[void]$lines.Add('exit $finalExitCode')

$lines | Set-Content -LiteralPath $launcherPath -Encoding UTF8

$actionArgument = "-NoProfile -ExecutionPolicy Bypass -File `"$launcherPath`""
$preview = [pscustomobject]@{
    format = "qtnetworkchat-database-health-task-preview-v1"
    taskKind = "database-health"
    taskName = $TaskName
    taskDisplayName = "Database health"
    taskSummary = "Read-only database health check that writes redacted health, status, optional dashboard, last-run, and task history artifacts."
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
    historyScript = $historyScript
    outputDir = $resolvedOutputDir
    healthPath = $healthPath
    statusPath = $statusPath
    statusArtifactPath = $statusPath
    markdownPath = $markdownPath
    writeDashboard = [bool]$dashboardEnabled
    dashboardPath = $dashboardPath
    dashboardMarkdownPath = $dashboardMarkdownPath
    logPath = $logPath
    lastRunPath = $logPath
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
            path = $statusPath
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
    driver = $Driver
    planOnly = $PlanOnly.IsPresent
    failOnUnhealthy = $FailOnUnhealthy.IsPresent
    passwordSource = if ($Driver -eq "postgres") { "QTNETWORKCHAT_PGPASSWORD" } else { "" }
    readOnly = $true
    notes = "Default mode writes this preview and launcher script only. The launcher reads PostgreSQL password from QTNETWORKCHAT_PGPASSWORD at run time and writes redacted database health JSON, status JSON/Markdown, optional dashboard JSON/Markdown, a last-run log with exit codes and artifact paths, and automation task history JSON/Markdown derived from last-run.log plus same-directory ack state."
}
$preview | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $previewPath -Encoding UTF8

if ($Register) {
    $action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $actionArgument
    if ($Schedule -eq "Daily") {
        $trigger = New-ScheduledTaskTrigger -Daily -At $At
    } else {
        $trigger = New-ScheduledTaskTrigger -Once -At $At -RepetitionInterval (New-TimeSpan -Hours $EveryHours)
    }
    $description = "QtNetworkChat database health checker. Read-only; writes redacted health/status artifacts."
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Description $description -User $User -Force | Out-Null
    Write-Host ("registered scheduled task: {0}" -f $TaskName)
} else {
    Write-Host "preview only; scheduled task was not registered"
}

Write-Host ("launcher: {0}" -f $launcherPath)
Write-Host ("preview: {0}" -f $previewPath)
