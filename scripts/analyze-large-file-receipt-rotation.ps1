param(
    [Parameter(Mandatory = $true)]
    [string[]]$SummaryPath,

    [int]$WarnArchivedRecords = 0,

    [int]$WarnRetainedRecords = 0,

    [switch]$NoFailOnWarning
)

$ErrorActionPreference = "Stop"

function Get-Int64Field($Object, [string]$Name) {
    if ($null -eq $Object.PSObject.Properties[$Name]) {
        return 0L
    }
    $value = $Object.$Name
    if ($null -eq $value) {
        return 0L
    }
    $parsed = 0L
    if ([int64]::TryParse([string]$value, [ref]$parsed)) {
        return $parsed
    }
    0L
}

if ($WarnArchivedRecords -lt 0) {
    throw "WarnArchivedRecords must be 0 or greater."
}
if ($WarnRetainedRecords -lt 0) {
    throw "WarnRetainedRecords must be 0 or greater."
}

$summaries = @()
foreach ($path in $SummaryPath) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Summary path not found: $path"
    }
    try {
        $summary = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    } catch {
        throw ("Invalid rotation summary JSON: {0}: {1}" -f $path, $_.Exception.Message)
    }
    $summaries += [pscustomobject]@{
        path = $path
        receiptPath = [string]$summary.receiptPath
        archivePath = [string]$summary.archivePath
        dryRun = [bool]$summary.dryRun
        totalRecords = Get-Int64Field $summary "totalRecords"
        retainedRecords = Get-Int64Field $summary "retainedRecords"
        archivedRecords = Get-Int64Field $summary "archivedRecords"
        keepRecords = Get-Int64Field $summary "keepRecords"
        maxAgeDays = Get-Int64Field $summary "maxAgeDays"
        compressArchive = [bool]$summary.compressArchive
        sensitiveHits = Get-Int64Field $summary "sensitiveHits"
    }
}

$totalRecords = 0L
$retainedRecords = 0L
$archivedRecords = 0L
$sensitiveHits = 0L
$dryRuns = 0
$warnings = New-Object System.Collections.Generic.List[string]

foreach ($summary in $summaries) {
    $totalRecords += $summary.totalRecords
    $retainedRecords += $summary.retainedRecords
    $archivedRecords += $summary.archivedRecords
    $sensitiveHits += $summary.sensitiveHits
    if ($summary.dryRun) {
        $dryRuns += 1
    }
    if ($summary.sensitiveHits -gt 0) {
        $warnings.Add(("{0}: sensitiveHits={1}" -f $summary.path, $summary.sensitiveHits))
    }
    if ($summary.archivedRecords -gt 0 -and [string]::IsNullOrWhiteSpace($summary.archivePath)) {
        $warnings.Add(("{0}: archivedRecords={1} but archivePath is empty" -f $summary.path, $summary.archivedRecords))
    }
    if ($WarnArchivedRecords -gt 0 -and $summary.archivedRecords -gt $WarnArchivedRecords) {
        $warnings.Add(("{0}: archivedRecords={1} exceeds threshold {2}" -f $summary.path, $summary.archivedRecords, $WarnArchivedRecords))
    }
    if ($WarnRetainedRecords -gt 0 -and $summary.retainedRecords -gt $WarnRetainedRecords) {
        $warnings.Add(("{0}: retainedRecords={1} exceeds threshold {2}" -f $summary.path, $summary.retainedRecords, $WarnRetainedRecords))
    }
}

Write-Host "large file receipt rotation summaries"
Write-Host ("  files: {0}" -f $summaries.Count)
Write-Host ("  total records: {0}" -f $totalRecords)
Write-Host ("  retained records: {0}" -f $retainedRecords)
Write-Host ("  archived records: {0}" -f $archivedRecords)
Write-Host ("  sensitive hits: {0}" -f $sensitiveHits)
Write-Host ("  dry runs: {0}" -f $dryRuns)

Write-Host ""
Write-Host "summary rows"
foreach ($summary in ($summaries | Sort-Object path)) {
    Write-Host ("  {0} retained={1} archived={2} sensitiveHits={3} archivePath={4}" -f
        $summary.path,
        $summary.retainedRecords,
        $summary.archivedRecords,
        $summary.sensitiveHits,
        $(if ([string]::IsNullOrWhiteSpace($summary.archivePath)) { "<none>" } else { $summary.archivePath }))
}

if ($warnings.Count -gt 0) {
    Write-Host ""
    Write-Host "warnings"
    foreach ($warning in $warnings) {
        Write-Host ("  {0}" -f $warning)
    }
    if (-not $NoFailOnWarning) {
        throw "Receipt rotation summary warnings were found."
    }
}
