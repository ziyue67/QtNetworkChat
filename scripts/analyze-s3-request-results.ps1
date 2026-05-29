param(
    [Parameter(Mandatory = $true)]
    [string[]]$Path,

    [string]$SummaryPath,

    [int]$WarnTimeout = 0,

    [int]$WarnRetryable = 0,

    [int]$WarnAuth = 0,

    [int]$WarnTls = 0,

    [int]$WarnHash = 0,

    [int]$WarnSize = 0,

    [int]$WarnSensitiveHits = 0,

    [switch]$NoFailOnWarning
)

$ErrorActionPreference = "Stop"

$allowedReasons = @(
    "success",
    "timeout",
    "network",
    "tls",
    "auth",
    "not_found",
    "retryable",
    "client",
    "server",
    "unknown",
    "size",
    "hash"
)

$requestOperations = @(
    "put",
    "get",
    "head",
    "delete",
    "write",
    "read",
    "validate",
    "remove",
    "upload",
    "download"
)

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

function Add-Count([hashtable]$Table, [string]$Key) {
    if ([string]::IsNullOrWhiteSpace($Key)) {
        $Key = "<empty>"
    }
    if (-not $Table.ContainsKey($Key)) {
        $Table[$Key] = 0
    }
    $Table[$Key] += 1
}

function Get-Count([hashtable]$Table, [string]$Key) {
    if (-not $Table.ContainsKey($Key)) {
        return 0
    }
    [int]$Table[$Key]
}

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
        $fields[$part.Substring(0, $separator)] = $part.Substring($separator + 1)
    }
    $fields
}

function ConvertTo-CountObject([hashtable]$Table) {
    $ordered = [ordered]@{}
    foreach ($entry in ($Table.GetEnumerator() | Sort-Object Name)) {
        $ordered[[string]$entry.Name] = [int]$entry.Value
    }
    [pscustomobject]$ordered
}

foreach ($value in @(
        @{ Name = "WarnTimeout"; Value = $WarnTimeout },
        @{ Name = "WarnRetryable"; Value = $WarnRetryable },
        @{ Name = "WarnAuth"; Value = $WarnAuth },
        @{ Name = "WarnTls"; Value = $WarnTls },
        @{ Name = "WarnHash"; Value = $WarnHash },
        @{ Name = "WarnSize"; Value = $WarnSize },
        @{ Name = "WarnSensitiveHits"; Value = $WarnSensitiveHits })) {
    if ($value.Value -lt 0) {
        throw ("{0} must be 0 or greater." -f $value.Name)
    }
}

$reasonCounts = @{}
$operationCounts = @{}
$resultCounts = @{}
$unknownReasons = @{}
$sensitiveHits = New-Object System.Collections.Generic.List[string]
$s3LineCount = 0
$routeLineCount = 0

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
        if ([string]$fields["storeType"] -ne "s3") {
            return
        }

        $reason = [string]$fields["reason"]
        $operation = ([string]$fields["operation"]).ToLowerInvariant()
        $result = [string]$fields["result"]
        if (-not $requestOperations.Contains($operation)) {
            return
        }

        $s3LineCount += 1
        Add-Count $reasonCounts $reason
        Add-Count $operationCounts $operation
        Add-Count $resultCounts $result

        if (-not [string]::IsNullOrWhiteSpace($reason) -and -not $allowedReasons.Contains($reason)) {
            Add-Count $unknownReasons $reason
        }
    }
}

$warnings = New-Object System.Collections.Generic.List[string]
if ($sensitiveHits.Count -gt $WarnSensitiveHits) {
    $warnings.Add(("sensitiveHits={0} exceeds threshold {1}" -f $sensitiveHits.Count, $WarnSensitiveHits))
}
foreach ($threshold in @(
        @{ Reason = "timeout"; Value = $WarnTimeout },
        @{ Reason = "retryable"; Value = $WarnRetryable },
        @{ Reason = "auth"; Value = $WarnAuth },
        @{ Reason = "tls"; Value = $WarnTls },
        @{ Reason = "hash"; Value = $WarnHash },
        @{ Reason = "size"; Value = $WarnSize })) {
    $count = Get-Count $reasonCounts $threshold.Reason
    if ($count -gt $threshold.Value) {
        $warnings.Add(("{0}={1} exceeds threshold {2}" -f $threshold.Reason, $count, $threshold.Value))
    }
}
if ($unknownReasons.Count -gt 0) {
    $warnings.Add(("unknown reason buckets found: {0}" -f (($unknownReasons.Keys | Sort-Object) -join ",")))
}

Write-Host "S3 large file route request results"
Write-Host ("  files: {0}" -f $Path.Count)
Write-Host ("  route lines: {0}" -f $routeLineCount)
Write-Host ("  s3 lines: {0}" -f $s3LineCount)
Write-Host ("  sensitive hits: {0}" -f $sensitiveHits.Count)

Write-Host ""
Write-Host "reasons"
foreach ($entry in ($reasonCounts.GetEnumerator() | Sort-Object Name)) {
    Write-Host ("  {0}: {1}" -f $entry.Name, $entry.Value)
}

Write-Host ""
Write-Host "operations"
foreach ($entry in ($operationCounts.GetEnumerator() | Sort-Object Name)) {
    Write-Host ("  {0}: {1}" -f $entry.Name, $entry.Value)
}

Write-Host ""
Write-Host "results"
foreach ($entry in ($resultCounts.GetEnumerator() | Sort-Object Name)) {
    Write-Host ("  {0}: {1}" -f $entry.Name, $entry.Value)
}

if ($sensitiveHits.Count -gt 0) {
    Write-Host ""
    Write-Host "sensitive hits"
    foreach ($hit in $sensitiveHits) {
        Write-Host ("  {0}" -f $hit)
    }
}

if (-not [string]::IsNullOrWhiteSpace($SummaryPath)) {
    $resolvedSummaryPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($SummaryPath)
    $summaryParent = Split-Path -Parent $resolvedSummaryPath
    if (-not [string]::IsNullOrWhiteSpace($summaryParent)) {
        New-Item -ItemType Directory -Path $summaryParent -Force | Out-Null
    }

    [pscustomobject]@{
        path = @($Path)
        routeLineCount = $routeLineCount
        s3LineCount = $s3LineCount
        reasonCounts = ConvertTo-CountObject $reasonCounts
        operationCounts = ConvertTo-CountObject $operationCounts
        resultCounts = ConvertTo-CountObject $resultCounts
        unknownReasonCounts = ConvertTo-CountObject $unknownReasons
        sensitiveHits = $sensitiveHits.Count
        warnings = @($warnings)
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resolvedSummaryPath -Encoding UTF8
}

if ($warnings.Count -gt 0) {
    Write-Host ""
    Write-Host "warnings"
    foreach ($warning in $warnings) {
        Write-Host ("  {0}" -f $warning)
    }
    if (-not $NoFailOnWarning) {
        throw "S3 request result warnings were found."
    }
}
