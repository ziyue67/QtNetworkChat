param(
    [string]$Endpoint = "http://127.0.0.1:9000",
    [string]$Bucket = "qtchat-large-files",
    [string]$Region = "us-east-1",
    [string]$AccessKey = "qtchat-dev",
    [string]$SecretKey = "qtchat-dev-secret",
    [string]$Prefix = "qtchat/manual-smoke",
    [string]$ContainerName = "qtchat-minio-smoke",
    [switch]$SkipContainer,
    [Alias("MinioServerPath")]
    [string]$MinioExePath,
    [string]$DataDir,
    [int]$ConsolePort = 9001,
    [switch]$KeepLocalServer,
    [string]$SanitizedRouteLogPath,
    [string]$SanitizedSummaryPath,
    [string]$SanitizedSmokeLogPath
)

$ErrorActionPreference = "Stop"

function ConvertTo-Hex([byte[]]$Bytes) {
    $builder = [System.Text.StringBuilder]::new($Bytes.Length * 2)
    foreach ($byte in $Bytes) {
        [void]$builder.Append($byte.ToString("x2"))
    }
    $builder.ToString()
}

function Get-Sha256Hex([byte[]]$Bytes) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        ConvertTo-Hex $sha.ComputeHash($Bytes)
    } finally {
        $sha.Dispose()
    }
}

function Get-HmacSha256([byte[]]$Key, [string]$Data) {
    $hmac = [System.Security.Cryptography.HMACSHA256]::new($Key)
    try {
        $hmac.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Data))
    } finally {
        $hmac.Dispose()
    }
}

function Join-S3Path([string]$Left, [string]$Right) {
    $l = $Left.TrimEnd("/")
    $r = $Right.TrimStart("/")
    if ([string]::IsNullOrWhiteSpace($l)) { return $r }
    if ([string]::IsNullOrWhiteSpace($r)) { return $l }
    "$l/$r"
}

function Get-S3AuthorizationHeader(
    [string]$Method,
    [Uri]$Uri,
    [string]$PayloadHash,
    [string]$AmzDate,
    [string]$Date,
    [string]$Region,
    [string]$AccessKey,
    [string]$SecretKey
) {
    $hostHeader = $Uri.Host
    if (($Uri.Scheme -eq "http" -and $Uri.Port -ne 80) -or ($Uri.Scheme -eq "https" -and $Uri.Port -ne 443)) {
        $hostHeader = "$($Uri.Host):$($Uri.Port)"
    }

    $canonicalHeaders = "host:$hostHeader`n" +
        "x-amz-content-sha256:$PayloadHash`n" +
        "x-amz-date:$AmzDate`n"
    $signedHeaders = "host;x-amz-content-sha256;x-amz-date"
    $canonicalRequest = $Method.ToUpperInvariant() + "`n" +
        $Uri.AbsolutePath + "`n" +
        $Uri.Query.TrimStart("?") + "`n" +
        $canonicalHeaders + "`n" +
        $signedHeaders + "`n" +
        $PayloadHash
    $scope = "$Date/$Region/s3/aws4_request"
    $canonicalHash = Get-Sha256Hex ([System.Text.Encoding]::UTF8.GetBytes($canonicalRequest))
    $stringToSign = "AWS4-HMAC-SHA256`n$AmzDate`n$scope`n$canonicalHash"

    $kDate = Get-HmacSha256 ([System.Text.Encoding]::UTF8.GetBytes("AWS4$SecretKey")) $Date
    $kRegion = Get-HmacSha256 $kDate $Region
    $kService = Get-HmacSha256 $kRegion "s3"
    $kSigning = Get-HmacSha256 $kService "aws4_request"
    $signature = ConvertTo-Hex (Get-HmacSha256 $kSigning $stringToSign)

    [PSCustomObject]@{
        HostHeader = $hostHeader
        SignedHeaders = $signedHeaders
        Authorization = "AWS4-HMAC-SHA256 Credential=$AccessKey/$scope,SignedHeaders=$signedHeaders,Signature=$signature"
    }
}

