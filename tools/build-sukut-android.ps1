param(
    [string]$Jdk='C:\Program Files\Microsoft\jdk-17.0.20.8-hotspot',
    [string]$SigningProperties=(Join-Path $env:USERPROFILE 'Documents\Sukut-Private-Signing\Android\release-signing.properties')
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$requiredAsset=Join-Path $projectRoot 'app/src/engine/assets/separator.tflite'
if(-not(Test-Path -LiteralPath $requiredAsset -PathType Leaf)){throw 'Required local engine asset is unavailable. Read AGENTS.md before building.'}
$env:JAVA_HOME=$Jdk
if(-not(Test-Path -LiteralPath $SigningProperties -PathType Leaf)){throw 'Supply -SigningProperties with your private Android release-signing properties file.'}
$previousSigningPath=$env:SUKUT_ANDROID_SIGNING_PROPERTIES
$env:SUKUT_ANDROID_SIGNING_PROPERTIES=(Resolve-Path -LiteralPath $SigningProperties).Path
Push-Location $projectRoot
try {
    & .\gradlew.bat :app:assembleEngine --offline --no-daemon --max-workers=2
    if($LASTEXITCODE -ne 0){throw 'Sukut Android build failed'}
    $sdkRoot=if($env:ANDROID_SDK_ROOT){$env:ANDROID_SDK_ROOT}elseif($env:ANDROID_HOME){$env:ANDROID_HOME}else{Join-Path $env:LOCALAPPDATA 'Android\Sdk'}
    $buildTools=Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'build-tools') -Directory | Where-Object {$_.Name -match '^\d+\.\d+\.\d+$'} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
    if(-not $buildTools){throw 'Android SDK build-tools were not found'}
    $builtApk=Join-Path $projectRoot 'app\build\outputs\apk\engine\app-engine.apk'
    $badging=& (Join-Path $buildTools.FullName 'aapt.exe') dump badging $builtApk
    if($LASTEXITCODE -ne 0 -or $badging -match 'application-debuggable'){throw 'APK must be non-debuggable before release packaging'}
    $certificateReport=& (Join-Path $buildTools.FullName 'apksigner.bat') verify --verbose --print-certs $builtApk
    if($LASTEXITCODE -ne 0 -or $certificateReport -match 'CN=Android Debug'){throw 'APK must have a valid production signature'}
    $configuration=[IO.File]::ReadAllText((Join-Path $projectRoot 'app\build.gradle.kts'))
    $version=[regex]::Match($configuration,'versionName\s*=\s*"([^"]+)"').Groups[1].Value
    if(-not $version){throw 'Version was not found in app/build.gradle.kts'}
    $destination=Join-Path $projectRoot ('Builds\Sukut-'+$version+'-android-arm64.apk')
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $builtApk -Destination $destination -Force
    Write-Output ('APK: '+$destination)
    Write-Output 'Build only. No tests, installation, playback or device settings changes.'
} finally {
    Pop-Location
    $env:SUKUT_ANDROID_SIGNING_PROPERTIES=$previousSigningPath
}
