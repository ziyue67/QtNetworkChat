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

if ([string]::IsNullOrWhiteSpace($TaskDir)) {
    $TaskDir = Join-Path $OutputDir "scheduled-task"
}

$resolvedTaskDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($TaskDir)
New-Item -ItemType Directory -Path $resolvedTaskDir -Force | Out-Null

$governanceScript = Join-Path $PSScriptRoot "run-large-file-governance.ps1"
if (-not (Test-Path -LiteralPath $governanceScript -PathType Leaf)) {
    throw "Governance runner not found: $governanceScript"
}

$launcherPath = Join-Path $resolvedTaskDir "run-large-file-governance-task.ps1"
$previewPath = Join-Path $resolvedTaskDir "scheduled-task-preview.json"
$logPath = Join-Path $resolvedTaskDir "last-run.log"

$lines = New-Object System.Collections.ArrayList
[void]$lines.Add('$ErrorActionPreference = "Stop"')
[void]$lines.Add('$runner = ' + (Quote-PSString $governanceScript))
[void]$lines.Add('$logPath = ' + (Quote-PSString $logPath))
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

$lastIndex = $lines.Count - 1
if ($lastIndex -ge 0) {
    $lastLine = ([string]$lines[$lastIndex]).TrimEnd()
    if ($lastLine.EndsWith([string][char]0x60)) {
        $lastLine = $lastLine.Substring(0, $lastLine.Length - 1).TrimEnd()
    }
    $lines[$lastIndex] = $lastLine
}
[void]$lines.Add('$exitCode = $LASTEXITCODE')
[void]$lines.Add(('"{0} exitCode=$exitCode" | Set-Content -LiteralPath $logPath -Encoding UTF8' -f (Get-Date).ToUniversalTime().ToString("o")))
[void]$lines.Add('exit $exitCode')

$lines | Set-Content -LiteralPath $launcherPath -Encoding UTF8

$actionArgument = "-NoProfile -ExecutionPolicy Bypass -File `"$launcherPath`""
$preview = [pscustomobject]@{
    taskName = $TaskName
    register = $Register.IsPresent
    schedule = $Schedule
    at = $At
    everyHours = if ($Schedule -eq "Hourly") { $EveryHours } else { $null }
    user = $User
    action = "powershell.exe $actionArgument"
    launcherPath = $launcherPath
    governanceScript = $governanceScript
    outputDir = $OutputDir
    readOnly = $true
    notes = "Default mode only writes this preview and launcher script. Use -Register to create or update the Windows Scheduled Task."
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
