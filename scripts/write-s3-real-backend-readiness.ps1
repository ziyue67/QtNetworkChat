param(
    [string]$OutputPath = "build-qt6-mingw\s3-real-backend-readiness.json",
    [string]$MarkdownPath,
    [string]$SmokeSummaryPath,
    [string]$EvidencePath,
    [string]$S3SummaryPath,
    [int]$MinS3Lines = 1,
    [switch]$RequireConfigured,
    [switch]$FailOnSensitive
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qt-script-common.ps1")

$sensitivePatterns = @(
    "endpoint\s*[:=]",
    "bucket\s*[:=]",
    "objectUrl\s*[:=]",
    "object-url\s*[:=]",
    "https?://",
    "access[-_\s]?key\s*[:=]",
    "secret[-_\s]?key\s*[:=]",
    "session[-_\s]?token\s*[:=]",
    "Authorization\s*[:=]",
    "Credential\s*=",
    "Signature\s*="
)


function Read-OptionalJson([string]$PathValue) {
    $resolvedPath = Resolve-OptionalPath $PathValue
    if ([string]::IsNullOrWhiteSpace($resolvedPath) -or -not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
        return $null
    }
    $raw = Get-Content -LiteralPath $resolvedPath -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $null
    }
    $raw | ConvertFrom-Json -ErrorAction Stop
}

function Get-JsonValue([object]$ObjectValue, [string]$Name, [object]$DefaultValue = $null) {
    if ($null -eq $ObjectValue) {
        return $DefaultValue
    }
    if ($ObjectValue.PSObject.Properties.Name -contains $Name) {
        return $ObjectValue.$Name
    }
    $DefaultValue
}

function Normalize-Bool([object]$Value, [bool]$DefaultValue = $false) {
    if ($null -eq $Value) {
        return $DefaultValue
    }
    if ($Value -is [bool]) {
        return [bool]$Value
    }
    $text = ([string]$Value).Trim().ToLowerInvariant()
    if ($text -in @("true", "1", "yes", "on")) { return $true }
    if ($text -in @("false", "0", "no", "off")) { return $false }
    $DefaultValue
}

function Format-Value([object]$Value) {
    if ($null -eq $Value) {
        return "unknown"
    }
    if ($Value -is [bool]) {
        return $Value.ToString().ToLowerInvariant()
    }
    $text = ([string]$Value).Trim()
    if ([string]::IsNullOrWhiteSpace($text)) {
        return "unknown"
    }
    $text
}

function Add-SensitiveHits([string]$PathValue, [System.Collections.ArrayList]$Hits) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        return
    }
    $lineNumber = 0
    Get-Content -LiteralPath $PathValue -Encoding UTF8 | ForEach-Object {
        $lineNumber += 1
        $line = [string]$_
        foreach ($pattern in $sensitivePatterns) {
            if ($line -match $pattern -and $line -notmatch "(<redacted>|\\u003credacted\\u003e|&lt;redacted&gt;)") {
                [void]$Hits.Add(("{0}:{1}:{2}" -f (Split-Path -Leaf $PathValue), $lineNumber, $pattern))
            }
        }
    }
}

if ($MinS3Lines -lt 0) {
    throw "MinS3Lines must be 0 or greater."
}

$requiredEnv = [ordered]@{
    QTNETWORKCHAT_OBJECT_S3_ENDPOINT = $env:QTNETWORKCHAT_OBJECT_S3_ENDPOINT
    QTNETWORKCHAT_OBJECT_S3_BUCKET = $env:QTNETWORKCHAT_OBJECT_S3_BUCKET
    QTNETWORKCHAT_OBJECT_S3_REGION = $env:QTNETWORKCHAT_OBJECT_S3_REGION
    QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY = $env:QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY
    QTNETWORKCHAT_OBJECT_S3_SECRET_KEY = $env:QTNETWORKCHAT_OBJECT_S3_SECRET_KEY
}
$missingRequired = @()
foreach ($name in $requiredEnv.Keys) {
    if ([string]::IsNullOrWhiteSpace([string]$requiredEnv[$name])) {
        $missingRequired += $name
    }
}

