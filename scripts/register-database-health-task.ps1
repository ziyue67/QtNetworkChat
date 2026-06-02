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

$resolvedTaskDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($TaskDir)
New-Item -ItemType Directory -Path $resolvedTaskDir -Force | Out-Null

$healthScript = Join-Path $PSScriptRoot "check-database-health.ps1"
$statusScript = Join-Path $PSScriptRoot "show-database-health-status.ps1"
$dashboardScript = Join-Path $PSScriptRoot "write-database-health-dashboard.ps1"
if (-not (Test-Path -LiteralPath $healthScript -PathType Leaf)) {
    throw "Database health checker not found: $healthScript"
}
if (-not (Test-Path -LiteralPath $statusScript -PathType Leaf)) {
    throw "Database health status script not found: $statusScript"
}
if (-not (Test-Path -LiteralPath $dashboardScript -PathType Leaf)) {
    throw "Database health dashboard script not found: $dashboardScript"
}

$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

$launcherPath = Join-Path $resolvedTaskDir "run-database-health-task.ps1"
$previewPath = Join-Path $resolvedTaskDir "database-health-task-preview.json"
$logPath = Join-Path $resolvedTaskDir "last-run.log"
$historyPath = Join-Path $resolvedTaskDir "automation-task-history.json"
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
[void]$lines.Add('$healthScript = ' + (Quote-PSString $healthScript))
[void]$lines.Add('$statusScript = ' + (Quote-PSString $statusScript))
[void]$lines.Add('$dashboardScript = ' + (Quote-PSString $dashboardScript))
[void]$lines.Add('$logPath = ' + (Quote-PSString $logPath))
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
[void]$lines.Add(('"{0} healthExitCode=$healthExitCode statusExitCode=$statusExitCode dashboardExitCode=$dashboardExitCode exitCode=$exitCode healthPath={1} statusPath={2} dashboardPath={3} markdownPath={4}" | Set-Content -LiteralPath $logPath -Encoding UTF8' -f (Get-Date).ToUniversalTime().ToString("o"), $healthPath, $statusPath, $dashboardPath, $markdownPath))
[void]$lines.Add('exit $exitCode')

$lines | Set-Content -LiteralPath $launcherPath -Encoding UTF8

$actionArgument = "-NoProfile -ExecutionPolicy Bypass -File `"$launcherPath`""
$preview = [pscustomobject]@{
    format = "qtnetworkchat-database-health-task-preview-v1"
    taskName = $TaskName
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
    outputDir = $resolvedOutputDir
    healthPath = $healthPath
    statusPath = $statusPath
    markdownPath = $markdownPath
    writeDashboard = [bool]$dashboardEnabled
    dashboardPath = $dashboardPath
    dashboardMarkdownPath = $dashboardMarkdownPath
    logPath = $logPath
    historyPath = $historyPath
    ackPath = $ackPath
    driver = $Driver
    planOnly = $PlanOnly.IsPresent
    failOnUnhealthy = $FailOnUnhealthy.IsPresent
    passwordSource = if ($Driver -eq "postgres") { "QTNETWORKCHAT_PGPASSWORD" } else { "" }
    readOnly = $true
    notes = "Default mode writes this preview and launcher script only. The launcher reads PostgreSQL password from QTNETWORKCHAT_PGPASSWORD at run time and writes redacted database health JSON, status JSON/Markdown, optional dashboard JSON/Markdown, and a last-run log with exit codes and artifact paths."
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