function Invoke-S3Request(
    [string]$Method,
    [string]$Url,
    [byte[]]$Body = [byte[]]::new(0)
) {
    $uri = [Uri]$Url
    $now = [DateTime]::UtcNow
    $amzDate = $now.ToString("yyyyMMddTHHmmssZ")
    $date = $now.ToString("yyyyMMdd")
    $payloadHash = Get-Sha256Hex $Body
    $signed = Get-S3AuthorizationHeader $Method $uri $payloadHash $amzDate $date $Region $AccessKey $SecretKey
    $headers = @{
        "host" = $signed.HostHeader
        "x-amz-content-sha256" = $payloadHash
        "x-amz-date" = $amzDate
        "Authorization" = $signed.Authorization
    }

    $invokeArgs = @{
        Method = $Method
        Uri = $uri
        Headers = $headers
        UseBasicParsing = $true
    }
    if ($Body.Length -gt 0 -or $Method -eq "PUT") {
        $invokeArgs["Body"] = $Body
        $invokeArgs["ContentType"] = "application/octet-stream"
    }
    Invoke-WebRequest @invokeArgs
}

function Test-TransientMinioStartupError($ErrorRecord) {
    if ($null -eq $ErrorRecord -or $null -eq $ErrorRecord.Exception) {
        return $false
    }
    $response = $ErrorRecord.Exception.Response
    $statusCode = 0
    if ($null -ne $response -and $null -ne $response.StatusCode) {
        $statusCode = [int]$response.StatusCode
    }
    $message = [string]$ErrorRecord.Exception.Message
    if ($null -ne $ErrorRecord.ErrorDetails -and -not [string]::IsNullOrWhiteSpace($ErrorRecord.ErrorDetails.Message)) {
        $message += " " + [string]$ErrorRecord.ErrorDetails.Message
    }

    $transientServer = $statusCode -eq 0 -or $statusCode -eq 503
    $initializing = $message -match "XMinioServerNotInitialized" `
        -or $message -match "Server not initialized yet"
    $transientServer -and $initializing
}

function Invoke-S3RequestWithStartupRetry(
    [string]$Method,
    [string]$Url,
    [byte[]]$Body = [byte[]]::new(0)
) {
    for ($attempt = 1; $attempt -le 10; ++$attempt) {
        try {
            return Invoke-S3Request $Method $Url $Body
        } catch {
            if (-not (Test-TransientMinioStartupError $_) -or $attempt -eq 10) {
                throw
            }
            Start-Sleep -Milliseconds (250 * $attempt)
        }
    }
}

function Get-ResponseBytes($Response) {
    if ($null -ne $Response.RawContentStream) {
        $stream = $Response.RawContentStream
        if ($stream.CanSeek) {
            $stream.Position = 0
        }
        $memory = [System.IO.MemoryStream]::new()
        try {
            $stream.CopyTo($memory)
            return $memory.ToArray()
        } finally {
            $memory.Dispose()
        }
    }
    [System.Text.Encoding]::UTF8.GetBytes([string]$Response.Content)
}

function Wait-MinIOReady([string]$Endpoint) {
    for ($attempt = 1; $attempt -le 30; ++$attempt) {
        try {
            Invoke-WebRequest -UseBasicParsing -Uri "$Endpoint/minio/health/ready" -TimeoutSec 2 | Out-Null
            return
        } catch {
            Start-Sleep -Seconds 1
        }
    }
    throw "MinIO did not become ready at $Endpoint"
}

function Write-ParentDirectory([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return
    }
    $parent = Split-Path -Parent $PathValue
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

function Add-SmokeRouteLine([System.Collections.Generic.List[string]]$Lines,
                            [string]$EventName,
                            [string]$Result,
                            [string]$Operation,
                            [string]$Reason,
                            [long]$Bytes = -1) {
    $fields = @(
        "event=$EventName",
        "result=$Result",
        "storeType=s3",
        "operation=$Operation",
        "reason=$Reason"
    )
    if ($Bytes -ge 0) {
        $fields += "bytes=$Bytes"
    }
    $Lines.Add(("redis_large_file_route {0}" -f ($fields -join " ")))
}

function Start-MinioProcess {
    param(
        [string]$ExecutablePath,
        [string[]]$Arguments
    )

    $startArgs = @{
        FilePath = $ExecutablePath
        ArgumentList = $Arguments
        PassThru = $true
    }
    if ($IsWindows -or [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT) {
        $startArgs["WindowStyle"] = "Hidden"
    }
    Start-Process @startArgs
}

$localMinioProcess = $null
if (-not [string]::IsNullOrWhiteSpace($MinioExePath)) {
    if (-not (Test-Path -LiteralPath $MinioExePath -PathType Leaf)) {
        throw "MinioServerPath/MinioExePath not found: $MinioExePath"
    }
    if ([string]::IsNullOrWhiteSpace($DataDir)) {
        $DataDir = Join-Path ([System.IO.Path]::GetTempPath()) "qtnetworkchat-minio-smoke-data"
    }
    New-Item -ItemType Directory -Path $DataDir -Force | Out-Null
    $env:MINIO_ROOT_USER = $AccessKey
    $env:MINIO_ROOT_PASSWORD = $SecretKey
    $endpointUri = [Uri]$Endpoint
    $listenAddress = "{0}:{1}" -f $endpointUri.Host, $endpointUri.Port
    $consoleAddress = "127.0.0.1:{0}" -f $ConsolePort
    $localMinioProcess = Start-MinioProcess `
        -ExecutablePath $MinioExePath `
        -Arguments @("server", $DataDir, "--address", $listenAddress, "--console-address", $consoleAddress)
} elseif (-not $SkipContainer) {
    $docker = Get-Command docker -ErrorAction SilentlyContinue
    if (-not $docker) {
        throw "Docker was not found. Install Docker, start MinIO yourself, pass -MinioExePath, or rerun with -SkipContainer."
    }

    $existing = docker ps -a --filter "name=^/$ContainerName$" --format "{{.Names}}"
    if (-not $existing) {
        $endpointUri = [Uri]$Endpoint
        docker run -d --name $ContainerName `
            -p "$($endpointUri.Port):9000" -p "$($ConsolePort):9001" `
            -e "MINIO_ROOT_USER=$AccessKey" `
            -e "MINIO_ROOT_PASSWORD=$SecretKey" `
            minio/minio server /data --console-address ":9001" | Out-Null
    } else {
        docker start $ContainerName | Out-Null
    }
}

$routeLines = [System.Collections.Generic.List[string]]::new()
$smokeOk = $false
$operations = [ordered]@{
    createBucket = $false
    put = $false
    head = $false
    get = $false
    delete = $false
}

try {
    Wait-MinIOReady $Endpoint

    $bucketUrl = "$( $Endpoint.TrimEnd('/') )/$Bucket"
    try {
        Invoke-S3RequestWithStartupRetry "PUT" $bucketUrl | Out-Null
        $operations.createBucket = $true
        Add-SmokeRouteLine $routeLines "real_backend_smoke" "published" "put" "success"
    } catch {
        if ($_.Exception.Response.StatusCode.value__ -ne 409) {
            Add-SmokeRouteLine $routeLines "real_backend_smoke" "rejected" "put" "server"
            throw
        }
        $operations.createBucket = $true
        Add-SmokeRouteLine $routeLines "real_backend_smoke" "published" "put" "success"
    }

    $safePrefix = $Prefix.Trim("/").Replace("\", "/")
    $objectKey = Join-S3Path $safePrefix ("manual-smoke-{0}.txt" -f ([Guid]::NewGuid().ToString("N")))
    $objectUrl = "$( $Endpoint.TrimEnd('/') )/$Bucket/$objectKey"
    $payloadText = "QtNetworkChat MinIO S3 smoke " + [DateTime]::UtcNow.ToString("o")
    $payloadBytes = [System.Text.Encoding]::UTF8.GetBytes($payloadText)
    $expectedHash = Get-Sha256Hex $payloadBytes

    Invoke-S3RequestWithStartupRetry "PUT" $objectUrl $payloadBytes | Out-Null
    $operations.put = $true
    Add-SmokeRouteLine $routeLines "real_backend_smoke" "published" "put" "success" $payloadBytes.Length

    Invoke-S3RequestWithStartupRetry "HEAD" $objectUrl | Out-Null
    $operations.head = $true
    Add-SmokeRouteLine $routeLines "real_backend_smoke" "published" "head" "success" $payloadBytes.Length

    $download = Invoke-S3RequestWithStartupRetry "GET" $objectUrl
    $downloadBytes = Get-ResponseBytes $download
    $actualHash = Get-Sha256Hex $downloadBytes
    if ($actualHash -ne $expectedHash) {
        Add-SmokeRouteLine $routeLines "real_backend_smoke" "rejected" "get" "hash" $downloadBytes.Length
        throw "Downloaded object hash mismatch."
    }
    $operations.get = $true
    Add-SmokeRouteLine $routeLines "real_backend_smoke" "published" "get" "success" $downloadBytes.Length

    Invoke-S3RequestWithStartupRetry "DELETE" $objectUrl | Out-Null
    $operations.delete = $true
    Add-SmokeRouteLine $routeLines "real_backend_smoke" "cleaned" "delete" "success" $payloadBytes.Length
    $smokeOk = $true

    if (-not [string]::IsNullOrWhiteSpace($SanitizedRouteLogPath)) {
        Write-ParentDirectory $SanitizedRouteLogPath
        $routeLines | Set-Content -LiteralPath $SanitizedRouteLogPath -Encoding UTF8
    }
    if (-not [string]::IsNullOrWhiteSpace($SanitizedSummaryPath)) {
        Write-ParentDirectory $SanitizedSummaryPath
        [pscustomobject]@{
            format = "qtnetworkchat-minio-s3-smoke-summary-v1"
            generatedAt = (Get-Date).ToUniversalTime().ToString("o")
            ok = $smokeOk
            readOnlyEvidence = $false
            serverMode = if ($localMinioProcess) { "local-binary" } elseif ($SkipContainer) { "external" } else { "docker" }
            operations = [pscustomobject]$operations
            sanitizedRouteLogPath = if ([string]::IsNullOrWhiteSpace($SanitizedRouteLogPath)) { "" } else { $SanitizedRouteLogPath }
            notes = "Summary is sanitized: no endpoint, bucket, object URL, credentials, Authorization, Credential, or Signature values are written."
        } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $SanitizedSummaryPath -Encoding UTF8
    }
    if (-not [string]::IsNullOrWhiteSpace($SanitizedSmokeLogPath)) {
        Write-ParentDirectory $SanitizedSmokeLogPath
        $serverMode = if ($localMinioProcess) { "local-binary" } elseif ($SkipContainer) { "external" } else { "docker" }
        @(
            "MinIO S3 smoke passed.",
            "serverMode={0}" -f $serverMode,
            "operations=createBucket,put,head,get,delete",
            "sensitiveFields=redacted"
        ) | Set-Content -LiteralPath $SanitizedSmokeLogPath -Encoding UTF8
    }

    Write-Host "MinIO S3 smoke passed."
    Write-Host "Endpoint: $Endpoint"
    Write-Host "Bucket: $Bucket"
    Write-Host "ObjectKey: $objectKey"
    Write-Host ""
    Write-Host "QtNetworkChat environment example:"
    if ($IsWindows -or [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT) {
        Write-Host "  set QTNETWORKCHAT_OBJECT_STORE=s3"
        Write-Host "  set QTNETWORKCHAT_OBJECT_S3_ENDPOINT=$Endpoint"
        Write-Host "  set QTNETWORKCHAT_OBJECT_S3_BUCKET=$Bucket"
        Write-Host "  set QTNETWORKCHAT_OBJECT_S3_REGION=$Region"
        Write-Host "  set QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY=$AccessKey"
        Write-Host "  set QTNETWORKCHAT_OBJECT_S3_SECRET_KEY=<redacted>"
        Write-Host "  set QTNETWORKCHAT_OBJECT_S3_PREFIX=$safePrefix"
    } else {
        Write-Host "  export QTNETWORKCHAT_OBJECT_STORE=s3"
        Write-Host "  export QTNETWORKCHAT_OBJECT_S3_ENDPOINT='$Endpoint'"
        Write-Host "  export QTNETWORKCHAT_OBJECT_S3_BUCKET='$Bucket'"
        Write-Host "  export QTNETWORKCHAT_OBJECT_S3_REGION='$Region'"
        Write-Host "  export QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY='$AccessKey'"
        Write-Host "  export QTNETWORKCHAT_OBJECT_S3_SECRET_KEY='<redacted>'"
        Write-Host "  export QTNETWORKCHAT_OBJECT_S3_PREFIX='$safePrefix'"
    }
} finally {
    if ($localMinioProcess -and -not $KeepLocalServer) {
        Stop-Process -Id $localMinioProcess.Id -Force -ErrorAction SilentlyContinue
        $localMinioProcess.WaitForExit(5000) | Out-Null
    }
}