$objectStoreS3 = ([string]$env:QTNETWORKCHAT_OBJECT_STORE).Trim().ToLowerInvariant() -eq "s3"
$explicitEnabled = Normalize-Bool $env:QTNETWORKCHAT_OBJECT_S3_ENABLE $false
$defaultCIRequested = Normalize-Bool $env:QTNETWORKCHAT_OBJECT_S3_DEFAULT_CI $false
$configured = $objectStoreS3 -and $explicitEnabled -and $missingRequired.Count -eq 0

$smoke = Read-OptionalJson $SmokeSummaryPath
$evidence = Read-OptionalJson $EvidencePath
$s3Summary = Read-OptionalJson $S3SummaryPath

$smokeConfigured = $null -ne $smoke
$smokeOk = $false
$smokeOperationsReady = $false
if ($smokeConfigured -and (Get-JsonValue $smoke "format" "") -eq "qtnetworkchat-minio-s3-smoke-summary-v1") {
    $ops = Get-JsonValue $smoke "operations" $null
    $smokeOk = [bool](Get-JsonValue $smoke "ok" $false)
    $smokeOperationsReady =
        [bool](Get-JsonValue $ops "put" $false) -and
        [bool](Get-JsonValue $ops "head" $false) -and
        [bool](Get-JsonValue $ops "get" $false) -and
        [bool](Get-JsonValue $ops "delete" $false)
}

$evidenceConfigured = $null -ne $evidence
$evidenceOk = $false
$evidenceS3LineCount = 0
$evidenceSuccessCount = 0
$evidenceFixedFailureReasonCount = 0
$evidenceSensitiveHits = 0
if ($evidenceConfigured -and (Get-JsonValue $evidence "format" "") -eq "qtnetworkchat-s3-real-backend-evidence-v1") {
    $metrics = Get-JsonValue $evidence "metrics" $null
    $evidenceOk = [bool](Get-JsonValue $evidence "ok" $false)
    $evidenceS3LineCount = [int](Get-JsonValue $metrics "s3LineCount" 0)
    $evidenceSuccessCount = [int](Get-JsonValue $metrics "successCount" 0)
    $evidenceFixedFailureReasonCount = [int](Get-JsonValue $metrics "fixedFailureReasonCount" 0)
    $evidenceSensitiveHits = [int](Get-JsonValue $metrics "sensitiveHits" 0)
}

$s3SummaryConfigured = $null -ne $s3Summary
$s3SummaryLineCount = if ($s3SummaryConfigured) { [int](Get-JsonValue $s3Summary "s3LineCount" 0) } else { 0 }
$s3SummarySensitiveHits = if ($s3SummaryConfigured) { [int](Get-JsonValue $s3Summary "sensitiveHits" 0) } else { 0 }

$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in @($SmokeSummaryPath, $EvidencePath, $S3SummaryPath)) {
    Add-SensitiveHits $path $sensitiveHits
}

$status = "not-configured"
$readiness = "not-configured"
$releaseGate = "await-s3-real-backend-config"
$operatorAction = "Provide explicit S3/MinIO configuration and run minio-s3-smoke.ps1 to generate redacted real-backend evidence."
$auditFocus = @("s3-real-backend-config")
$ok = $false

