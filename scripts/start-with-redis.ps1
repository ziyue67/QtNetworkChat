param(
    [string]$RedisDir = "D:\Program Files\Redis-8.6.2",
    [string]$BuildDir = "build-qt6-mingw",
    [string]$Configuration = "Release",
    [string]$HostName = "127.0.0.1",
    [int]$Port = 6379,
    [string]$Prefix = "qtchat",
    [switch]$NoStartRedis
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$redisServer = Join-Path $RedisDir "redis-server.exe"
$redisCli = Join-Path $RedisDir "redis-cli.exe"
$redisConf = Join-Path $RedisDir "redis.conf"

if (-not (Test-Path -LiteralPath $redisServer)) {
    throw "redis-server.exe was not found under $RedisDir"
}
if (-not (Test-Path -LiteralPath $redisCli)) {
    throw "redis-cli.exe was not found under $RedisDir"
}

function Test-RedisReady {
    param([string]$CliPath, [string]$TargetHost, [int]$TargetPort)

    try {
        $pong = & $CliPath -h $TargetHost -p $TargetPort PING 2>$null
        return ($LASTEXITCODE -eq 0 -and ($pong -join "").Trim() -eq "PONG")
    } catch {
        return $false
    }
}

if (-not (Test-RedisReady -CliPath $redisCli -TargetHost $HostName -TargetPort $Port)) {
    if ($NoStartRedis) {
        throw "Redis is not reachable at ${HostName}:$Port"
    }

    $arguments = @()
    if (Test-Path -LiteralPath $redisConf) {
        $arguments += "`"$redisConf`""
    }
    $arguments += "--port"
    $arguments += $Port

    Write-Host "Starting Redis from $redisServer on ${HostName}:$Port"
    Start-Process -FilePath $redisServer -ArgumentList $arguments -WorkingDirectory $RedisDir -WindowStyle Hidden | Out-Null

    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        if (Test-RedisReady -CliPath $redisCli -TargetHost $HostName -TargetPort $Port) {
            break
        }
        Start-Sleep -Milliseconds 300
    }
}

if (-not (Test-RedisReady -CliPath $redisCli -TargetHost $HostName -TargetPort $Port)) {
    throw "Redis did not become ready at ${HostName}:$Port"
}

$buildPath = Join-Path $repoRoot $BuildDir
$exe = Get-ChildItem -Path $buildPath -Recurse -Filter "QtNetworkChat.exe" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $exe) {
    Write-Host "QtNetworkChat.exe was not found; building $BuildDir first"
    cmake --build $buildPath --config $Configuration
    $exe = Get-ChildItem -Path $buildPath -Recurse -Filter "QtNetworkChat.exe" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}
if (-not $exe) {
    throw "QtNetworkChat.exe was not found under $buildPath"
}

$env:QTNETWORKCHAT_REDIS = "1"
$env:QTNETWORKCHAT_REDIS_REQUIRED = "1"
$env:QTNETWORKCHAT_REDIS_HOST = $HostName
$env:QTNETWORKCHAT_REDIS_PORT = [string]$Port
$env:QTNETWORKCHAT_REDIS_PREFIX = $Prefix

Write-Host "Launching $($exe.FullName) with required Redis at ${HostName}:$Port"
& $exe.FullName
