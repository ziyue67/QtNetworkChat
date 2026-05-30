param(
    [string[]]$RouteLogPath,

    [string]$ReceiptPath,

    [Parameter(Mandatory = $true)]
    [string[]]$QueuePath,

    [Parameter(Mandatory = $true)]
    [string]$SourceInstanceId,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [switch]$EmitRouteLog,

    [switch]$NoFailOnWarning,

    [switch]$NoFailOnSensitive,

    [string]$ReceiptRotationPath,

    [int]$RotationKeepRecords,

    [int]$RotationMaxAgeDays,

    [switch]$CompressRotationArchive,

    [string]$NotesPath,

    [switch]$PackageAcceptance,

    [string]$PackagePath
)

$ErrorActionPreference = "Stop"

function Invoke-CheckedScript([string]$ScriptPath, [string[]]$Arguments, [string]$OutputPath) {
    $parent = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    $output = & powershell -ExecutionPolicy Bypass -File $ScriptPath @Arguments 2>&1
    $output | Set-Content -LiteralPath $OutputPath -Encoding UTF8
    $output | Write-Host
    if ($LASTEXITCODE -ne 0) {
        throw ("Script failed with exit code {0}: {1}" -f $LASTEXITCODE, $ScriptPath)
    }
}

function Assert-PathExists([string]$PathValue, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue)) {
        throw ("{0} not found: {1}" -f $Label, $PathValue)
    }
}

if (($null -eq $RouteLogPath -or $RouteLogPath.Count -eq 0) -and [string]::IsNullOrWhiteSpace($ReceiptPath)) {
    throw "Either RouteLogPath or ReceiptPath is required."
}
if (($null -ne $RouteLogPath -and $RouteLogPath.Count -gt 0) -and -not [string]::IsNullOrWhiteSpace($ReceiptPath)) {
    throw "Use either RouteLogPath or ReceiptPath, not both."
}
foreach ($path in $QueuePath) {
    Assert-PathExists $path "QueuePath"
}
if ($null -ne $RouteLogPath) {
    foreach ($path in $RouteLogPath) {
        Assert-PathExists $path "RouteLogPath"
    }
}
if (-not [string]::IsNullOrWhiteSpace($ReceiptPath)) {
    Assert-PathExists $ReceiptPath "ReceiptPath"
}
if (-not [string]::IsNullOrWhiteSpace($ReceiptRotationPath)) {
    Assert-PathExists $ReceiptRotationPath "ReceiptRotationPath"
}
if (-not [string]::IsNullOrWhiteSpace($NotesPath)) {
    Assert-PathExists $NotesPath "NotesPath"
}

$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

$routeSummaryPath = Join-Path $resolvedOutputDir "large-file-route-summary.json"
$s3SummaryPath = Join-Path $resolvedOutputDir "s3-request-results-summary.json"
$reconcileOutputDir = Join-Path $resolvedOutputDir "reconcile"
$rotationSummaryPath = Join-Path $resolvedOutputDir "receipt-rotation-summary.json"
$packageOutputDir = Join-Path $resolvedOutputDir "acceptance-package"
$shouldPackageAcceptance = $PackageAcceptance -or -not [string]::IsNullOrWhiteSpace($PackagePath)
if ([string]::IsNullOrWhiteSpace($PackagePath)) {
    $PackagePath = Join-Path $packageOutputDir "large-file-acceptance.zip"
}

$routeLogAnalyzer = Join-Path $PSScriptRoot "analyze-large-file-route-logs.ps1"
$routeSummaryAnalyzer = Join-Path $PSScriptRoot "analyze-large-file-route-summary.ps1"
$s3Analyzer = Join-Path $PSScriptRoot "analyze-s3-request-results.ps1"
$reconcileRunner = Join-Path $PSScriptRoot "run-large-file-delivery-reconcile.ps1"
$receiptRotator = Join-Path $PSScriptRoot "rotate-large-file-receipts.ps1"
$rotationAnalyzer = Join-Path $PSScriptRoot "analyze-large-file-receipt-rotation.ps1"
$packager = Join-Path $PSScriptRoot "package-large-file-acceptance.ps1"

$hasRouteLogs = $null -ne $RouteLogPath -and $RouteLogPath.Count -gt 0
$totalSteps = 1
if ($hasRouteLogs) { $totalSteps += 3 }
if (-not [string]::IsNullOrWhiteSpace($ReceiptRotationPath)) { $totalSteps += 2 }
if ($shouldPackageAcceptance) { $totalSteps++ }
$currentStep = 0

function StepLabel([string]$Label) {
    $script:currentStep++
    Write-Host ""
    Write-Host ("step {0}/{1} {2}" -f $script:currentStep, $totalSteps, $Label)
}

Write-Host "large file governance run"
Write-Host ("  output dir: {0}" -f $resolvedOutputDir)