if ($sensitiveHits.Count -gt 0 -or $evidenceSensitiveHits -gt 0 -or $s3SummarySensitiveHits -gt 0) {
    $status = "blocked"
    $readiness = "blocked"
    $releaseGate = "blocked-s3-real-backend-sensitive-evidence"
    $operatorAction = "Remove endpoint, bucket, object URL, credentials, Authorization, Credential, or Signature fields from S3 real-backend evidence."
    $auditFocus = @("s3-real-backend-redaction")
} elseif ($evidenceOk -and $evidenceS3LineCount -ge $MinS3Lines -and $evidenceSuccessCount -gt 0 -and (-not $smokeConfigured -or ($smokeOk -and $smokeOperationsReady))) {
    $status = "verified"
    $readiness = "verified"
    $releaseGate = "can-review-s3-real-backend-evidence"
    $operatorAction = "Archive the redacted S3/MinIO real-backend evidence with large-file governance artifacts."
    $auditFocus = @("s3-real-backend-smoke", "s3-real-backend-redaction")
    $ok = $true
} elseif ($configured) {
    $status = "review"
    $readiness = "review"
    $releaseGate = "await-s3-real-backend-smoke"
    $operatorAction = "Run minio-s3-smoke.ps1 and verify-s3-real-backend-evidence.ps1 with sanitized outputs before release review."
    $auditFocus = @("s3-real-backend-smoke")
} elseif ($evidenceConfigured -or $smokeConfigured -or $s3SummaryConfigured) {
    $status = "review"
    $readiness = "review"
    $releaseGate = "review-s3-real-backend-evidence"
    $operatorAction = "Review incomplete S3/MinIO evidence and regenerate it with sanitized smoke, summary, and evidence artifacts."
    $auditFocus = @("s3-real-backend-evidence")
}

if ($RequireConfigured.IsPresent -and -not $configured -and -not $ok) {
    $status = "blocked"
    $readiness = "blocked"
    $releaseGate = "blocked-s3-real-backend-not-configured"
    $operatorAction = "Configure S3/MinIO environment variables and regenerate readiness evidence."
    $auditFocus = @("s3-real-backend-config")
}

$realBackendDefaultCI = $defaultCIRequested -and $configured -and $ok -and $smokeOk -and $smokeOperationsReady -and ($evidenceSensitiveHits -eq 0) -and ($s3SummarySensitiveHits -eq 0) -and ($sensitiveHits.Count -eq 0)
$defaultCTestMode = if ($realBackendDefaultCI) {
    "real-backend-gated"
} elseif ($defaultCIRequested) {
    "real-backend-requested-but-not-ready"
} else {
    "readiness-and-redaction-only"
}
$defaultCIReleaseGate = if ($realBackendDefaultCI) {
    "s3-real-backend-default-ci-ready"
} elseif ($defaultCIRequested) {
    "blocked-s3-real-backend-default-ci-not-ready"
} else {
    "s3-real-backend-default-ci-not-requested"
}

$result = [ordered]@{
    format = "qtnetworkchat-s3-real-backend-readiness-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    readOnly = $true
    ok = $ok
    status = $status
    configured = $configured
    explicitEnabled = $explicitEnabled
    objectStoreS3 = $objectStoreS3
    config = [ordered]@{
        requiredPresentCount = $requiredEnv.Count - $missingRequired.Count
        requiredMissing = @($missingRequired)
        endpointPresent = -not [string]::IsNullOrWhiteSpace($env:QTNETWORKCHAT_OBJECT_S3_ENDPOINT)
        bucketPresent = -not [string]::IsNullOrWhiteSpace($env:QTNETWORKCHAT_OBJECT_S3_BUCKET)
        regionPresent = -not [string]::IsNullOrWhiteSpace($env:QTNETWORKCHAT_OBJECT_S3_REGION)
        accessKeyPresent = -not [string]::IsNullOrWhiteSpace($env:QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY)
        secretKeyPresent = -not [string]::IsNullOrWhiteSpace($env:QTNETWORKCHAT_OBJECT_S3_SECRET_KEY)
        prefixPresent = -not [string]::IsNullOrWhiteSpace($env:QTNETWORKCHAT_OBJECT_S3_PREFIX)
        defaultCIRequested = $defaultCIRequested
    }
    evidence = [ordered]@{
        smokeSummaryConfigured = $smokeConfigured
        smokeOk = $smokeOk
        smokeOperationsReady = $smokeOperationsReady
        realBackendEvidenceConfigured = $evidenceConfigured
        realBackendEvidenceOk = $evidenceOk
        s3SummaryConfigured = $s3SummaryConfigured
        s3LineCount = $evidenceS3LineCount
        s3SummaryLineCount = $s3SummaryLineCount
        successCount = $evidenceSuccessCount
        fixedFailureReasonCount = $evidenceFixedFailureReasonCount
        sensitiveHitCount = [int]$sensitiveHits.Count + $evidenceSensitiveHits + $s3SummarySensitiveHits
    }
    summary = [ordered]@{
        readiness = $readiness
        operatorAction = $operatorAction
    }
    auditSummary = [ordered]@{
        releaseGate = $releaseGate
        auditFocus = @($auditFocus)
        defaultCTestMode = $defaultCTestMode
        realBackendDefaultCI = $realBackendDefaultCI
        defaultCIReleaseGate = $defaultCIReleaseGate
    }
    inputs = [ordered]@{
        smokeSummaryPath = if ([string]::IsNullOrWhiteSpace($SmokeSummaryPath)) { "" } else { Split-Path -Leaf $SmokeSummaryPath }
        evidencePath = if ([string]::IsNullOrWhiteSpace($EvidencePath)) { "" } else { Split-Path -Leaf $EvidencePath }
        s3SummaryPath = if ([string]::IsNullOrWhiteSpace($S3SummaryPath)) { "" } else { Split-Path -Leaf $S3SummaryPath }
    }
    sensitiveHits = @($sensitiveHits.ToArray())
}

