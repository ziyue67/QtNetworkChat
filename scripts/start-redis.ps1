Get-Process -Name redis-server -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Remove-Item -Path "D:\Program Files\Redis-8.6.2\redis_6379.pid" -Force -ErrorAction SilentlyContinue

$conn = Get-NetTCPConnection -LocalPort 6379 -ErrorAction SilentlyContinue | Select-Object -First 1
if ($conn) {
    $proc = Get-Process -Id $conn.OwningProcess -ErrorAction SilentlyContinue
    if ($proc) {
        Write-Host "Port 6379 is occupied by PID $($proc.Id) ($($proc.Name)). Stopping it..."
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
    }
}

Start-Process -FilePath "D:\Program Files\Redis-8.6.2\redis-server.exe" -ArgumentList "D:\Program Files\Redis-8.6.2\redis.conf" -WorkingDirectory "D:\Program Files\Redis-8.6.2" -WindowStyle Hidden
Start-Sleep -Seconds 3

$redis = Get-Process -Name redis-server -ErrorAction SilentlyContinue | Select-Object -First 1
if ($redis) {
    Write-Host "Redis started successfully. PID=$($redis.Id)"
} else {
    Write-Host "Redis failed to start."
}
Get-NetTCPConnection -LocalPort 6379 -ErrorAction SilentlyContinue | Select-Object LocalPort, OwningProcess, @{Name='ProcessName'; Expression={(Get-Process -Id $_.OwningProcess -ErrorAction SilentlyContinue).Name}} | Format-Table -AutoSize
