param(
    [Parameter(Mandatory = $true)]
    [string]$ReceiptPath,

    [Parameter(Mandatory = $true)]
    [string]$FallbackPath,

    [switch]$EmitRouteLog,

    [switch]$NoFailOnSensitive
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

function Read-JsonRecords([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Input path not found: $Path"
    }

    $raw = Get-Content -LiteralPath $Path -Raw
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return @()
    }

    $trimmed = $raw.Trim()
    if ($trimmed.StartsWith("[") -or ($trimmed.StartsWith("{") -and -not ($trimmed -match "`r?`n\s*\{"))) {
        try {
            $parsed = $trimmed | ConvertFrom-Json
            if ($null -eq $parsed) {
                return @()
            }
            if ($parsed -is [array]) {
                return @($parsed)
            }
            return @($parsed)
        } catch {
            if ($trimmed.StartsWith("[")) {
                throw
            }
        }
    }

    $records = New-Object System.Collections.Generic.List[object]
    foreach ($line in ($raw -split "`r?`n")) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        $records.Add(($line | ConvertFrom-Json))
    }
    @($records)
}

function Test-SafeObjectKey([string]$Value) {
    $trimmed = ([string]$Value).Trim()
    if ($trimmed -ne [string]$Value) { return $false }
    if ([string]::IsNullOrWhiteSpace($trimmed)) { return $false }
    if ($trimmed.Contains("..") -or $trimmed.Contains("/") -or $trimmed.Contains("\") -or $trimmed.Contains(":")) {
        return $false
    }
    $trimmed -match "^[A-Za-z0-9_-]{8,64}(\.[A-Za-z0-9_-]{1,32})?$"
}

function Test-Sha256Hex([string]$Value) {
    ([string]$Value).Trim() -match "^[A-Fa-f0-9]{64}$"
}

function Get-StringField($Record, [string]$Name) {
    $value = $Record.$Name
    if ($null -eq $value) {
        return ""
    }
    ([string]$value).Trim()
}

function Get-Int64Field($Record, [string]$Name) {
    $value = $Record.$Name
    if ($null -eq $value) {
        return 0L
    }
    try {
        return [int64]$value
    } catch {
        return 0L
    }
}

function Get-RouteKey($Record) {
    "{0}|{1}|{2}" -f (Get-StringField $Record "transferId"),
        (Get-StringField $Record "objectKey"),
        (Get-StringField $Record "receiverId")
}

function Test-ValidReceipt($Receipt) {
    -not [string]::IsNullOrWhiteSpace((Get-StringField $Receipt "sourceInstanceId")) -and
        -not [string]::IsNullOrWhiteSpace((Get-StringField $Receipt "transferId")) -and
        -not [string]::IsNullOrWhiteSpace((Get-StringField $Receipt "receiverId")) -and
        (Test-SafeObjectKey (Get-StringField $Receipt "objectKey")) -and
        (Test-Sha256Hex (Get-StringField $Receipt "fileHash")) -and
        (Get-Int64Field $Receipt "confirmedBytes") -gt 0
}

function Test-ValidFallback($Fallback) {
    -not [string]::IsNullOrWhiteSpace((Get-StringField $Fallback "sourceInstanceId")) -and
        -not [string]::IsNullOrWhiteSpace((Get-StringField $Fallback "transferId")) -and
        -not [string]::IsNullOrWhiteSpace((Get-StringField $Fallback "receiverId")) -and
        (Test-SafeObjectKey (Get-StringField $Fallback "objectKey")) -and
        (Test-Sha256Hex (Get-StringField $Fallback "fileHash")) -and
        (Get-Int64Field $Fallback "fileSize") -gt 0
}

function Compare-ReceiptToFallback($Receipt, $Fallback) {
    if (-not (Test-ValidReceipt $Receipt)) {
        return [ordered]@{ Result = "retained"; Reason = "invalid-receipt" }
    }
    if ($null -eq $Fallback) {
        return [ordered]@{ Result = "retained"; Reason = "receipt-not-matched" }
    }
    if (-not (Test-ValidFallback $Fallback)) {
        return [ordered]@{ Result = "retained"; Reason = "invalid-payload" }
    }

    $sameMetadata = (Get-StringField $Receipt "sourceInstanceId") -eq (Get-StringField $Fallback "sourceInstanceId") -and
        (Get-StringField $Receipt "transferId") -eq (Get-StringField $Fallback "transferId") -and
        (Get-StringField $Receipt "receiverId") -eq (Get-StringField $Fallback "receiverId") -and
        (Get-StringField $Receipt "objectKey") -eq (Get-StringField $Fallback "objectKey") -and
        (Get-StringField $Receipt "fileHash").ToLowerInvariant() -eq (Get-StringField $Fallback "fileHash").ToLowerInvariant()
    if (-not $sameMetadata) {
        return [ordered]@{ Result = "retained"; Reason = "receipt-not-matched" }
    }

    if ((Get-Int64Field $Receipt "confirmedBytes") -lt (Get-Int64Field $Fallback "fileSize")) {
        return [ordered]@{ Result = "retained"; Reason = "confirmed-bytes-insufficient" }
    }

    [ordered]@{ Result = "cleaned"; Reason = "cleaned" }
}

function Add-Count([hashtable]$Table, [string]$Key) {
    if ([string]::IsNullOrWhiteSpace($Key)) {
        $Key = "<empty>"
    }
    if (-not $Table.ContainsKey($Key)) {
        $Table[$Key] = 0
    }
    $Table[$Key] += 1
}

function ConvertTo-RouteLogValue([string]$Value, [string]$Fallback) {
    $trimmed = ([string]$Value).Trim()
    if ([string]::IsNullOrWhiteSpace($trimmed)) {
        return $Fallback
    }
    $safe = [regex]::Replace($trimmed, "[^A-Za-z0-9_.:-]", "_")
    if ([string]::IsNullOrWhiteSpace($safe)) {
        return $Fallback
    }
    $safe
}

function Find-SensitiveHits([string[]]$Paths) {
    $hits = New-Object System.Collections.Generic.List[string]
    foreach ($path in $Paths) {
        $lineNumber = 0
        Get-Content -LiteralPath $path | ForEach-Object {
            $lineNumber += 1
            $line = [string]$_
            foreach ($pattern in $sensitivePatterns) {
                if ($line -match $pattern) {
                    $hits.Add(("{0}:{1}:{2}" -f $path, $lineNumber, $pattern))
                }
            }
        }
    }
    $hits
}

$receipts = Read-JsonRecords $ReceiptPath
$fallbacks = Read-JsonRecords $FallbackPath
$fallbackByKey = @{}
foreach ($fallback in $fallbacks) {
    $fallbackByKey[(Get-RouteKey $fallback)] = $fallback
}

$resultCounts = @{}
$reasonCounts = @{}
$rows = New-Object System.Collections.Generic.List[object]
foreach ($receipt in $receipts) {
    $key = Get-RouteKey $receipt
    $fallback = $null
    if ($fallbackByKey.ContainsKey($key)) {
        $fallback = $fallbackByKey[$key]
    }
    $decision = Compare-ReceiptToFallback $receipt $fallback
    Add-Count $resultCounts $decision.Result
    Add-Count $reasonCounts $decision.Reason
    $rows.Add([pscustomobject]@{
        routeKey = $key
        result = $decision.Result
        reason = $decision.Reason
        confirmedBytes = Get-Int64Field $receipt "confirmedBytes"
        fileSize = if ($null -eq $fallback) { 0L } else { Get-Int64Field $fallback "fileSize" }
    })
}

Write-Host "large file delivered reconciliation"
Write-Host ("  receipts: {0}" -f $receipts.Count)
Write-Host ("  fallbacks: {0}" -f $fallbacks.Count)
Write-Host ("  decisions: {0}" -f $rows.Count)

Write-Host ""
Write-Host "results"
foreach ($entry in ($resultCounts.GetEnumerator() | Sort-Object Name)) {
    Write-Host ("  {0}: {1}" -f $entry.Name, $entry.Value)
}

Write-Host ""
Write-Host "reasons"
foreach ($entry in ($reasonCounts.GetEnumerator() | Sort-Object Name)) {
    Write-Host ("  {0}: {1}" -f $entry.Name, $entry.Value)
}

Write-Host ""
Write-Host "decision rows"
foreach ($row in ($rows | Sort-Object routeKey)) {
    Write-Host ("  {0} result={1} reason={2} confirmedBytes={3} fileSize={4}" -f
        $row.routeKey, $row.result, $row.reason, $row.confirmedBytes, $row.fileSize)
}

if ($EmitRouteLog) {
    Write-Host ""
    Write-Host "route log events"
    foreach ($row in ($rows | Sort-Object routeKey)) {
        $parts = $row.routeKey -split "\|", 3
        $transferId = if ($parts.Count -gt 0) { $parts[0] } else { "" }
        $objectKey = if ($parts.Count -gt 1) { $parts[1] } else { "" }
        $receiverId = if ($parts.Count -gt 2) { $parts[2] } else { "" }
        Write-Host ("redis_large_file_route event=delivered_reconcile result={0} reason={1} operation=reconcile transferId={2} objectKey={3} receiverId={4} bytes={5}" -f
            (ConvertTo-RouteLogValue $row.result "retained"),
            (ConvertTo-RouteLogValue $row.reason "unknown"),
            (ConvertTo-RouteLogValue $transferId "unknown"),
            (ConvertTo-RouteLogValue $objectKey "unknown"),
            (ConvertTo-RouteLogValue $receiverId "unknown"),
            [int64]$row.confirmedBytes)
    }
}

$sensitiveHits = Find-SensitiveHits @($ReceiptPath, $FallbackPath)
if ($sensitiveHits.Count -gt 0) {
    Write-Host ""
    Write-Host "sensitive hits"
    foreach ($hit in $sensitiveHits) {
        Write-Host ("  {0}" -f $hit)
    }
    if (-not $NoFailOnSensitive) {
        throw "Sensitive S3/ObjectStore fields were found in reconciliation inputs."
    }
}
