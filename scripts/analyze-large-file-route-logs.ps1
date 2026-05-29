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

function Get-RouteKey([hashtable]$Fields) {
    $transferId = [string]$Fields["transferId"]
    $objectKey = [string]$Fields["objectKey"]
    $receiverId = [string]$Fields["receiverId"]
    if ([string]::IsNullOrWhiteSpace($transferId) -and
        [string]::IsNullOrWhiteSpace($objectKey) -and
        [string]::IsNullOrWhiteSpace($receiverId)) {
        return $null
    }
    "{0}|{1}|{2}" -f $transferId, $objectKey, $receiverId
}

function Get-RouteState([hashtable]$States, [string]$Key) {
    if (-not $States.ContainsKey($Key)) {
        $States[$Key] = [ordered]@{
            Delivered = 0
            DeliveredCleaned = 0
            DeliveredRetained = 0
            ReconcileCleaned = 0
            ReconcileRetained = 0
            Failed = 0
            FallbackRetained = 0
            LastReason = ""
        }
    }
    $States[$Key]
}

$eventCounts = @{}
$resultCounts = @{}
$reasonCounts = @{}
$operationCounts = @{}
$storeCounts = @{}
$routeStates = @{}
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

        $routeKey = Get-RouteKey $fields
        if ($null -ne $routeKey) {
            $state = Get-RouteState $routeStates $routeKey
            $eventName = [string]$fields["event"]
            $resultName = [string]$fields["result"]
            if ($eventName -eq "delivered") {
                $state.Delivered += 1
            } elseif ($eventName -eq "delivered_cleanup" -and $resultName -eq "cleaned") {
                $state.DeliveredCleaned += 1
            } elseif ($eventName -eq "delivered_cleanup" -and $resultName -eq "retained") {
                $state.DeliveredRetained += 1
            } elseif ($eventName -eq "delivered_reconcile" -and $resultName -eq "cleaned") {
                $state.ReconcileCleaned += 1
            } elseif ($eventName -eq "delivered_reconcile" -and $resultName -eq "retained") {
                $state.ReconcileRetained += 1
            } elseif ($eventName -eq "failed") {
                $state.Failed += 1
            } elseif ($eventName -eq "failed_received" -and $resultName -eq "fallback-retained") {
                $state.FallbackRetained += 1
            }
            if (-not [string]::IsNullOrWhiteSpace([string]$fields["reason"])) {
                $state.LastReason = [string]$fields["reason"]
            }
        }
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

$deliveredCleaned = 0
$deliveredRetained = 0
$deliveredWithoutCleanup = 0
$reconcileCleaned = 0
$reconcileRetained = 0
$failedFallbackRetained = 0
$failedWithoutFallback = 0
foreach ($state in $routeStates.Values) {
    if ($state.DeliveredCleaned -gt 0) {
        $deliveredCleaned += 1
    }
    if ($state.DeliveredRetained -gt 0) {
        $deliveredRetained += 1
    }
    if ($state.Delivered -gt 0 -and $state.DeliveredCleaned -eq 0 -and $state.DeliveredRetained -eq 0) {
        $deliveredWithoutCleanup += 1
    }
    if ($state.ReconcileCleaned -gt 0) {
        $reconcileCleaned += 1
    }
    if ($state.ReconcileRetained -gt 0) {
        $reconcileRetained += 1
    }
    if ($state.FallbackRetained -gt 0) {
        $failedFallbackRetained += 1
    }
    if ($state.Failed -gt 0 -and $state.FallbackRetained -eq 0) {
        $failedWithoutFallback += 1
    }
}

Write-Host ""
Write-Host "delivery reconciliation candidates"
Write-Host ("  route keys: {0}" -f $routeStates.Count)
Write-Host ("  delivered cleaned: {0}" -f $deliveredCleaned)
Write-Host ("  delivered retained: {0}" -f $deliveredRetained)
Write-Host ("  delivered without cleanup log: {0}" -f $deliveredWithoutCleanup)
Write-Host ("  delivered reconcile cleaned: {0}" -f $reconcileCleaned)
Write-Host ("  delivered reconcile retained: {0}" -f $reconcileRetained)
Write-Host ("  failed fallback retained: {0}" -f $failedFallbackRetained)
Write-Host ("  failed without fallback log: {0}" -f $failedWithoutFallback)

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
