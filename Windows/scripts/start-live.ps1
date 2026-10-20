param([ValidateRange(0,3600)][int]$Seconds = 0)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$route = Get-Content -LiteralPath (Join-Path $projectRoot 'docs\route.json') -Raw | ConvertFrom-Json
$runtimeRoot = Join-Path $projectRoot 'runtime'
New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
$commands = Join-Path $runtimeRoot 'live-commands.txt'
$status = Join-Path $runtimeRoot 'live-status.json'
'1 off' | Set-Content -LiteralPath $commands -Encoding ASCII
Write-Host "Status: $status"
Write-Host 'Use scripts\set-live-command.ps1 in another terminal to control modes/restart/stop.'
& (Join-Path $projectRoot 'build\bootstrap\bin\Release\cleanwave_live.exe') `
    (Join-Path $projectRoot 'assets\engine\separator.tflite') `
    (Join-Path $projectRoot 'assets\engine\window_576_q15.bin') `
    (Join-Path $projectRoot 'assets\engine\window_960_q15.bin') `
    $route.source_render_id $route.capture_id $route.physical_render_id $status $commands $Seconds
if ($LASTEXITCODE -ne 0) { throw "CleanWave live stopped with an error; inspect $status" }
