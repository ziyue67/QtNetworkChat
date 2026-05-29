param(
    [Parameter(Mandatory = $true)]
    [string[]]$QueuePath,

    [Parameter(Mandatory = $true)]
    [string]$SourceInstanceId,

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
    $records.ToArray()
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

function Add-Count([hashtable]$Table, [string]$Key) {
    if ([string]::IsNullOrWhiteSpace($Key)) {
        $Key = "<empty>"
    }
    if (-not $Table.ContainsKey($Key)) {
        $Table[$Key] = 0
    }
    $Table[$Key] += 1
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

$trimmedSourceInstanceId = $SourceInstanceId.Trim()
if ([string]::IsNullOrWhiteSpace($trimmedSourceInstanceId)) {
    throw "SourceInstanceId is required."
}

$sensitiveHits = Find-SensitiveHits $QueuePath
if ($sensitiveHits.Count -gt 0) {
    Write-Host "sensitive hits"
    foreach ($hit in $sensitiveHits) {
        Write-Host ("  {0}" -f $hit)
    }
    if (-not $NoFailOnSensitive) {
        throw "Sensitive S3/ObjectStore fields were found in fallback inputs."
    }
}

$fallbacks = New-Object System.Collections.Generic.List[object]
$reasonCounts = @{}
$inputCount = 0
foreach ($path in $QueuePath) {
    foreach ($record in (Read-JsonRecords $path)) {
        $inputCount += 1
        $objectKey = Get-StringField $record "objectStoreKey"
        if ([string]::IsNullOrWhiteSpace($objectKey)) {
            Add-Count $reasonCounts "no-object-store-key"
            continue
        }
        $transferId = Get-StringField $record "transferId"
        $receiverId = Get-StringField $record "receiverId"
        $invalidRoute = [string]::IsNullOrWhiteSpace($transferId) `
            -or [string]::IsNullOrWhiteSpace($receiverId) `
            -or -not (Test-SafeObjectKey $objectKey)
        if ($invalidRoute) {
            Add-Count $reasonCounts "invalid-route"
            continue
        }
        $fileHash = Get-StringField $record "fileHash"
        if (-not (Test-Sha256Hex $fileHash)) {
            Add-Count $reasonCounts "invalid-hash"
            continue
        }
        $fileSize = Get-Int64Field $record "fileSize"
        if ($fileSize -le 0) {
            Add-Count $reasonCounts "invalid-size"
            continue
        }

        $fallbacks.Add([pscustomobject]@{
            sourceInstanceId = $trimmedSourceInstanceId
            transferId = $transferId
            receiverId = $receiverId
            objectKey = $objectKey
            fileHash = $fileHash.ToLowerInvariant()
            fileSize = $fileSize
        })
    }
}

if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $parent = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $fallbacks | ForEach-Object { $_ | ConvertTo-Json -Compress } | Set-Content -LiteralPath $OutputPath -Encoding UTF8
}

Write-Host "large file fallback export"
Write-Host ("  input records: {0}" -f $inputCount)
Write-Host ("  exported fallbacks: {0}" -f $fallbacks.Count)
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    Write-Host ("  output: {0}" -f $OutputPath)
}

Write-Host ""
Write-Host "skipped reasons"
foreach ($entry in ($reasonCounts.GetEnumerator() | Sort-Object Name)) {
    Write-Host ("  {0}: {1}" -f $entry.Name, $entry.Value)
}
