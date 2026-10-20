param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) {
    throw "Bundled CMake was not found at $cmake"
}

Push-Location $projectRoot
try {
    & $cmake --preset bootstrap-msvc
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE"
    }

    & $cmake --build build\bootstrap --config $Configuration --target cleanwave_model_verify --parallel 4
    if ($LASTEXITCODE -ne 0) {
        throw "Step 3 model verification build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
