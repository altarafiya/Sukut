param(
    [ValidateSet('Release','Debug')][string]$Configuration='Release',
    [ValidateSet('Existing','Baseline','Avx','Avx2')][string]$CpuRuntime='Existing'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$cmake='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
Push-Location $projectRoot
try {
    $runtimeOptions=@()
    if($CpuRuntime -ne 'Existing'){$runtimeOptions+=('-DCLEANWAVE_CPU_RUNTIME='+$CpuRuntime)}
    & $cmake --preset bootstrap-msvc @runtimeOptions
    if($LASTEXITCODE -ne 0){throw "Configure failed: $LASTEXITCODE"}
    & $cmake --build build\bootstrap --config $Configuration --target cleanwave_step8 --parallel 4 -- /nodeReuse:false
    if($LASTEXITCODE -ne 0){throw "Step 8 build failed: $LASTEXITCODE"}
} finally {Pop-Location}
