param([string]$BuildDirectory='D:/Sukut-Dev/build-0.4-xnn',[string[]]$Targets=@('SukutEngine','sukut_efficiency_verify','cleanwave_realtime_verify','cleanwave_dsp_verify'))
# Separate build tree with TFLite + XNNPACK. Reuses the already-fetched
# dependency sources from the working build-0.4 tree read-only, so that cache
# is never reconfigured. XNNPACK and its small helpers are fetched by TFLite's
# pinned CMake declarations.
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$paths=Get-Content -LiteralPath (Join-Path $projectRoot 'local.build.json') -Raw | ConvertFrom-Json
$dependencyRoot=$paths.dependencies
$cache=$paths.build_directory
Push-Location $projectRoot
try{
    $configure=@('--preset','bootstrap-msvc','-B',$BuildDirectory,'-DCLEANWAVE_XNNPACK=ON',
        "-DCLEANWAVE_TENSORFLOW_SOURCE=$dependencyRoot/src/tensorflow-2.16.1","-DCLEANWAVE_KISSFFT_SOURCE=$dependencyRoot/src/kissfft-131.1.0",
        "-DCLEANWAVE_FXDIV_SOURCE=$dependencyRoot/src/FXdiv-63058eff77e11aa15bf531df5dd34395ec3017c8","-DFETCHCONTENT_UPDATES_DISCONNECTED=ON")
    # FP16 would git-clone psimd; Git is not installed, so use the extracted archive.
    if(Test-Path -LiteralPath "$BuildDirectory/psimd-source/CMakeLists.txt"){$configure+="-DPSIMD_SOURCE_DIR=$BuildDirectory/psimd-source"}
    if(Test-Path -LiteralPath "$cache/pthreadpool-source/CMakeLists.txt"){$configure+="-DPTHREADPOOL_SOURCE_DIR=$cache/pthreadpool-source"}
    foreach($entry in @{'ABSEIL-CPP'='abseil-cpp';'CPUINFO'='cpuinfo';'EIGEN'='eigen';'FARMHASH'='farmhash';'FFT2D'='fft2d';'FLATBUFFERS'='flatbuffers';'GEMMLOWP'='gemmlowp';'ML_DTYPES'='ml_dtypes';'RUY'='ruy'}.GetEnumerator()){
        if(Test-Path -LiteralPath "$cache/$($entry.Value)/CMakeLists.txt"){$configure+=('-DFETCHCONTENT_SOURCE_DIR_'+$entry.Key+'='+$cache+'/'+$entry.Value)}
    }
    & $paths.cmake @configure
    if($LASTEXITCODE -ne 0){throw 'XNNPACK configuration failed'}
    & $paths.cmake --build $BuildDirectory --config Release --target @Targets --parallel 6 -- /nodeReuse:false
    if($LASTEXITCODE -ne 0){throw 'XNNPACK native build failed'}
    Write-Output "XNNPACK build: $BuildDirectory/bin/Release"
}finally{Pop-Location}
