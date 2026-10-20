param([ValidateSet('Release','Debug')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
Push-Location $projectRoot
try {
    & $cmake --preset bootstrap-msvc
    if ($LASTEXITCODE -ne 0) { throw "Configure failed: $LASTEXITCODE" }
    & $cmake --build build\bootstrap --config $Configuration --target cleanwave_step7 --parallel 4 -- /nodeReuse:false
    if ($LASTEXITCODE -ne 0) { throw "Step 7 build failed: $LASTEXITCODE" }
} finally { Pop-Location }
