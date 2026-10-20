param([string]$AppDirectory='build/sukut-0.2.33', [switch]$NoArchive)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$paths=Get-Content -LiteralPath (Join-Path $projectRoot 'local.build.json') -Raw | ConvertFrom-Json
$taskProject=[xml](Get-Content -LiteralPath (Join-Path $projectRoot 'src/sukut-ui/Sukut.csproj') -Raw)
$taskVersion=$taskProject.SelectSingleNode('/Project/PropertyGroup/Version').InnerText.Trim()
$taskGradle=Get-Content -LiteralPath (Join-Path (Split-Path -Parent $projectRoot) 'app/build.gradle.kts') -Raw
$taskAndroidVersion=[regex]::Match($taskGradle,'versionName\s*=\s*"([0-9.]+)"').Groups[1].Value
$packageName='Sukut-'+$taskVersion+'-win-x64'
$packageRoot=Join-Path (Join-Path $projectRoot 'dist') $packageName
$appSource=Join-Path $projectRoot $AppDirectory
foreach($required in @('Sukut.exe','Sukut.pri','SukutEngine.dll','Assets\Sukut.ico','Assets\Tray-on-dark.ico','Assets\Tray-off-dark.ico','Assets\Tray-on-light.ico','Assets\Tray-off-light.ico','SukutVolumeGuard.exe','Assets\Phone\Sukut.apk','Assets\Phone\Sukut.apk.sha256','Assets\Phone\Sukut.apk.version')){
    if(-not (Test-Path -LiteralPath (Join-Path $appSource $required))){throw ('Incomplete app output: '+$required)}
}
$packageApp=Join-Path $packageRoot 'app'
$licenseRoot=Join-Path $packageRoot 'licenses'
if(Test-Path -LiteralPath (Join-Path $packageRoot 'runtime')){throw 'This extracted copy has user settings; use a fresh package directory.'}
$activePackage=@(Get-Process | ForEach-Object {try{if($_.Path -and $_.Path.StartsWith($packageRoot+'\',[StringComparison]::OrdinalIgnoreCase)){$_.Id}}catch{}})
if($activePackage.Count){throw 'The extracted package is running; leave it intact and use another package directory.'}
New-Item -ItemType Directory -Force -Path $packageApp,$licenseRoot,(Join-Path $packageRoot 'assets\engine') | Out-Null
# Copy the complete self-contained output. Developer symbols and local
# generation provenance are not part of the recipient's runtime bundle.
Get-ChildItem -LiteralPath $appSource -Recurse -File | ForEach-Object {
    $relative=$_.FullName.Substring($appSource.Length).TrimStart('\','/')
    if($_.Extension -eq '.pdb' -or $_.Extension -eq '.log' -or $_.Name -eq 'Calligraphy-PROVENANCE.md' -or $_.Name -eq 'PROVENANCE.md'){return}
    $destination=Join-Path $packageApp $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -Force -LiteralPath $_.FullName -Destination $destination
}
Copy-Item -Force -LiteralPath (Join-Path $projectRoot 'build\bootstrap\bin\Release\SukutPortable.exe') -Destination (Join-Path $packageRoot 'Sukut.exe')
foreach($asset in @('separator.tflite','window_576_q15.bin','window_960_q15.bin')){
    Copy-Item -Force -LiteralPath (Join-Path (Join-Path $projectRoot 'assets\engine') $asset) -Destination (Join-Path (Join-Path $packageRoot 'assets\engine') $asset)
}
# App-local Microsoft CRT deployment for the native engine.
$crtSource='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT'
Get-ChildItem -LiteralPath $crtSource -Filter '*.dll' -File | ForEach-Object {Copy-Item -Force -LiteralPath $_.FullName -Destination $packageApp}
foreach($guide in @('GETTING_STARTED.txt','GETTING_STARTED.html','RELEASE_NOTES.txt')){Copy-Item -Force -LiteralPath (Join-Path (Join-Path $projectRoot 'packaging') $guide) -Destination (Join-Path $packageRoot $guide)}
Copy-Item -Force -LiteralPath (Join-Path $projectRoot 'CHANGELOG.md') -Destination (Join-Path $packageRoot 'CHANGELOG.md')
$licenseDirectories=[ordered]@{
    'TensorFlow-Lite'=(Join-Path $paths.dependencies 'src\tensorflow-2.16.1')
    'KissFFT'=(Join-Path $paths.dependencies 'src\kissfft-131.1.0')
    'FXdiv'=(Join-Path $paths.dependencies 'src\FXdiv-63058eff77e11aa15bf531df5dd34395ec3017c8')
    'Abseil'=(Join-Path $paths.dependency_sources 'abseil-cpp')
    'RUY'=(Join-Path $paths.dependency_sources 'ruy')
    'Eigen'=(Join-Path $paths.dependency_sources 'eigen')
    'Gemmlowp'=(Join-Path $paths.dependency_sources 'gemmlowp')
    'Pthreadpool'=(Join-Path $paths.dependency_sources 'pthreadpool-source')
    'NET-Runtime'=(Join-Path $paths.dependencies 'nuget-sukut\microsoft.netcore.app.runtime.win-x64\8.0.31')
    'WindowsAppSDK'=(Join-Path $paths.dependencies 'nuget-sukut\microsoft.windowsappsdk\2.5.1')
    'WindowsAppSDK-WinUI'=(Join-Path $paths.dependencies 'nuget-sukut\microsoft.windowsappsdk.winui\2.3.9')
    'WindowsAppSDK-Runtime'=(Join-Path $paths.dependencies 'nuget-sukut\microsoft.windowsappsdk.runtime\2.5.1')
    'CommunityToolkit-Common'=(Join-Path $paths.dependencies 'nuget-sukut\communitytoolkit.common\8.2.1')
    'CommunityToolkit-SettingsControls'=(Join-Path $paths.dependencies 'nuget-sukut\communitytoolkit.winui.controls.settingscontrols\8.2.251219')
    'CommunityToolkit-Extensions'=(Join-Path $paths.dependencies 'nuget-sukut\communitytoolkit.winui.extensions\8.2.251219')
    'CommunityToolkit-Helpers'=(Join-Path $paths.dependencies 'nuget-sukut\communitytoolkit.winui.helpers\8.2.251219')
    'CommunityToolkit-Triggers'=(Join-Path $paths.dependencies 'nuget-sukut\communitytoolkit.winui.triggers\8.2.251219')
}
foreach($entry in $licenseDirectories.GetEnumerator()){
    $destination=Join-Path $licenseRoot $entry.Key
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Get-ChildItem -LiteralPath $entry.Value -File | Where-Object {$_.Name -match '(?i)license|copying|notice'} | ForEach-Object {Copy-Item -Force -LiteralPath $_.FullName -Destination $destination}
}
Copy-Item -Force -LiteralPath (Join-Path $paths.dependencies 'src\tensorflow-2.16.1\tensorflow\tools\pip_package\THIRD_PARTY_NOTICES.txt') -Destination (Join-Path $licenseRoot 'TensorFlow-Lite\THIRD_PARTY_NOTICES.txt')
$notices=@'
Sukut — developed by الطرفية

This package includes the self-contained WinUI/.NET application, a native
CPU audio engine, model/window assets, original Microsoft runtime files,
and the matching Android companion. Audio processing is performed locally.
Model assets are third-party material, not claimed as original Sukut models.
Redistribution of those assets is subject to separate authorization; the
original provenance/permission records are maintained separately.

TensorFlow Lite 2.16.1 — Apache 2.0.
https://github.com/tensorflow/tensorflow
KissFFT, FXdiv, Abseil, RUY, Eigen, Gemmlowp and Pthreadpool — original
component license/copyright texts are retained under licenses/.
Microsoft Windows App SDK 2.5.1, .NET 8.0.31 and VC143 runtime files —
original runtime components and applicable notices are retained.
CommunityToolkit WinUI — MIT.
IBM Plex Sans Arabic, Readex Pro and Handjet — SIL Open Font License 1.1.
Font license files are retained with the application's assets.

Android companion: Sukut $taskAndroidVersion, package com.osama.sukut. Its TensorFlow Lite
runtime contains one local CPU display-label adjustment; executable code
sections, model weights and numerical operations remain unchanged.
The original Apache 2.0 license is retained in the source project's app/vendor.
Shizuku and Google Platform Tools are downloaded separately from official
sources, checked against pinned hashes; original download notices are kept.

The setup EXE includes the unchanged original VB-CABLE driver package from
VB-Audio. Installation occurs only when the driver is absent, with an
administrator prompt and reboot guidance. VB-CABLE is donationware:
https://vb-audio.com/Cable/
https://shop.vb-audio.com/en/win-apps/11-vb-cable.html
Its original terms and signature are preserved. Removing Sukut leaves the
shared driver installed. No installer test has installed or changed it here.

No personal settings, endpoint IDs, routing journals, audio recordings,
developer symbols or old application data are included in this package.
'@
$notices=$notices.Replace('$taskAndroidVersion',$taskAndroidVersion)
Set-Content -LiteralPath (Join-Path $packageRoot 'THIRD_PARTY_NOTICES.txt') -Value $notices -Encoding UTF8
$metadata=[ordered]@{name='Sukut';version=$taskVersion;revision='route-identity-and-recorder-off';channel='release-candidate';platform='win-x64';packaged_at=(Get-Date -Format o);entrypoint='Sukut.exe';application='app/Sukut.exe';first_run_setup=$true;external_driver='https://vb-audio.com/Cable/';validation=('See docs/release-'+$taskVersion+'-'+$taskAndroidVersion+' in the source workspace')}
$metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $packageRoot 'sukut-package.json') -Encoding UTF8
# Only the generated package directory is archived. Existing runtime settings,
# docs/route.json, project sources, build evidence and caches are never inputs.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipPath=$null
if(-not $NoArchive){
    $zipPath=Join-Path (Join-Path $projectRoot 'dist') ($packageName+'.zip')
    $stagedZip=$zipPath+'.new'
    [System.IO.Compression.ZipFile]::CreateFromDirectory($packageRoot,$stagedZip,[System.IO.Compression.CompressionLevel]::Optimal,$true)
    [System.IO.File]::Move($stagedZip,$zipPath,$true)
}
$record=[ordered]@{package_directory=$packageRoot;zip=$zipPath;created_at=(Get-Date -Format o);executed_or_checked=$false}
$taskRecord=Join-Path $projectRoot ('docs\redesign\release-'+$taskVersion)
New-Item -ItemType Directory -Force -Path $taskRecord | Out-Null
$record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskRecord 'package-created.json') -Encoding UTF8
Write-Output ('Package: '+$zipPath)
Write-Output ('Extracted folder: '+$packageRoot)
