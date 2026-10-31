param([ValidateSet('Release','Debug')][string]$Configuration='Release',[string]$OutputDirectory='',[switch]$NativeOnly)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$requiredAsset=Join-Path $projectRoot 'assets/engine/separator.tflite'
if(-not(Test-Path -LiteralPath $requiredAsset -PathType Leaf)){throw 'Required local engine asset is unavailable. Read AGENTS.md before building.'}
$uiProject=[xml](Get-Content -LiteralPath (Join-Path $projectRoot 'src/sukut-ui/Sukut.csproj') -Raw)
if([string]::IsNullOrWhiteSpace($OutputDirectory)){$OutputDirectory='build/sukut-'+$uiProject.SelectSingleNode('/Project/PropertyGroup/Version').InnerText.Trim()}
$paths=Get-Content -LiteralPath (Join-Path $projectRoot 'local.build.json') -Raw | ConvertFrom-Json
$dependencyRoot=$paths.dependencies
$dependencySources=$paths.dependency_sources
$nativeBuild=if($paths.build_directory){$paths.build_directory}else{Join-Path $projectRoot 'build/bootstrap'}
$env:NUGET_PACKAGES=Join-Path $dependencyRoot 'nuget-sukut'
$env:DOTNET_CLI_HOME=Join-Path $projectRoot 'build\dotnet-home'
$env:DOTNET_CLI_TELEMETRY_OPTOUT='1'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE='false'
$env:DOTNET_NOLOGO='1'
Push-Location $projectRoot
try{
    $uiOutput=([IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))).Replace('\','/')
    $configure=@('--preset','bootstrap-msvc','-B',$nativeBuild,"-DSUKUT_UI_OUTPUT_DIRECTORY=$uiOutput", "-DCLEANWAVE_TENSORFLOW_SOURCE=$dependencyRoot/src/tensorflow-2.16.1", "-DCLEANWAVE_KISSFFT_SOURCE=$dependencyRoot/src/kissfft-131.1.0", "-DCLEANWAVE_FXDIV_SOURCE=$dependencyRoot/src/FXdiv-63058eff77e11aa15bf531df5dd34395ec3017c8", "-DCONFU_DEPENDENCIES_SOURCE_DIR=$dependencySources", '-DFETCHCONTENT_UPDATES_DISCONNECTED=ON')
    if(Test-Path -LiteralPath "$dependencySources/pthreadpool-source/CMakeLists.txt"){$configure+="-DPTHREADPOOL_SOURCE_DIR=$dependencySources/pthreadpool-source"}
    foreach($entry in @{'ABSEIL-CPP'='abseil-cpp';'CPUINFO'='cpuinfo';'EIGEN'='eigen';'FARMHASH'='farmhash';'FFT2D'='fft2d';'FLATBUFFERS'='flatbuffers';'GEMMLOWP'='gemmlowp';'ML_DTYPES'='ml_dtypes';'RUY'='ruy'}.GetEnumerator()){
        if(Test-Path -LiteralPath "$dependencySources/$($entry.Value)/CMakeLists.txt"){$configure+=('-DFETCHCONTENT_SOURCE_DIR_'+$entry.Key+'='+$dependencySources+'/'+$entry.Value)}
    }
    & $paths.cmake @configure
    if($LASTEXITCODE -ne 0){throw 'Native configuration failed'}
    & $paths.cmake --build $nativeBuild --config $Configuration --target SukutEngine SukutVolumeGuard SukutHost SukutDesktopLauncher SukutPortableLauncher --parallel 4 -- /nodeReuse:false
    if($LASTEXITCODE -ne 0){throw 'Native audio bridge build failed'}
    if($NativeOnly){return}
    $repository=Split-Path -Parent $projectRoot
    $gradle=Get-Content -LiteralPath (Join-Path $repository 'app/build.gradle.kts') -Raw
    $androidVersion=[regex]::Match($gradle,'versionName\s*=\s*"(\d+\.\d+(?:\.\d+)?(?:-beta\.\d+)?)"').Groups[1].Value
    $apk=Join-Path $repository ('Builds/Sukut-'+$androidVersion+'-android-arm64.apk')
    if(-not (Test-Path -LiteralPath $apk)){throw 'Build Android first'}
    $phoneAssets=Join-Path $projectRoot 'src/sukut-ui/Assets/Phone'
    New-Item -ItemType Directory -Path $phoneAssets -Force | Out-Null
    $workerScript=Join-Path $repository 'app/src/main/assets/activate-worker.sh'
    Copy-Item -LiteralPath $workerScript -Destination (Join-Path $phoneAssets 'activate-worker.sh') -Force
    (Get-FileHash -LiteralPath $workerScript -Algorithm SHA256).Hash | Set-Content -LiteralPath (Join-Path $phoneAssets 'activate-worker.sh.sha256') -Encoding ascii
    Copy-Item -LiteralPath $apk -Destination (Join-Path $phoneAssets 'Sukut.apk') -Force
    (Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash | Set-Content -LiteralPath (Join-Path $phoneAssets 'Sukut.apk.sha256') -Encoding ascii
    $androidCode=[regex]::Match($gradle,'versionCode\s*=\s*(\d+)').Groups[1].Value
    if(-not $androidCode){throw 'Android version code missing'}
    ($androidVersion+"`n"+$androidCode) | Set-Content -LiteralPath (Join-Path $phoneAssets 'Sukut.apk.version') -Encoding ascii
    & dotnet publish src/sukut-ui/Sukut.csproj -c $Configuration -p:Platform=x64 ("-p:SukutDependencyRoot="+$dependencyRoot) ("-p:SukutNativeDirectory=$nativeBuild/bin/$Configuration") -o $uiOutput
    if($LASTEXITCODE -ne 0){throw 'WinUI publish failed'}
    Write-Output ('Window: '+(Join-Path $uiOutput 'SukutWindow.exe'))
}finally{Pop-Location}
