param([switch]$SkipPayloadStage)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskRepo=Split-Path -Parent $taskRoot
$paths=Get-Content -LiteralPath (Join-Path $taskRoot 'local.build.json') -Raw | ConvertFrom-Json
$taskProject=[xml](Get-Content -LiteralPath (Join-Path $taskRoot 'src/sukut-ui/Sukut.csproj') -Raw)
$taskVersion=$taskProject.SelectSingleNode('/Project/PropertyGroup/Version').InnerText.Trim()
$taskGradle=Get-Content -LiteralPath (Join-Path $taskRepo 'app/build.gradle.kts') -Raw
$taskAndroidVersion=[regex]::Match($taskGradle,'versionName\s*=\s*"([0-9.]+)"').Groups[1].Value
$taskApk=Join-Path $taskRepo ('Builds/Sukut-'+$taskAndroidVersion+'-android-arm64.apk')
$taskBundle=Join-Path $taskRoot ('build/sukut-'+$taskVersion+'/Assets/Phone/Sukut.apk')
$taskCurrentApk=Join-Path $taskRepo 'app/build/outputs/apk/engine/app-engine.apk'
$taskApkHash=(Get-FileHash -LiteralPath $taskApk -Algorithm SHA256).Hash
if((Get-FileHash -LiteralPath $taskCurrentApk -Algorithm SHA256).Hash -ne $taskApkHash){throw 'Release APK differs from current Android output'}
if((Get-FileHash -LiteralPath $taskBundle -Algorithm SHA256).Hash -ne $taskApkHash){throw 'Bundled Android APK is out of date'}
if((Get-Content -LiteralPath ($taskBundle+'.sha256') -Raw).Trim() -ne $taskApkHash){throw 'Bundled APK checksum mismatch'}
$taskAndroidCode=[regex]::Match($taskGradle,'versionCode\s*=\s*(\d+)').Groups[1].Value
$taskBundleVersion=@(Get-Content -LiteralPath ($taskBundle+'.version') | Where-Object {$_})
if($taskBundleVersion.Count -ne 2 -or $taskBundleVersion[0] -ne $taskAndroidVersion -or $taskBundleVersion[1] -ne $taskAndroidCode){throw 'Bundled APK version metadata is out of date'}
$taskArchive=Join-Path $paths.dependencies 'installer/VBCABLE_Driver_Pack45.zip'
if((Get-FileHash -LiteralPath $taskArchive -Algorithm SHA256).Hash -ne 'B950E39F01AF1D04EA623C8F6D8EB9B6EA5C477C637295FABF20631C85116BFB'){throw 'Unexpected VB-CABLE archive'}
$taskDriver=Join-Path $paths.dependencies 'installer/vbcable45/VBCABLE_Setup_x64.exe'
$taskSignature=Get-AuthenticodeSignature -LiteralPath $taskDriver
if($taskSignature.Status -ne 'Valid' -or $taskSignature.SignerCertificate.Subject -notlike '*BUREL VINCENT*'){throw 'Invalid VB-CABLE signature'}
if(-not $SkipPayloadStage){& (Join-Path $PSScriptRoot 'package-sukut.ps1') -NoArchive}
$taskCompiler=Join-Path $paths.dependencies 'inno-7.1.0/ISCC.exe'
& $taskCompiler ("/DAppVersion="+$taskVersion) ("/DAndroidVersion="+$taskAndroidVersion) ("/DDependencyRoot="+$paths.dependencies) (Join-Path $taskRoot 'installer/Sukut.iss')
if($LASTEXITCODE -ne 0){throw 'Installer compiler failed'}
$taskOutput=Join-Path $taskRoot ('dist/installer-final/Sukut-'+$taskVersion+'-Setup.exe')
$record=[ordered]@{version=$taskVersion;android_version=$taskAndroidVersion;android_sha256=$taskApkHash;installer=$taskOutput;bytes=(Get-Item -LiteralPath $taskOutput).Length;sha256=(Get-FileHash -LiteralPath $taskOutput -Algorithm SHA256).Hash;driver_signature=$taskSignature.Status.ToString();built_date=(Get-Date -Format 'yyyy-MM-dd')}
$evidence=Join-Path $taskRepo ('docs/release-'+$taskVersion+'-'+$taskAndroidVersion)
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'installer-built.json') -Encoding utf8
Write-Output ('Installer: '+$taskOutput)
