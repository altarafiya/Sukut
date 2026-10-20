param(
    [string]$EvidenceDirectory='docs\steps\step-08\evidence\transport-repeat',
    [string]$RouteFile='docs\route.json',
    [switch]$Continuous,
    [switch]$OrdinaryWorker
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$routePath=if([IO.Path]::IsPathRooted($RouteFile)){$RouteFile}else{Join-Path $projectRoot $RouteFile}
$target=if([IO.Path]::IsPathRooted($EvidenceDirectory)){$EvidenceDirectory}else{Join-Path $projectRoot $EvidenceDirectory}
$route=Get-Content -LiteralPath $routePath -Raw | ConvertFrom-Json
# Stop any other CleanWave bridge first. No audio recordings or routing setters.
$verificationOptions=@()
if($Continuous){$verificationOptions+='--continuous'}
if($OrdinaryWorker){$verificationOptions+='--ordinary-worker'}
& (Join-Path $projectRoot 'build\bootstrap\bin\Release\cleanwave_transport_verify.exe') `
    (Join-Path $projectRoot 'assets\engine\separator.tflite') `
    (Join-Path $projectRoot 'assets\engine\window_576_q15.bin') `
    (Join-Path $projectRoot 'assets\engine\window_960_q15.bin') `
    $route.source_render_id $route.capture_id $route.physical_render_id `
    (Join-Path $projectRoot 'build\bootstrap\bin\Release\cleanwave_live_source.exe') $target @verificationOptions
if($LASTEXITCODE -ne 0){throw "Transport verification failed: $LASTEXITCODE"}
