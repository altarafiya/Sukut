param(
    [string]$EvidenceDirectory = 'docs\steps\step-07\evidence\native-live',
    [string]$RouteFile = 'docs\route.json'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$routePath = if ([IO.Path]::IsPathRooted($RouteFile)) {$RouteFile} else {Join-Path $projectRoot $RouteFile}
$route = Get-Content -LiteralPath $routePath -Raw | ConvertFrom-Json
$target = if ([IO.Path]::IsPathRooted($EvidenceDirectory)) {$EvidenceDirectory} else {Join-Path $projectRoot $EvidenceDirectory}
& (Join-Path $projectRoot 'build\bootstrap\bin\Release\cleanwave_live_verify.exe') `
    (Join-Path $projectRoot 'assets\engine\separator.tflite') `
    (Join-Path $projectRoot 'assets\engine\window_576_q15.bin') `
    (Join-Path $projectRoot 'assets\engine\window_960_q15.bin') `
    $route.source_render_id $route.capture_id $route.physical_render_id `
    (Join-Path $projectRoot 'build\bootstrap\bin\Release\cleanwave_live_source.exe') $target
if ($LASTEXITCODE -ne 0) { throw "Step 7 native live verification failed: $LASTEXITCODE" }
