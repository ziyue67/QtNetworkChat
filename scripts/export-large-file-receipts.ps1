param(
    [Parameter(Mandatory = $true)]
    [string[]]$Path,

    [string]$OutputPath,

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

function ConvertTo-RouteFields([string]$FieldText) {
    $fields = @{}
    foreach ($part in ($FieldText -split "\s+")) {
        if ([string]::IsNullOrWhiteSpace($part)) {
            continue
        }
        $separator = $part.IndexOf("=")
        if ($separator -le 0) {
            continue
        }
        $key = $part.Substring(0, $separator)
        $value = $part.Substring($separator + 1)
        $fields[$key] = $value
    }
    $fields
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

function Get-StringField([hashtable]$Fields, [string]$Name) {
    if (-not $Fields.ContainsKey($Name)) {
        return ""
    }
    ([string]$Fields[$Name]).Trim()
}

function Get-Int64Field([hashtable]$Fields, [string]$Name) {
    if (-not $Fields.ContainsKey($Name)) {
        return 0L
    }
    try {
        return [int64]$Fields[$Name]
    } catch {
        return 0L
    }
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

$receipts = New-Object System.Collections.Generic.List[object]
$reasonCounts = @{}
$routeLineCount = 0
$sensitiveHits = New-Object System.Collections.Generic.List[string]

foreach ($logPath in $Path) {
    if (-not (Test-Path -LiteralPath $logPath)) {
        throw "Log path not found: $logPath"
    }

    $lineNumber = 0
    Get-Content -LiteralPath $logPath | ForEach-Object {
        $lineNumber += 1
        $line = [string]$_
        if ($line -notmatch "redis_large_file_route\s+(?<fields>.+)$") {
            return
        }

        $routeLineCount += 1
        foreach ($pattern in $sensitivePatterns) {
            if ($line -match $pattern) {
                $sensitiveHits.Add(("{0}:{1}:{2}" -f $logPath, $lineNumber, $pattern))
            }
        }

        $fields = ConvertTo-RouteFields $Matches["fields"]
        $eventName = Get-StringField $fields "event"
        $resultName = Get-StringField $fields "result"
        if (($eventName -ne "delivered" -and $eventName -ne "delivered_reconcile") -or
            ($resultName -ne "published" -and $resultName -ne "cleaned")) {
            Add-Count $reasonCounts "not-delivered-receipt"
            return
        }

        $sourceInstanceId = Get-StringField $fields "sourceInstanceId"
        $transferId = Get-StringField $fields "transferId"
        $receiverId = Get-StringField $fields "receiverId"
        $objectKey = Get-StringField $fields "objectKey"
        $fileHash = Get-StringField $fields "fileHash"
        $confirmedBytes = Get-Int64Field $fields "bytes"
        if ([string]::IsNullOrWhiteSpace($sourceInstanceId) -or
            [string]::IsNullOrWhiteSpace($transferId) -or
            [string]::IsNullOrWhiteSpace($receiverId) -or
            -not (Test-SafeObjectKey $objectKey)) {
            Add-Count $reasonCounts "invalid-route"
            return
        }
        if (-not (Test-Sha256Hex $fileHash)) {
            Add-Count $reasonCounts "invalid-hash"
            return
        }
        if ($confirmedBytes -le 0) {
            Add-Count $reasonCounts "invalid-confirmed-bytes"
            return
        }

        $receipts.Add([pscustomobject]@{
            sourceInstanceId = $sourceInstanceId
            transferId = $transferId
            receiverId = $receiverId
            objectKey = $objectKey
            fileHash = $fileHash.ToLowerInvariant()
            confirmedBytes = $confirmedBytes
        })
    }
}

if ($sensitiveHits.Count -gt 0) {
    Write-Host "sensitive hits"
    foreach ($hit in $sensitiveHits) {
        Write-Host ("  {0}" -f $hit)
    }
    if (-not $NoFailOnSensitive) {
        throw "Sensitive S3/ObjectStore fields were found in route log inputs."
    }
}

if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $parent = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $receipts | ForEach-Object { $_ | ConvertTo-Json -Compress } | Set-Content -LiteralPath $OutputPath -Encoding UTF8
}

Write-Host "large file receipt export"
Write-Host ("  route log lines: {0}" -f $routeLineCount)
Write-Host ("  exported receipts: {0}" -f $receipts.Count)
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    Write-Host ("  output: {0}" -f $OutputPath)
}

Write-Host ""
Write-Host "skipped reasons"
foreach ($entry in ($reasonCounts.GetEnumerator() | Sort-Object Name)) {
    Write-Host ("  {0}: {1}" -f $entry.Name, $entry.Value)
}
