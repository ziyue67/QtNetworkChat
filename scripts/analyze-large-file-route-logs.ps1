param(
    [Parameter(Mandatory = $true)]
    [string[]]$Path,

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

function Add-Count([hashtable]$Table, [string]$Key) {
    if ([string]::IsNullOrWhiteSpace($Key)) {
        $Key = "<empty>"
    }
    if (-not $Table.ContainsKey($Key)) {
        $Table[$Key] = 0
    }
    $Table[$Key] += 1
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
        $key = $part.Substring(0, $separator)
        $value = $part.Substring($separator + 1)
        $fields[$key] = $value
    }
    $fields
}

$eventCounts = @{}
$resultCounts = @{}
$reasonCounts = @{}
$operationCounts = @{}
$storeCounts = @{}
$sensitiveHits = New-Object System.Collections.Generic.List[string]
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

        $fieldText = $Matches["fields"]
        $routeLineCount += 1
        foreach ($pattern in $sensitivePatterns) {
            if ($line -match $pattern) {
                $sensitiveHits.Add(("{0}:{1}:{2}" -f $logPath, $lineNumber, $pattern))
            }
        }

        $fields = ConvertTo-RouteFields $fieldText
        Add-Count $eventCounts $fields["event"]
        Add-Count $resultCounts $fields["result"]
        Add-Count $reasonCounts $fields["reason"]
        Add-Count $operationCounts $fields["operation"]
        Add-Count $storeCounts $fields["storeType"]
    }
}

function Write-Counts([string]$Title, [hashtable]$Table) {
    Write-Host ""
    Write-Host $Title
    foreach ($entry in ($Table.GetEnumerator() | Sort-Object Name)) {
        Write-Host ("  {0}: {1}" -f $entry.Name, $entry.Value)
    }
}

Write-Host ("redis_large_file_route lines: {0}" -f $routeLineCount)
Write-Counts "events" $eventCounts
Write-Counts "results" $resultCounts
Write-Counts "reasons" $reasonCounts
Write-Counts "operations" $operationCounts
Write-Counts "stores" $storeCounts

if ($sensitiveHits.Count -gt 0) {
    Write-Host ""
    Write-Host "sensitive hits"
    foreach ($hit in $sensitiveHits) {
        Write-Host ("  {0}" -f $hit)
    }
    if (-not $NoFailOnSensitive) {
        throw "Sensitive S3/ObjectStore fields were found in redis_large_file_route logs."
    }
}
