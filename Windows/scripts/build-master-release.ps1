param([switch]$Installer,[switch]$Store)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$windows=Join-Path $repo 'Windows'
$project=[xml](Get-Content -LiteralPath (Join-Path $windows 'src/sukut-ui/Sukut.csproj') -Raw)
$version=$project.SelectSingleNode('/Project/PropertyGroup/Version').InnerText.Trim()
if($version -ne '0.5.2'){throw 'This release kit expects source version 0.5.2.'}
$companion=Join-Path $repo 'Builds/Sukut-0.5.2-code63-android-arm64.apk'
if(-not(Test-Path -LiteralPath $companion)){throw 'Missing the signed Android 0.5.2/code63 companion from the kit.'}
& (Join-Path $PSScriptRoot 'build-sukut.ps1')
if($LASTEXITCODE -ne 0){throw 'Windows build failed'}
Push-Location $windows
try {
    & dotnet test tests/Sukut.UnitTests/Sukut.UnitTests.csproj -c Release --filter 'FullyQualifiedName~AdvancedMix|FullyQualifiedName~Phone'
    if($LASTEXITCODE -ne 0){throw 'Targeted Windows tests failed'}
} finally {Pop-Location}
& (Join-Path $PSScriptRoot 'package-sukut.ps1')
if($Installer){& (Join-Path $PSScriptRoot 'build-installer.ps1') -SkipPayloadStage -CompanionApk $companion}
if($Store){& (Join-Path $windows 'store/prepare-store.ps1') -PackageVersion '0.5.2.0' -Build}
Write-Output 'Built local Windows 0.5.2 candidates. Nothing uploaded or submitted.'
