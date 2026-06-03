param(
    [string]$TaskName = "QtNetworkChatLargeFileGovernance",

    [ValidateSet("Daily", "Hourly")]
    [string]$Schedule = "Daily",

    [string]$At = "03:00",

    [int]$EveryHours = 1,

    [string[]]$RouteLogPath,

    [string]$ReceiptPath,

    [Parameter(Mandatory = $true)]
    [string[]]$QueuePath,

    [Parameter(Mandatory = $true)]
    [string]$SourceInstanceId,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$TaskDir,

    [string]$ReceiptRotationPath,

    [int]$RotationKeepRecords,

    [int]$RotationMaxAgeDays,

    [switch]$CompressRotationArchive,

    [string]$NotesPath,

    [switch]$PackageAcceptance,

    [string]$PackagePath,

    [switch]$PackageDiagnostics,

    [string]$DiagnosticsPackagePath,

    [switch]$WriteReport,

    [string]$ReportPath,

    [string]$HtmlReportPath,

    [switch]$WriteDashboard,

    [string]$DashboardPath,

    [string]$DashboardMarkdownPath,

    [switch]$WriteS3StabilityRunbook,

    [string]$S3StabilityRunbookPath,

    [string]$S3StabilityRunbookMarkdownPath,

    [string]$S3CoveragePolicyPath,

    [switch]$WarnS3CoverageGaps,

    [switch]$RunS3FailureBatchSample,

    [int]$S3FailureBatchCountPerReason = 2,

    [switch]$EmitRouteLog,

    [switch]$NoFailOnWarning,

    [switch]$NoFailOnSensitive,

    [switch]$Register,

    [string]$User = "SYSTEM"
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    "endpoint\s*=",
    "bucket\s*=",
    "objectUrl\s*=",
    "object-url\s*=",
    "https?://",
    "access[-_\s]?key",
    "secret[-_\s]?key",
    "session[-_\s]?token",
    "Authorization",
    "Credential",
    "Signature"
)

