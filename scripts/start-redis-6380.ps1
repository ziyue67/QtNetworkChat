. $env:PATH
Get-Process -Name redis-server -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

$redisDir = "D:\Program Files\Redis-8.6.2"
$conf = "$PSScriptRoot\redis-6380.conf"

if (-not (Test-Path $conf)) {
    Write-Host "Redis config not found: $conf"
    exit 1
}

Start-Process -FilePath "$redisDir\redis-server.exe" -ArgumentList $conf -WorkingDirectory $redisDir -WindowStyle Hidden
Start-Sleep -Seconds 2

$redis = Get-Process -Name redis-server -ErrorAction SilentlyContinue | Select-Object -First 1
if ($redis) {
    Write-Host "Redis started on port 6380. PID=$($redis.Id)"
} else {
    Write-Host "Redis failed to start."
}
