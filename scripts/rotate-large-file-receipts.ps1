param(
    [Parameter(Mandatory = $true)]
    [string]$ReceiptPath,

    [int]$KeepRecords = 10000,

    [int]$MaxAgeDays = 30,

    [string]$ArchiveDir,

    [switch]$CompressArchive,

    [switch]$DryRun,

    [switch]$NoFailOnSensitive,

    [string]$SummaryPath
)

$ErrorActionPreference = "Stop"
$script:CliBoundParameters = @{} + $PSBoundParameters

$sensitivePatterns = @(
    "endpoint",
    "bucket",
    "objectUrl",
    "object URL",
    "accessKey",
    "secretKey",
    "sessionToken",
    "Authorization",
    "Credential",
    "Signature",
    "X-Amz-Credential",
    "X-Amz-Signature"
)

function Find-SensitiveHits([string[]]$Path) {
    $hits = @()
    foreach ($item in $Path) {
        if ([string]::IsNullOrWhiteSpace($item) -or -not (Test-Path -LiteralPath $item)) {
            continue
        }
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $item) {
            $lineNumber++
            foreach ($pattern in $sensitivePatterns) {
                if ($line -match [regex]::Escape($pattern)) {
                    $hits += [pscustomobject]@{
                        path = $item
                        line = $lineNumber
                        pattern = $pattern
                    }
                }
            }
        }
    }
    $hits
}

function Read-ReceiptRecords([string]$Path) {
    $records = @()
    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $Path) {
        $lineNumber++
        $trimmed = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed)) {
            continue
        }
        try {
            $obj = $trimmed | ConvertFrom-Json
        } catch {
            throw ("Invalid receipt JSON at line {0}: {1}" -f $lineNumber, $_.Exception.Message)
        }
        $createdAt = [datetimeoffset]::MinValue
        if (-not [string]::IsNullOrWhiteSpace([string]$obj.createdAt)) {
            [datetimeoffset]::TryParse([string]$obj.createdAt, [ref]$createdAt) | Out-Null
        }
        $records += [pscustomobject]@{
            json = $trimmed
            createdAt = $createdAt
            index = $lineNumber
        }
    }
    $records
}

function Get-ReceiptEnvName([string]$Name) {
    switch ($Name) {
        "KeepRecords" { return "QTNETWORKCHAT_DELIVERED_RECEIPT_KEEP_RECORDS" }
        "MaxAgeDays" { return "QTNETWORKCHAT_DELIVERED_RECEIPT_MAX_AGE_DAYS" }
        "ArchiveDir" { return "QTNETWORKCHAT_DELIVERED_RECEIPT_ARCHIVE_DIR" }
        "CompressArchive" { return "QTNETWORKCHAT_DELIVERED_RECEIPT_COMPRESS_ARCHIVE" }
        "SummaryPath" { return "QTNETWORKCHAT_DELIVERED_RECEIPT_SUMMARY_PATH" }
        default { throw ("Unsupported receipt rotation setting: {0}" -f $Name) }
    }
}

function Get-PositiveIntSetting([string]$Name, [int]$CurrentValue) {
    if ($script:CliBoundParameters.ContainsKey($Name)) {
        return $CurrentValue
    }
    $envName = Get-ReceiptEnvName $Name
    $value = [string]([Environment]::GetEnvironmentVariable($envName))
    if ([string]::IsNullOrWhiteSpace($value)) {
        return $CurrentValue
    }
    $parsed = 0
    if (-not [int]::TryParse($value.Trim(), [ref]$parsed) -or $parsed -lt 1) {
        throw ("{0} must be a positive integer." -f $envName)
    }
    $parsed
}

function Get-NonNegativeIntSetting([string]$Name, [int]$CurrentValue) {
    if ($script:CliBoundParameters.ContainsKey($Name)) {
        return $CurrentValue
    }
    $envName = Get-ReceiptEnvName $Name
    $value = [string]([Environment]::GetEnvironmentVariable($envName))
    if ([string]::IsNullOrWhiteSpace($value)) {
        return $CurrentValue
    }
    $parsed = 0
    if (-not [int]::TryParse($value.Trim(), [ref]$parsed) -or $parsed -lt 0) {
        throw ("{0} must be 0 or a positive integer." -f $envName)
    }
    $parsed
}

function Get-StringSetting([string]$Name, [string]$CurrentValue) {
    if ($script:CliBoundParameters.ContainsKey($Name) -and -not [string]::IsNullOrWhiteSpace($CurrentValue)) {
        return $CurrentValue
    }
    $envName = Get-ReceiptEnvName $Name
    $value = [string]([Environment]::GetEnvironmentVariable($envName))
    if ([string]::IsNullOrWhiteSpace($value)) {
        return $CurrentValue
    }
    $value.Trim()
}

function Get-BoolSetting([string]$Name, [bool]$CurrentValue) {
    if ($script:CliBoundParameters.ContainsKey($Name)) {
        return $CurrentValue
    }
    $envName = Get-ReceiptEnvName $Name
    $value = [string]([Environment]::GetEnvironmentVariable($envName))
    if ([string]::IsNullOrWhiteSpace($value)) {
        return $CurrentValue
    }
    switch ($value.Trim().ToLowerInvariant()) {
        { $_ -in @("1", "true", "yes", "on") } { return $true }
        { $_ -in @("0", "false", "no", "off") } { return $false }
        default { throw ("{0} must be one of 1/0, true/false, yes/no, on/off." -f $envName) }
    }
}