function Assert-NoSensitiveValue([string]$Label, [string[]]$Values) {
    foreach ($value in $Values) {
        if ([string]::IsNullOrWhiteSpace($value)) {
            continue
        }
        foreach ($pattern in $sensitivePatterns) {
            if ($value -match $pattern) {
                throw ("{0} contains sensitive-looking text rejected by scheduled task helper: {1}" -f $Label, $pattern)
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

function Add-ArrayArg([System.Collections.ArrayList]$Lines, [string]$Name, [string[]]$Values) {
    if ($null -eq $Values -or $Values.Count -eq 0) {
        return
    }
    $quoted = @()
    foreach ($value in $Values) {
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            $quoted += (Quote-PSString $value)
        }
    }
    if ($quoted.Count -gt 0) {
        [void]$Lines.Add(("    -{0} {1} ``" -f $Name, ($quoted -join ", ")))
    }
}

function Add-SwitchArg([System.Collections.ArrayList]$Lines, [string]$Name, [bool]$Enabled) {
    if ($Enabled) {
        [void]$Lines.Add(("    -{0} ``" -f $Name))
    }
}

if (($null -eq $RouteLogPath -or $RouteLogPath.Count -eq 0) -and [string]::IsNullOrWhiteSpace($ReceiptPath)) {
    throw "Either RouteLogPath or ReceiptPath is required."
}
if (($null -ne $RouteLogPath -and $RouteLogPath.Count -gt 0) -and -not [string]::IsNullOrWhiteSpace($ReceiptPath)) {
    throw "Use either RouteLogPath or ReceiptPath, not both."
}
if ([string]::IsNullOrWhiteSpace($SourceInstanceId)) {
    throw "SourceInstanceId is required."
}
if ($null -eq $QueuePath -or $QueuePath.Count -eq 0) {
    throw "QueuePath is required."
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    throw "OutputDir is required."
}
if ($Schedule -eq "Hourly" -and $EveryHours -lt 1) {
    throw "EveryHours must be greater than zero."
}
if ([string]::IsNullOrWhiteSpace($ReceiptRotationPath) -and ($RotationKeepRecords -gt 0 -or $RotationMaxAgeDays -gt 0 -or $CompressRotationArchive)) {
    throw "ReceiptRotationPath is required when receipt rotation options are set."
}
if ($S3FailureBatchCountPerReason -lt 1) {
    throw "S3FailureBatchCountPerReason must be 1 or greater."
}

Assert-NoSensitiveValue "TaskName" @($TaskName)
Assert-NoSensitiveValue "RouteLogPath" $RouteLogPath
Assert-NoSensitiveValue "ReceiptPath" @($ReceiptPath)
Assert-NoSensitiveValue "QueuePath" $QueuePath
Assert-NoSensitiveValue "SourceInstanceId" @($SourceInstanceId)
Assert-NoSensitiveValue "OutputDir" @($OutputDir)
Assert-NoSensitiveValue "TaskDir" @($TaskDir)
Assert-NoSensitiveValue "ReceiptRotationPath" @($ReceiptRotationPath)
Assert-NoSensitiveValue "NotesPath" @($NotesPath)
Assert-NoSensitiveValue "PackagePath" @($PackagePath)
Assert-NoSensitiveValue "DiagnosticsPackagePath" @($DiagnosticsPackagePath)
Assert-NoSensitiveValue "ReportPath" @($ReportPath)
Assert-NoSensitiveValue "HtmlReportPath" @($HtmlReportPath)
Assert-NoSensitiveValue "DashboardPath" @($DashboardPath)
Assert-NoSensitiveValue "DashboardMarkdownPath" @($DashboardMarkdownPath)
Assert-NoSensitiveValue "S3StabilityRunbookPath" @($S3StabilityRunbookPath)
Assert-NoSensitiveValue "S3StabilityRunbookMarkdownPath" @($S3StabilityRunbookMarkdownPath)
Assert-NoSensitiveValue "S3CoveragePolicyPath" @($S3CoveragePolicyPath)

if ([string]::IsNullOrWhiteSpace($TaskDir)) {
    $TaskDir = Join-Path $OutputDir "scheduled-task"
}

$resolvedTaskDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($TaskDir)
New-Item -ItemType Directory -Path $resolvedTaskDir -Force | Out-Null

$governanceScript = Join-Path $PSScriptRoot "run-large-file-governance.ps1"
$historyScript = Join-Path $PSScriptRoot "write-automation-task-history.ps1"
if (-not (Test-Path -LiteralPath $governanceScript -PathType Leaf)) {
    throw "Governance runner not found: $governanceScript"
}
if (-not (Test-Path -LiteralPath $historyScript -PathType Leaf)) {
    throw "Automation task history writer not found: $historyScript"
}

$launcherPath = Join-Path $resolvedTaskDir "run-large-file-governance-task.ps1"
$previewPath = Join-Path $resolvedTaskDir "scheduled-task-preview.json"
$logPath = Join-Path $resolvedTaskDir "last-run.log"
$historyPath = Join-Path $resolvedTaskDir "automation-task-history.json"
$historyMarkdownPath = Join-Path $resolvedTaskDir "automation-task-history.md"
$ackPath = Join-Path $resolvedTaskDir "automation-task-ack.json"

$lines = New-Object System.Collections.ArrayList
[void]$lines.Add('$ErrorActionPreference = "Stop"')
[void]$lines.Add('$runner = ' + (Quote-PSString $governanceScript))
[void]$lines.Add('$historyScript = ' + (Quote-PSString $historyScript))
[void]$lines.Add('$logPath = ' + (Quote-PSString $logPath))
[void]$lines.Add('$historyPath = ' + (Quote-PSString $historyPath))
[void]$lines.Add('$historyMarkdownPath = ' + (Quote-PSString $historyMarkdownPath))
[void]$lines.Add('$ackPath = ' + (Quote-PSString $ackPath))
[void]$lines.Add('$logDir = Split-Path -Parent $logPath')
[void]$lines.Add('if (-not [string]::IsNullOrWhiteSpace($logDir)) { New-Item -ItemType Directory -Path $logDir -Force | Out-Null }')
[void]$lines.Add('& powershell -ExecutionPolicy Bypass -File $runner `')
Add-ArrayArg $lines "RouteLogPath" $RouteLogPath
Add-ScalarArg $lines "ReceiptPath" $ReceiptPath
Add-ArrayArg $lines "QueuePath" $QueuePath
Add-ScalarArg $lines "SourceInstanceId" $SourceInstanceId
Add-ScalarArg $lines "OutputDir" $OutputDir
Add-SwitchArg $lines "EmitRouteLog" $EmitRouteLog.IsPresent
Add-SwitchArg $lines "NoFailOnWarning" $NoFailOnWarning.IsPresent
Add-SwitchArg $lines "NoFailOnSensitive" $NoFailOnSensitive.IsPresent
Add-ScalarArg $lines "ReceiptRotationPath" $ReceiptRotationPath
Add-IntArg $lines "RotationKeepRecords" $RotationKeepRecords
Add-IntArg $lines "RotationMaxAgeDays" $RotationMaxAgeDays
Add-SwitchArg $lines "CompressRotationArchive" $CompressRotationArchive.IsPresent
Add-ScalarArg $lines "NotesPath" $NotesPath
Add-SwitchArg $lines "PackageAcceptance" $PackageAcceptance.IsPresent
Add-ScalarArg $lines "PackagePath" $PackagePath
Add-SwitchArg $lines "PackageDiagnostics" ($PackageDiagnostics.IsPresent -or -not [string]::IsNullOrWhiteSpace($DiagnosticsPackagePath))
Add-ScalarArg $lines "DiagnosticsPackagePath" $DiagnosticsPackagePath
Add-SwitchArg $lines "WriteReport" ($WriteReport.IsPresent -or -not [string]::IsNullOrWhiteSpace($ReportPath) -or -not [string]::IsNullOrWhiteSpace($HtmlReportPath))
Add-ScalarArg $lines "ReportPath" $ReportPath
Add-ScalarArg $lines "HtmlReportPath" $HtmlReportPath
Add-SwitchArg $lines "WriteDashboard" ($WriteDashboard.IsPresent -or -not [string]::IsNullOrWhiteSpace($DashboardPath) -or -not [string]::IsNullOrWhiteSpace($DashboardMarkdownPath))
Add-ScalarArg $lines "DashboardPath" $DashboardPath
Add-ScalarArg $lines "DashboardMarkdownPath" $DashboardMarkdownPath
Add-SwitchArg $lines "WriteS3StabilityRunbook" ($WriteS3StabilityRunbook.IsPresent -or -not [string]::IsNullOrWhiteSpace($S3StabilityRunbookPath) -or -not [string]::IsNullOrWhiteSpace($S3StabilityRunbookMarkdownPath))
Add-ScalarArg $lines "S3StabilityRunbookPath" $S3StabilityRunbookPath
Add-ScalarArg $lines "S3StabilityRunbookMarkdownPath" $S3StabilityRunbookMarkdownPath
Add-ScalarArg $lines "S3CoveragePolicyPath" $S3CoveragePolicyPath
Add-SwitchArg $lines "WarnS3CoverageGaps" $WarnS3CoverageGaps.IsPresent
Add-SwitchArg $lines "RunS3FailureBatchSample" $RunS3FailureBatchSample.IsPresent
Add-IntArg $lines "S3FailureBatchCountPerReason" $S3FailureBatchCountPerReason
Add-ScalarArg $lines "HealthCheckPath" (Join-Path $OutputDir "last-health.json")
Add-IntArg $lines "HealthMaxWarnings" 0
Add-SwitchArg $lines "NotifyOnUnhealthy" $true

$lastIndex = $lines.Count - 1
if ($lastIndex -ge 0) {
    $lastLine = ([string]$lines[$lastIndex]).TrimEnd()
    if ($lastLine.EndsWith([string][char]0x60)) {
        $lastLine = $lastLine.Substring(0, $lastLine.Length - 1).TrimEnd()
    }
    $lines[$lastIndex] = $lastLine
}
[void]$lines.Add('$exitCode = $LASTEXITCODE')
[void]$lines.Add(('"{0} exitCode=$exitCode historyPath={1} historyMarkdownPath={2} ackPath={3}" | Set-Content -LiteralPath $logPath -Encoding UTF8' -f (Get-Date).ToUniversalTime().ToString("o"), $historyPath, $historyMarkdownPath, $ackPath))
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
[void]$lines.Add('$finalExitCode = if ($exitCode -ne 0) { $exitCode } else { $historyExitCode }')
[void]$lines.Add('exit $finalExitCode')

$lines | Set-Content -LiteralPath $launcherPath -Encoding UTF8

$alertOverviewPath = Join-Path $OutputDir "governance-alert-overview.json"
$healthCheckOutputPath = Join-Path $OutputDir "last-health.json"
$diagnosticsPreviewPath = if ([string]::IsNullOrWhiteSpace($DiagnosticsPackagePath)) {
    Join-Path (Join-Path $OutputDir "diagnostics-package") "large-file-governance-diagnostics.zip"
} else {
    $DiagnosticsPackagePath
}
$reportPreviewPath = if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    Join-Path $OutputDir "large-file-governance-report.md"
} else {
    $ReportPath
}
$htmlReportPreviewPath = if ([string]::IsNullOrWhiteSpace($HtmlReportPath)) {
    ""
} else {
    $HtmlReportPath
}
$dashboardPreviewPath = if ([string]::IsNullOrWhiteSpace($DashboardPath)) {
    Join-Path $OutputDir "large-file-governance-dashboard.json"
} else {
    $DashboardPath
}
$dashboardMarkdownPreviewPath = if ([string]::IsNullOrWhiteSpace($DashboardMarkdownPath)) {
    ""
} else {
    $DashboardMarkdownPath
}
$s3FailureBatchPreviewPath = if ($RunS3FailureBatchSample) {
    Join-Path $OutputDir "s3-failure-batch-summary.json"
} else {
    ""
}
$s3StabilityRunbookPreviewPath = if ([string]::IsNullOrWhiteSpace($S3StabilityRunbookPath)) {
    Join-Path $OutputDir "s3-stability-runbook.json"
} else {
    $S3StabilityRunbookPath
}
$s3StabilityRunbookMarkdownPreviewPath = if ([string]::IsNullOrWhiteSpace($S3StabilityRunbookMarkdownPath)) {
    ""
} else {
    $S3StabilityRunbookMarkdownPath
}
$actionArgument = "-NoProfile -ExecutionPolicy Bypass -File `"$launcherPath`""
$preview = [pscustomobject]@{
    taskKind = "large-file-governance"
    taskName = $TaskName
    register = $Register.IsPresent
    schedule = $Schedule
    at = $At
    everyHours = if ($Schedule -eq "Hourly") { $EveryHours } else { $null }
    user = $User
    action = "powershell.exe $actionArgument"
    launcherPath = $launcherPath
    governanceScript = $governanceScript
    historyScript = $historyScript
    outputDir = $OutputDir
    alertOverviewPath = $alertOverviewPath
    healthCheckPath = $healthCheckOutputPath
    diagnosticsPackagePath = $diagnosticsPreviewPath
    reportPath = $reportPreviewPath
    htmlReportPath = $htmlReportPreviewPath
    dashboardPath = $dashboardPreviewPath
    statusArtifactPath = $dashboardPreviewPath
    dashboardMarkdownPath = $dashboardMarkdownPreviewPath
    s3StabilityRunbookPath = $s3StabilityRunbookPreviewPath
    s3StabilityRunbookMarkdownPath = $s3StabilityRunbookMarkdownPreviewPath
    s3CoveragePolicyPath = $S3CoveragePolicyPath
    warnS3CoverageGaps = $WarnS3CoverageGaps.IsPresent
    s3FailureBatchSummaryPath = $s3FailureBatchPreviewPath
    s3FailureBatchCountPerReason = if ($RunS3FailureBatchSample) { $S3FailureBatchCountPerReason } else { $null }
    historyPath = $historyPath
    historyArtifactPath = $historyPath
    historyMarkdownPath = $historyMarkdownPath
    ackPath = $ackPath
    ackArtifactPath = $ackPath
    lastRunPath = $logPath
    artifactRoles = [ordered]@{
        status = "statusArtifactPath"
        lastRun = "lastRunPath"
        history = "historyArtifactPath"
        ack = "ackArtifactPath"
    }
    readOnly = $true
    notes = "Default mode only writes this preview and launcher script. Use -Register to create or update the Windows Scheduled Task. After each run, read alertOverviewPath for aggregated health status, healthCheckPath for a single ok/notOk verdict, dashboardPath for machine-readable local status, reportPath for an operator-readable summary, diagnosticsPackagePath for a sanitized zip, and automation task history JSON/Markdown derived from last-run.log plus same-directory ack state."
}
$preview | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $previewPath -Encoding UTF8

if ($Register) {
    $action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $actionArgument
    if ($Schedule -eq "Daily") {
        $trigger = New-ScheduledTaskTrigger -Daily -At $At
    } else {
        $trigger = New-ScheduledTaskTrigger -Once -At $At -RepetitionInterval (New-TimeSpan -Hours $EveryHours)
    }
    $description = "QtNetworkChat large-file governance runner. Read-only except optional delivered receipt rotation."
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Description $description -User $User -Force | Out-Null
    Write-Host ("registered scheduled task: {0}" -f $TaskName)
} else {
    Write-Host "preview only; scheduled task was not registered"
}

Write-Host ("launcher: {0}" -f $launcherPath)
Write-Host ("preview: {0}" -f $previewPath)
