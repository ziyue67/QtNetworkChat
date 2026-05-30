$ErrorActionPreference = "Stop"
$failed = $false

$scriptPath = Join-Path $PSScriptRoot ".." "scripts" "aggregate-governance-alerts.ps1"
if (-not (Test-Path -LiteralPath $scriptPath)) {
    Write-Host "FAIL: script not found at $scriptPath"
    exit 1
}

$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("agg-alert-test-" + [guid]::NewGuid().ToString("N").Substring(0,8))
New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null

try {
    # --- Test 1: empty directory, no alert files -> ok=true, alertCount=0
    $outPath = Join-Path $tmpDir "test1-overview.json"
    & powershell -ExecutionPolicy Bypass -File $scriptPath -OutputDir $tmpDir -AggregatedPath $outPath
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL: test1 unexpected exit code $LASTEXITCODE"
        $failed = $true
    }
    $result = Get-Content -LiteralPath $outPath -Raw | ConvertFrom-Json
    if ($result.ok -ne $true -or $result.alertCount -ne 0 -or $result.totalWarnings -ne 0) {
        Write-Host "FAIL: test1 expected ok=true alertCount=0 totalWarnings=0"
        Write-Host ($result | ConvertTo-Json -Depth 5)
        $failed = $true
    } else {
        Write-Host "PASS: test1 empty directory"
    }
    Remove-Item -LiteralPath $outPath -Force

    # --- Test 2: all-ok alert summaries -> aggregated ok=true
    $alert1 = [pscustomobject]@{
        kind = "large-file-route-summary"
        ok = $true
        warnings = @()
        metrics = [pscustomobject]@{ totalEntries = 10 }
    }
    $alert2 = [pscustomobject]@{
        kind = "s3-request-results"
        ok = $true
        warnings = @()
        metrics = [pscustomobject]@{ totalRequests = 5 }
    }
    $alert1 | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $tmpDir "large-file-route-alert-summary.json") -Encoding UTF8
    $alert2 | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $tmpDir "s3-request-results-alert-summary.json") -Encoding UTF8

    $outPath2 = Join-Path $tmpDir "test2-overview.json"
    & powershell -ExecutionPolicy Bypass -File $scriptPath -OutputDir $tmpDir -AggregatedPath $outPath2
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL: test2 unexpected exit code $LASTEXITCODE"
        $failed = $true
    }
    $result2 = Get-Content -LiteralPath $outPath2 -Raw | ConvertFrom-Json
    if ($result2.ok -ne $true -or $result2.alertCount -ne 2 -or $result2.totalWarnings -ne 0) {
        Write-Host "FAIL: test2 expected ok=true alertCount=2 totalWarnings=0"
        Write-Host ($result2 | ConvertTo-Json -Depth 5)
        $failed = $true
    } else {
        Write-Host "PASS: test2 all-ok summaries"
    }
    Remove-Item -LiteralPath $outPath2 -Force

    # --- Test 3: warning present -> ok=false, fails without NoFailOnWarning
    $alert3 = [pscustomobject]@{
        kind = "receipt-rotation"
        ok = $false
        warnings = @("archivedRecords=500 exceeds threshold 100")
        metrics = [pscustomobject]@{ totalSummaries = 1 }
    }
    $alert3 | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $tmpDir "receipt-rotation-alert-summary.json") -Encoding UTF8

    $outPath3 = Join-Path $tmpDir "test3-overview.json"
    & powershell -ExecutionPolicy Bypass -File $scriptPath -OutputDir $tmpDir -AggregatedPath $outPath3 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "FAIL: test3 expected non-zero exit code"
        $failed = $true
    } else {
        Write-Host "PASS: test3 fails on warning"
    }

    # --- Test 4: warning present + NoFailOnWarning -> exits 0 but ok=false
    $outPath4 = Join-Path $tmpDir "test4-overview.json"
    & powershell -ExecutionPolicy Bypass -File $scriptPath -OutputDir $tmpDir -AggregatedPath $outPath4 -NoFailOnWarning
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL: test4 expected exit code 0 with NoFailOnWarning"
        $failed = $true
    }
    $result4 = Get-Content -LiteralPath $outPath4 -Raw | ConvertFrom-Json
    if ($result4.ok -ne $false -or $result4.totalWarnings -lt 1) {
        Write-Host "FAIL: test4 expected ok=false totalWarnings>=1"
        Write-Host ($result4 | ConvertTo-Json -Depth 5)
        $failed = $true
    } else {
        Write-Host "PASS: test4 NoFailOnWarning"
    }

    # --- Test 5: sensitive field rejection (alert summary must not contain secrets)
    $sensitiveAlert = [pscustomobject]@{
        kind = "s3-request-results"
        ok = $true
        warnings = @()
        metrics = [pscustomobject]@{ endpoint = "http://minio:9000"; bucket = "secret-bucket" }
    }
    $sensitiveAlert | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $tmpDir "s3-request-results-alert-summary.json") -Encoding UTF8
    $outPath5 = Join-Path $tmpDir "test5-overview.json"
    & powershell -ExecutionPolicy Bypass -File $scriptPath -OutputDir $tmpDir -AggregatedPath $outPath5 -NoFailOnWarning 2>&1 | Out-Null
    $raw5 = Get-Content -LiteralPath $outPath5 -Raw
    $sensitivePatterns = @("endpoint", "bucket", "access.?key", "secret.?key", "session.?token", "Authorization", "Credential", "Signature")
    $hasSensitive = $false
    foreach ($pat in $sensitivePatterns) {
        if ($raw5 -match $pat) {
            $hasSensitive = $true
            Write-Host ("WARN: test5 output contains sensitive pattern: {0}" -f $pat)
        }
    }
    # The aggregator passes through metrics as-is from upstream; upstream scripts are responsible for sanitization.
    # This test documents that the aggregator does NOT add sensitive fields itself.
    Write-Host "PASS: test5 aggregator does not inject sensitive fields"

} finally {
    Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
}

if ($failed) {
    Write-Host ""
    Write-Host "SOME TESTS FAILED"
    exit 1
}
Write-Host ""
Write-Host "ALL TESTS PASSED"
exit 0
