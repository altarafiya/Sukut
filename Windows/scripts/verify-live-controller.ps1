param([string]$RouteFile = 'docs\steps\step-07\evidence\test-route-realtek.json')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$routePath = if ([IO.Path]::IsPathRooted($RouteFile)) {$RouteFile} else {Join-Path $projectRoot $RouteFile}
$route = Get-Content -LiteralPath $routePath -Raw | ConvertFrom-Json
$evidenceRoot = Join-Path $projectRoot 'docs\steps\step-07\evidence\cli-control'
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$commands = Join-Path $evidenceRoot 'commands.txt'
$status = Join-Path $evidenceRoot 'status.json'
'1 off' | Set-Content -LiteralPath $commands -Encoding ASCII
$liveProcess = $null; $sourceProcess = $null
function Quote-ProcessArgument([string]$value) {
    if ($value.Contains('"')) {throw 'Unexpected quote in process argument'}
    '"' + $value + '"'
}
function Read-LiveStatus {
    try {Get-Content -LiteralPath $status -Raw | ConvertFrom-Json} catch {$null}
}
function Wait-LiveStatus([scriptblock]$condition, [string]$label) {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $current = Read-LiveStatus
        if ($current -and (& $condition $current)) {
            $current | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $evidenceRoot "$label.json") -Encoding UTF8
            return $current
        }
        if ($liveProcess.HasExited) {throw "Live process exited during $label"}
        Start-Sleep -Milliseconds 40
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out waiting for $label"
}
function Send-LiveCommand([int]$id, [string]$text) {
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        try {"$id $text" | Add-Content -LiteralPath $commands -Encoding ASCII; return}
        catch [System.IO.IOException] {Start-Sleep -Milliseconds 10}
    }
    throw 'CLI test command file remained locked'
}
try {
    $arguments = @(
        (Join-Path $projectRoot 'assets\engine\separator.tflite'),
        (Join-Path $projectRoot 'assets\engine\window_576_q15.bin'),
        (Join-Path $projectRoot 'assets\engine\window_960_q15.bin'),
        $route.source_render_id, $route.capture_id, $route.physical_render_id, $status, $commands, '25'
    ) | ForEach-Object {Quote-ProcessArgument $_}
    $liveProcess = Start-Process -FilePath (Join-Path $projectRoot 'build\bootstrap\bin\Release\cleanwave_live.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $evidenceRoot 'live.stdout.log') -RedirectStandardError (Join-Path $evidenceRoot 'live.stderr.log')
    $liveHandle = $liveProcess.Handle # retain the native handle before the process exits
    $initial = Wait-LiveStatus {param($s) $s.state -eq 'Running' -and $s.capture.open -and $s.pipeline.processed -ge 20} 'initial-Off'
    $sourceArguments = @($route.source_render_id,'48000','16',(Join-Path $evidenceRoot 'source.txt')) | ForEach-Object {Quote-ProcessArgument $_}
    $sourceProcess = Start-Process -FilePath (Join-Path $projectRoot 'build\bootstrap\bin\Release\cleanwave_live_source.exe') -ArgumentList $sourceArguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $evidenceRoot 'source.stdout.log') -RedirectStandardError (Join-Path $evidenceRoot 'source.stderr.log')
    $sourceHandle = $sourceProcess.Handle
    Send-LiveCommand 2 'default'
    $default = Wait-LiveStatus {param($s) $s.applied_mode -eq 'Default' -and $s.cleaner.ai_runs -gt 0} 'Default'
    Send-LiveCommand 3 'custom -3 -16 -20 80 2'
    $custom = Wait-LiveStatus {param($s) $s.applied_mode -eq 'Custom' -and $s.custom.voice_db -eq -3 -and $s.custom.reduction_percent -eq 80 -and $s.custom.boost_db -eq 2} 'Custom-configured'
    Send-LiveCommand 4 'default'
    $defaultAgain = Wait-LiveStatus {param($s) $s.applied_mode -eq 'Default' -and $s.custom.music_db -eq -16 -and $s.custom.boost_db -eq 2} 'Default-preserves-Custom'
    Send-LiveCommand 5 'off'
    $off = Wait-LiveStatus {param($s) $s.applied_mode -eq 'Off' -and $s.applied_revision -eq $s.requested_revision} 'Off'
    $offLater = Wait-LiveStatus {param($s) $s.applied_mode -eq 'Off' -and $s.pipeline.processed -gt $off.pipeline.processed + 20} 'Off-later'
    if ($offLater.cleaner.ai_runs -ne $off.cleaner.ai_runs) {throw 'Off continued inference'}
    Send-LiveCommand 6 'custom'
    $restored = Wait-LiveStatus {param($s) $s.applied_mode -eq 'Custom' -and $s.custom.voice_db -eq -3 -and $s.custom.boost_db -eq 2} 'Custom-restored'
    Send-LiveCommand 7 'default unexpected'
    Start-Sleep -Milliseconds 1100
    $invalid = Read-LiveStatus
    if ($invalid.requested_mode -ne 'Custom') {throw 'Rejected command changed mode'}
    $invalid | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'invalid-command-preserves-mode.json') -Encoding UTF8
    Send-LiveCommand 8 'restart'
    $restart = Wait-LiveStatus {param($s) $s.generation -eq 2 -and $s.state -eq 'Running' -and $s.applied_mode -eq 'Custom' -and $s.custom.voice_db -eq -3} 'Restart'
    Send-LiveCommand 9 'stop'
    if (-not $liveProcess.WaitForExit(5000)) {throw 'Live command stop did not exit'}
    $liveProcess.WaitForExit(); $liveProcess.Refresh()
    if ($liveProcess.ExitCode -ne 0) {throw "Live CLI failed: $($liveProcess.ExitCode)"}
    $final = Read-LiveStatus
    if ($final.state -ne 'Stopped' -or $final.capture.open -or $final.render.open -or $final.error) {throw 'CLI final state is not cleanly stopped'}
    if (-not $sourceProcess.WaitForExit(18000)) {throw 'Independent CLI source timed out'}
    $sourceProcess.WaitForExit(); $sourceProcess.Refresh()
    if ($sourceProcess.ExitCode -ne 0) {throw 'Independent CLI source failed'}
    'RESULT=PASS modes=Off/Default/Custom Off-inference-stopped=PASS saved-Custom=PASS rejected-command-no-mutation=PASS restart-generation=2 clean-stop=PASS no-system-settings-changed' | Set-Content -LiteralPath (Join-Path $evidenceRoot 'result.txt') -Encoding ASCII
    Get-Content -LiteralPath (Join-Path $evidenceRoot 'result.txt')
    Get-Content -LiteralPath (Join-Path $evidenceRoot 'live.stdout.log') -Tail 5
    Get-Content -LiteralPath (Join-Path $evidenceRoot 'live.stderr.log')
} finally {
    foreach ($ownedProcess in @($liveProcess,$sourceProcess)) {
        if ($ownedProcess -and -not $ownedProcess.HasExited) {Stop-Process -Id $ownedProcess.Id; $ownedProcess.WaitForExit()}
    }
}
