param([switch]$Installer,[switch]$Store)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$windows=Join-Path $repo 'Windows'
$project=[xml](Get-Content -LiteralPath (Join-Path $windows 'src/sukut-ui/Sukut.csproj') -Raw)
$version=$project.SelectSingleNode('/Project/PropertyGroup/Version').InnerText.Trim()
if($version -notmatch '^\d+\.\d+\.\d+$'){throw 'Release builds expect a plain x.y.z version in Sukut.csproj.'}
# The companion is the current signed Android release (same version and code as Android/app/build.gradle.kts).
$gradle=Get-Content -LiteralPath (Join-Path $repo 'Android/app/build.gradle.kts') -Raw
$androidVersion=[regex]::Match($gradle,'versionName\s*=\s*"([^"]+)"').Groups[1].Value
$androidCode=[regex]::Match($gradle,'versionCode\s*=\s*(\d+)').Groups[1].Value
$companion=Join-Path $repo ('Android/releases/Sukut-'+$androidVersion+'-code'+$androidCode+'-android-arm64.apk')
if(-not(Test-Path -LiteralPath $companion)){throw ('Missing the signed Android '+$androidVersion+'/code'+$androidCode+' companion; run tools/build-sukut-android.ps1 first.')}
& (Join-Path $PSScriptRoot 'build-sukut.ps1')
if($LASTEXITCODE -ne 0){throw 'Windows build failed'}
Push-Location $windows
try {
    & dotnet test tests/Sukut.UnitTests/Sukut.UnitTests.csproj -c Release --filter 'FullyQualifiedName~AdvancedMix|FullyQualifiedName~Phone'
    if($LASTEXITCODE -ne 0){throw 'Targeted Windows tests failed'}
} finally {Pop-Location}
& (Join-Path $PSScriptRoot 'package-sukut.ps1')
if($Installer){& (Join-Path $PSScriptRoot 'build-installer.ps1') -SkipPayloadStage -CompanionApk $companion}
if($Store){& (Join-Path $windows 'store/prepare-store.ps1') -PackageVersion ($version+'.0') -Build}
Write-Output ('Built local Windows '+$version+' candidates with Android '+$androidVersion+'/code'+$androidCode+'. Nothing uploaded or submitted.')
