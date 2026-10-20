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
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
    # Reusable MSBuild child nodes can keep a process-tree logging wait alive
    # after a successful build. Keep this stage's helper processes short-lived.
    & $cmake --build build\bootstrap --config $Configuration --target cleanwave_realtime_verify --parallel 4 -- /nodeReuse:false
    if ($LASTEXITCODE -ne 0) { throw "Step 5 build failed: $LASTEXITCODE" }
}
finally { Pop-Location }
