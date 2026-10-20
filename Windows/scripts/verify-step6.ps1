param([string]$EvidenceDirectory = 'docs\steps\step-06\evidence')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    $route = Get-Content -LiteralPath 'docs\route.json' -Raw | ConvertFrom-Json
    $verifier = Join-Path $projectRoot 'build\bootstrap\bin\Release\cleanwave_audio_verify.exe'
    if (-not (Test-Path -LiteralPath $verifier)) { throw 'Run build-step6.ps1 first' }
    & $verifier $route.source_render_id $route.capture_id $route.physical_render_id $EvidenceDirectory
    if ($LASTEXITCODE -ne 0) { throw "Native backend verification failed: $LASTEXITCODE" }
}
finally { Pop-Location }