$KeepRecords = Get-PositiveIntSetting "KeepRecords" $KeepRecords
$MaxAgeDays = Get-NonNegativeIntSetting "MaxAgeDays" $MaxAgeDays
$ArchiveDir = Get-StringSetting "ArchiveDir" $ArchiveDir
$SummaryPath = Get-StringSetting "SummaryPath" $SummaryPath
$compressArchiveEnabled = Get-BoolSetting "CompressArchive" ([bool]$CompressArchive)

if ($KeepRecords -lt 1) {
    throw "KeepRecords must be greater than 0."
}
if ($MaxAgeDays -lt 0) {
    throw "MaxAgeDays must be 0 or greater."
}
if (-not (Test-Path -LiteralPath $ReceiptPath)) {
    throw "Receipt path not found: $ReceiptPath"
}

$resolvedReceiptPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReceiptPath)
$receiptDir = Split-Path -Parent $resolvedReceiptPath
if ([string]::IsNullOrWhiteSpace($ArchiveDir)) {
    $ArchiveDir = Join-Path $receiptDir "archive"
}
$resolvedArchiveDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ArchiveDir)

$sensitiveHits = @(Find-SensitiveHits @($resolvedReceiptPath))
if ($sensitiveHits.Count -gt 0 -and -not $NoFailOnSensitive) {
    $sensitiveHits | ConvertTo-Json -Depth 4
    throw "Sensitive fields were found in receipt input."
}

$records = @(Read-ReceiptRecords $resolvedReceiptPath)
$cutoff = [datetimeoffset]::UtcNow.AddDays(-1 * $MaxAgeDays)
$withCreatedAt = @($records | Where-Object { $_.createdAt -ne [datetimeoffset]::MinValue })
$expiredIndexes = @{}
if ($MaxAgeDays -gt 0) {
    foreach ($record in $withCreatedAt) {
        if ($record.createdAt -lt $cutoff) {
            $expiredIndexes[[int]$record.index] = $true
        }
    }
}

$retainedByAge = @($records | Where-Object { -not $expiredIndexes.ContainsKey([int]$_.index) })
$retainedByAgeNewestOrder = @($retainedByAge | Sort-Object -Property @{Expression = "createdAt"; Ascending = $true}, @{Expression = "index"; Ascending = $true})
$retainIndexSet = @{}
$start = [Math]::Max(0, $retainedByAgeNewestOrder.Count - $KeepRecords)
foreach ($record in @($retainedByAgeNewestOrder | Select-Object -Skip $start)) {
    $retainIndexSet[[int]$record.index] = $true
}

$retained = @($records | Where-Object { $retainIndexSet.ContainsKey([int]$_.index) })
$archived = @($records | Where-Object { -not $retainIndexSet.ContainsKey([int]$_.index) })

$archivePath = $null
$zipPath = $null
if ($archived.Count -gt 0) {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $archivePath = Join-Path $resolvedArchiveDir ("delivered-receipts-{0}.jsonl" -f $timestamp)
    $zipPath = "$archivePath.zip"
}

if (-not $DryRun) {
    if ($archived.Count -gt 0) {
        New-Item -ItemType Directory -Path $resolvedArchiveDir -Force | Out-Null
        $archived.json | Set-Content -LiteralPath $archivePath -Encoding UTF8
        if ($compressArchiveEnabled) {
            Compress-Archive -LiteralPath $archivePath -DestinationPath $zipPath -Force
            Remove-Item -LiteralPath $archivePath -Force
            $archivePath = $zipPath
        }
    }

    $tmpPath = "$resolvedReceiptPath.tmp"
    $retained.json | Set-Content -LiteralPath $tmpPath -Encoding UTF8
    Move-Item -LiteralPath $tmpPath -Destination $resolvedReceiptPath -Force
}

$summary = [pscustomobject]@{
    receiptPath = $resolvedReceiptPath
    archivePath = $archivePath
    dryRun = [bool]$DryRun
    totalRecords = $records.Count
    retainedRecords = $retained.Count
    archivedRecords = $archived.Count
    keepRecords = $KeepRecords
    maxAgeDays = $MaxAgeDays
    compressArchive = $compressArchiveEnabled
    sensitiveHits = $sensitiveHits.Count
}
$summaryJson = $summary | ConvertTo-Json -Depth 4
if (-not [string]::IsNullOrWhiteSpace($SummaryPath)) {
    $resolvedSummaryPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($SummaryPath)
    $summaryParent = Split-Path -Parent $resolvedSummaryPath
    if (-not [string]::IsNullOrWhiteSpace($summaryParent)) {
        New-Item -ItemType Directory -Path $summaryParent -Force | Out-Null
    }
    $summaryJson | Set-Content -LiteralPath $resolvedSummaryPath -Encoding UTF8
}
$summaryJson