$resolvedOutputPath = Resolve-OptionalPath $OutputPath
$outputParent = Split-Path -Parent $resolvedOutputPath
if (-not [string]::IsNullOrWhiteSpace($outputParent)) {
    New-Item -ItemType Directory -Path $outputParent -Force | Out-Null
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedOutputPath -Encoding UTF8

if (-not [string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $resolvedMarkdownPath = Resolve-OptionalPath $MarkdownPath
    $markdownParent = Split-Path -Parent $resolvedMarkdownPath
    if (-not [string]::IsNullOrWhiteSpace($markdownParent)) {
        New-Item -ItemType Directory -Path $markdownParent -Force | Out-Null
    }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# QtNetworkChat S3 Real Backend Readiness")
    $lines.Add("")
    $lines.Add(('- Status: `{0}`' -f $status))
    $lines.Add(('- OK: `{0}`' -f (Format-Value $ok)))
    $lines.Add(('- Configured: `{0}`' -f (Format-Value $configured)))
    $lines.Add(('- Explicit enabled: `{0}`' -f (Format-Value $explicitEnabled)))
    $lines.Add(('- Default CI requested: `{0}`' -f (Format-Value $defaultCIRequested)))
    $lines.Add(('- Real backend default CI: `{0}`' -f (Format-Value $realBackendDefaultCI)))
    $lines.Add(('- Default CI gate: `{0}`' -f $defaultCIReleaseGate))
    $lines.Add(('- Evidence configured: `{0}`' -f (Format-Value $evidenceConfigured)))
    $lines.Add(('- Smoke configured: `{0}`' -f (Format-Value $smokeConfigured)))
    $lines.Add(('- S3 lines: `{0}`' -f $evidenceS3LineCount))
    $lines.Add(('- Success count: `{0}`' -f $evidenceSuccessCount))
    $lines.Add(('- Fixed failure reasons: `{0}`' -f $evidenceFixedFailureReasonCount))
    $lines.Add(('- Release gate: `{0}`' -f $releaseGate))
    $lines.Add(('- Operator action: `{0}`' -f $operatorAction))
    $lines.Add("")
    $lines.Add("This readiness file is sanitized. It records whether S3/MinIO configuration and redacted evidence exist, but never records endpoint, bucket, object URLs, credentials, Authorization, Credential, or Signature values.")
    $lines | Set-Content -LiteralPath $resolvedMarkdownPath -Encoding UTF8
}

Write-Host ("s3 real backend readiness: {0}" -f $resolvedOutputPath)
$result | ConvertTo-Json -Depth 8

if ($FailOnSensitive.IsPresent -and ($sensitiveHits.Count -gt 0 -or $result.evidence.sensitiveHitCount -gt 0)) {
    exit 2
}
if ($RequireConfigured.IsPresent -and -not $configured -and -not $ok) {
    exit 3
}