if ($hasRouteLogs) {
    StepLabel "route log analysis"
    $routeArgs = @("-Path") + $RouteLogPath + @("-SummaryPath", $routeSummaryPath)
    if ($NoFailOnSensitive) {
        $routeArgs += "-NoFailOnSensitive"
    }
    Invoke-CheckedScript $routeLogAnalyzer $routeArgs (Join-Path $resolvedOutputDir "route-analysis.log")

    StepLabel "route summary alerts"
    $routeSummaryArgs = @("-SummaryPath", $routeSummaryPath)
    if ($NoFailOnWarning) {
        $routeSummaryArgs += "-NoFailOnWarning"
    }
    Invoke-CheckedScript $routeSummaryAnalyzer $routeSummaryArgs (Join-Path $resolvedOutputDir "route-summary-alerts.log")

    StepLabel "S3 request result analysis"
    $s3Args = @("-Path") + $RouteLogPath + @("-SummaryPath", $s3SummaryPath)
    if ($NoFailOnWarning) {
        $s3Args += "-NoFailOnWarning"
    }
    Invoke-CheckedScript $s3Analyzer $s3Args (Join-Path $resolvedOutputDir "s3-request-analysis.log")
}

StepLabel "delivered receipt reconciliation"
$reconcileArgs = @("-QueuePath") + $QueuePath + @("-SourceInstanceId", $SourceInstanceId, "-OutputDir", $reconcileOutputDir)
if ($hasRouteLogs) {
    $reconcileArgs = @("-RouteLogPath") + $RouteLogPath + $reconcileArgs + @("-RunS3Analysis", "-S3AnalysisSummaryPath", (Join-Path $reconcileOutputDir "s3-analysis-summary.json"))
} else {
    $reconcileArgs = @("-ReceiptPath", $ReceiptPath) + $reconcileArgs
}
if ($EmitRouteLog) {
    $reconcileArgs += "-EmitRouteLog"
}
if ($NoFailOnSensitive) {
    $reconcileArgs += "-NoFailOnSensitive"
}
Invoke-CheckedScript $reconcileRunner $reconcileArgs (Join-Path $resolvedOutputDir "reconcile-run.log")

if (-not [string]::IsNullOrWhiteSpace($ReceiptRotationPath)) {
    StepLabel "receipt rotation"
    $rotationArgs = @("-ReceiptPath", $ReceiptRotationPath, "-SummaryPath", $rotationSummaryPath)
    if ($RotationKeepRecords -gt 0) {
        $rotationArgs += @("-KeepRecords", $RotationKeepRecords)
    }
    if ($RotationMaxAgeDays -gt 0) {
        $rotationArgs += @("-MaxAgeDays", $RotationMaxAgeDays)
    }
    if ($CompressRotationArchive) {
        $rotationArgs += "-CompressArchive"
    }
    if ($NoFailOnSensitive) {
        $rotationArgs += "-NoFailOnSensitive"
    }
    Invoke-CheckedScript $receiptRotator $rotationArgs (Join-Path $resolvedOutputDir "receipt-rotation.log")

    StepLabel "receipt rotation alerts"
    $rotationAlertArgs = @("-SummaryPath", $rotationSummaryPath)
    if ($NoFailOnWarning) {
        $rotationAlertArgs += "-NoFailOnWarning"
    }
    Invoke-CheckedScript $rotationAnalyzer $rotationAlertArgs (Join-Path $resolvedOutputDir "receipt-rotation-alerts.log")
}

if ($shouldPackageAcceptance) {
    StepLabel "acceptance package"
    $packageArgs = @("-OutputDir", $packageOutputDir, "-PackagePath", $PackagePath)
    if ($hasRouteLogs) {
        $packageArgs += @("-RouteLogPath") + $RouteLogPath
        if (Test-Path -LiteralPath $routeSummaryPath) {
            $packageArgs += @("-RouteSummaryPath", $routeSummaryPath)
        }
        if (Test-Path -LiteralPath $s3SummaryPath) {
            $packageArgs += @("-S3SummaryPath", $s3SummaryPath)
        }
    }
    if (Test-Path -LiteralPath $reconcileOutputDir) {
        $packageArgs += @("-ReconcileDir", $reconcileOutputDir)
    }
    if (Test-Path -LiteralPath $rotationSummaryPath) {
        $packageArgs += @("-RotationSummaryPath", $rotationSummaryPath)
    }
    if (-not [string]::IsNullOrWhiteSpace($NotesPath)) {
        $packageArgs += @("-NotesPath", $NotesPath)
    }
    if ($NoFailOnSensitive) {
        $packageArgs += "-NoFailOnSensitive"
    }
    Invoke-CheckedScript $packager $packageArgs (Join-Path $resolvedOutputDir "acceptance-package.log")
}

Write-Host ""
Write-Host "outputs"
if ($hasRouteLogs) {
    Write-Host ("  route summary: {0}" -f $routeSummaryPath)
    Write-Host ("  S3 summary: {0}" -f $s3SummaryPath)
}
Write-Host ("  reconcile dir: {0}" -f $reconcileOutputDir)
if (Test-Path -LiteralPath $rotationSummaryPath) {
    Write-Host ("  rotation summary: {0}" -f $rotationSummaryPath)
}
if (Test-Path -LiteralPath $PackagePath) {
    Write-Host ("  package: {0}" -f $PackagePath)
}
Write-Host ""
Write-Host "This governance run is read-only except optional receipt rotation; it does not connect to Redis/S3/MinIO and does not modify queues, attachments, or objects."
