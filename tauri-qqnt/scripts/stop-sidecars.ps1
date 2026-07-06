$ErrorActionPreference = 'SilentlyContinue'

$processNames = @(
  'QQNTServer',
  'QQNTEngine',
  'tauri-qqnt'
)

foreach ($name in $processNames) {
  Get-Process -Name $name | Stop-Process -Force
}

Start-Sleep -Milliseconds 300

foreach ($name in $processNames) {
  Get-Process -Name $name | Wait-Process -Timeout 2
}
