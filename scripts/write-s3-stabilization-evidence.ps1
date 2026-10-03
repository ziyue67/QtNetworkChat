param(
    [Parameter(Mandatory = $true)]
    [string[]]$S3SummaryPath,

    [string[]]$RouteSummaryPath = @(),

    [string]$OutputPath,

    [string]$MarkdownPath,

    [string]$AlertSummaryPath
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qt-governance-common.ps1")

function Read-JsonFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Input JSON does not exist: $Path"
    }
    $content = Get-Content -LiteralPath $Path -Raw
    Assert-QtGovernanceNoSensitiveText -Text $content -Message "Sensitive fields were found in evidence input."
    return $content | ConvertFrom-Json
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path (Get-Location) "s3-stabilization-evidence.json"
}

$reasonCounts = [ordered]@{}
$operationCounts = [ordered]@{}
$sourceFiles = @()
foreach ($path in $S3SummaryPath) {
    $summary = Read-JsonFile $path
    $sourceFiles += (Split-Path -Leaf $path)
    if ($summary.reasonCounts) {
        foreach ($prop in $summary.reasonCounts.PSObject.Properties) {
            if (-not $reasonCounts.Contains($prop.Name)) { $reasonCounts[$prop.Name] = 0 }
            $reasonCounts[$prop.Name] += [int]$prop.Value
        }
    }
    if ($summary.operationCounts) {
        foreach ($prop in $summary.operationCounts.PSObject.Properties) {
            if (-not $operationCounts.Contains($prop.Name)) { $operationCounts[$prop.Name] = 0 }
            $operationCounts[$prop.Name] += [int]$prop.Value
        }
    }
}

$routeFiles = @()
$fallbackSignals = 0
foreach ($path in $RouteSummaryPath) {
    $summary = Read-JsonFile $path
    $routeFiles += (Split-Path -Leaf $path)
    if ($null -ne $summary.failedFallbackRetained) {
        $fallbackSignals += [int]$summary.failedFallbackRetained
    }
}

$requiredReasons = @("timeout", "network", "tls", "auth", "retryable", "server", "hash", "size")
$observedReasons = @($reasonCounts.Keys)
$missingReasons = @($requiredReasons | Where-Object { $observedReasons -notcontains $_ })
$requiredOperations = @("PUT", "HEAD", "GET", "DELETE", "validate", "read", "remove")
$observedOperations = @($operationCounts.Keys)
$missingOperations = @($requiredOperations | Where-Object { $observedOperations -notcontains $_ })

$ok = ($missingReasons.Count -eq 0 -and $missingOperations.Count -eq 0)
$evidence = [ordered]@{
    format = "qtnetworkchat-s3-stabilization-evidence-v1"
    ok = $ok
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    sourceFiles = $sourceFiles
    routeFiles = $routeFiles
    reasonCounts = $reasonCounts
    operationCounts = $operationCounts
    requiredReasons = $requiredReasons
    missingReasons = $missingReasons
    requiredOperations = $requiredOperations
    missingOperations = $missingOperations
    fallbackSignals = $fallbackSignals
    notes = @(
        "Read-only evidence summary; does not connect to Redis/S3/MinIO.",
        "Inputs must be sanitized summaries, not raw endpoint/bucket/object URL/credential logs.",
        "Default CTest uses generated summaries and does not require a real backend."
    )
}

$json = ConvertTo-QtGovernanceJson -InputObject $evidence
Assert-QtGovernanceNoSensitiveText -Text $json -Message "Sensitive fields were generated in evidence output."
$outDir = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
Set-Content -LiteralPath $OutputPath -Value $json -Encoding UTF8

if (-not [string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# S3 Stabilization Evidence")
    $lines.Add("")
    $statusText = if ($ok) { "complete" } else { "missing-coverage" }
    $lines.Add(("- Status: {0}" -f $statusText))
    $lines.Add(("- Missing reasons: {0}" -f (($missingReasons -join ", "))))
    $lines.Add(("- Missing operations: {0}" -f (($missingOperations -join ", "))))
    $lines.Add(("- Fallback signals: {0}" -f $fallbackSignals))
    $lines.Add("")
    $lines.Add("This evidence is read-only and sanitized. It does not connect to Redis/S3/MinIO.")
    $markdown = ($lines -join [Environment]::NewLine)
    Assert-QtGovernanceNoSensitiveText -Text $markdown -Message "Sensitive fields were generated in evidence markdown."
    $mdDir = Split-Path -Parent $MarkdownPath
    if (-not [string]::IsNullOrWhiteSpace($mdDir)) { New-Item -ItemType Directory -Force -Path $mdDir | Out-Null }
    Set-Content -LiteralPath $MarkdownPath -Value $markdown -Encoding UTF8
}

if (-not [string]::IsNullOrWhiteSpace($AlertSummaryPath)) {
    $alert = [ordered]@{
        kind = "s3-stabilization-evidence"
        ok = $ok
        warnings = @($missingReasons + $missingOperations | ForEach-Object { "missing:$($_)" })
        metrics = [ordered]@{
            observedReasonCount = $observedReasons.Count
            missingReasonCount = $missingReasons.Count
            observedOperationCount = $observedOperations.Count
            missingOperationCount = $missingOperations.Count
            fallbackSignals = $fallbackSignals
        }
    }
    $alertJson = ConvertTo-QtGovernanceJson -InputObject $alert
    Assert-QtGovernanceNoSensitiveText -Text $alertJson -Message "Sensitive fields were generated in evidence alert."
    $alertDir = Split-Path -Parent $AlertSummaryPath
    if (-not [string]::IsNullOrWhiteSpace($alertDir)) { New-Item -ItemType Directory -Force -Path $alertDir | Out-Null }
    Set-Content -LiteralPath $AlertSummaryPath -Value $alertJson -Encoding UTF8
}

Write-Host "S3 stabilization evidence written"
Write-Host ("  output: {0}" -f $OutputPath)
if (-not [string]::IsNullOrWhiteSpace($MarkdownPath)) { Write-Host ("  markdown: {0}" -f $MarkdownPath) }
if (-not [string]::IsNullOrWhiteSpace($AlertSummaryPath)) { Write-Host ("  alert: {0}" -f $AlertSummaryPath) }
Write-Host "This run is read-only and sanitized."
