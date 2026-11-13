param(
    [string]$KitDirectory,
    [string]$PackageVersion,
    [string]$NativeDirectory,
    [string]$PortableDirectory,
    [switch]$Build
)
$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $windowsRoot
$uiProject = [xml](Get-Content -LiteralPath (Join-Path $windowsRoot 'src\sukut-ui\Sukut.csproj') -Raw)
$appVersion = $uiProject.SelectSingleNode('/Project/PropertyGroup/Version').InnerText.Trim()
$buildPaths = Get-Content -LiteralPath (Join-Path $windowsRoot 'local.build.json') -Raw | ConvertFrom-Json
if (-not $KitDirectory) { $KitDirectory = Join-Path $repositoryRoot ('Store\Microsoft-Store-'+$appVersion) }
New-Item -ItemType Directory -Force -Path $KitDirectory,(Join-Path $KitDirectory 'assets'),(Join-Path $KitDirectory 'documents') | Out-Null
$identityFile = Join-Path $KitDirectory 'identity.json'
if (-not (Test-Path -LiteralPath $identityFile)) {
    $previousIdentity = Get-Content -LiteralPath (Join-Path $repositoryRoot 'Store\Microsoft-Store-2026-10-20\identity.json') -Raw | ConvertFrom-Json
    $numericVersion = [version]([regex]::Match($appVersion,'^\d+\.\d+(?:\.\d+)?').Value)
    $previousIdentity.packageVersion = ('{0}.{1}.{2}.0' -f $numericVersion.Major,$numericVersion.Minor,[Math]::Max(0,$numericVersion.Build))
    $previousIdentity | ConvertTo-Json | Set-Content -LiteralPath $identityFile -Encoding utf8
}
$identity = Get-Content -LiteralPath (Join-Path $KitDirectory 'identity.json') -Raw | ConvertFrom-Json
if ($PackageVersion) {
    if ($PackageVersion -notmatch '^\d+\.\d+\.\d+\.0$') { throw 'Store package version must have four numeric parts ending in .0.' }
    $identity.packageVersion = $PackageVersion
    $identity | ConvertTo-Json | Set-Content -LiteralPath $identityFile -Encoding utf8
}
foreach ($field in @('packageName','publisher','publisherDisplayName','displayName','packageVersion')) {
    if ([string]::IsNullOrWhiteSpace($identity.$field)) { throw ('Missing Partner Center identity field: ' + $field) }
}
if ($identity.packageName -ne 'Altarafiyah.Sukut' -or $identity.publisher -ne 'CN=114ABF7D-BD0E-41B0-8FC9-13FA1414FF9A') {
    throw 'The identity differs from the owner-supplied Partner Center screenshot; review before building.'
}
$inputRoot = Join-Path $windowsRoot ('build\store-input-'+$appVersion)
New-Item -ItemType Directory -Force -Path $inputRoot,(Join-Path $inputRoot 'logos') | Out-Null
# Standard Windows resource generation from the existing approved app icon.
# This preserves the design; no new or generative icon replaces the brand.
Add-Type -AssemblyName System.Drawing
$sourceIcon = [Drawing.Image]::FromFile((Join-Path $repositoryRoot 'assets\branding\sukut-app-icon.png'))
try {
    foreach ($resource in @(@('Square44x44Logo.png',44),@('Square150x150Logo.png',150),@('StoreLogo.png',50),@('AppTile-300.png',300),@('AppIcon-512.png',512))) {
        $bitmap = [Drawing.Bitmap]::new([int]$resource[1],[int]$resource[1],[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.DrawImage($sourceIcon,0,0,[int]$resource[1],[int]$resource[1])
            $bitmap.Save((Join-Path (Join-Path $inputRoot 'logos') $resource[0]),[Drawing.Imaging.ImageFormat]::Png)
        } finally { $graphics.Dispose(); $bitmap.Dispose() }
    }
} finally { $sourceIcon.Dispose() }
Copy-Item -LiteralPath (Join-Path $inputRoot 'logos\AppTile-300.png') -Destination (Join-Path $KitDirectory 'assets\AppTile-300.png') -Force
Copy-Item -LiteralPath (Join-Path $inputRoot 'logos\AppIcon-512.png') -Destination (Join-Path $KitDirectory 'assets\AppIcon-512.png') -Force
$template = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Package.appxmanifest.template') -Raw
foreach ($entry in @{'PACKAGE_NAME'=$identity.packageName;'PUBLISHER'=$identity.publisher;'PUBLISHER_DISPLAY_NAME'=$identity.publisherDisplayName;'DISPLAY_NAME'=$identity.displayName;'VERSION'=$identity.packageVersion}.GetEnumerator()) {
    $template = $template.Replace('{{'+$entry.Key+'}}',[Security.SecurityElement]::Escape([string]$entry.Value))
}
[IO.File]::WriteAllText((Join-Path $inputRoot 'Package.appxmanifest'),$template,[Text.UTF8Encoding]::new($false))
[ordered]@{name='Sukut';version=$appVersion;platform='win-x64';channel=if($appVersion.Contains('-beta.')){'store-beta-candidate'}else{'store-candidate'};application='Sukut.exe';first_run_setup=$true;external_driver='https://vb-audio.com/Cable/';driver_bundled=$false;store_id=$identity.storeId} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $inputRoot 'sukut-package.json') -Encoding UTF8
$priorPayload = if ($PortableDirectory) { $PortableDirectory } else { Join-Path $windowsRoot ('dist\Sukut-'+$appVersion+'-win-x64') }
if (-not (Test-Path -LiteralPath (Join-Path $priorPayload 'licenses'))) { throw 'Stage the current complete Windows portable payload first.' }
# The engine uses the dynamic VC143 runtime. Ship its exact portable dependencies
# rather than relying on an unrelated application having installed them globally.
$nativeRuntime = Join-Path $inputRoot 'native-runtime'
New-Item -ItemType Directory -Force -Path $nativeRuntime | Out-Null
foreach ($runtime in @('msvcp140.dll','msvcp140_atomic_wait.dll','vcruntime140.dll','vcruntime140_1.dll')) {
    Copy-Item -LiteralPath (Join-Path (Join-Path $priorPayload 'app') $runtime) -Destination (Join-Path $nativeRuntime $runtime) -Force
}
Copy-Item -LiteralPath (Join-Path $priorPayload 'licenses') -Destination $inputRoot -Recurse -Force
$notice = Get-Content -LiteralPath (Join-Path $priorPayload 'THIRD_PARTY_NOTICES.txt') -Raw
$notice = $notice.Replace('The setup EXE includes the unchanged original VB-CABLE driver package from','The separate desktop setup EXE includes the original VB-CABLE driver package from')
$notice += "`r`nMicrosoft Store candidate: this MSIX does not contain or install VB-CABLE. The separate driver dependency remains and requires Microsoft eligibility review. Original open-source component notices still apply.`r`n"
$notice | Set-Content -LiteralPath (Join-Path $inputRoot 'THIRD_PARTY_NOTICES.txt') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $inputRoot 'Package.appxmanifest') -Destination (Join-Path $KitDirectory 'documents\Package.appxmanifest') -Force
if ($Build) {
    $env:DOTNET_CLI_TELEMETRY_OPTOUT='1'
    $env:DOTNET_NOLOGO='1'
    $env:NUGET_PACKAGES=Join-Path $buildPaths.dependencies 'nuget-sukut'
    $packageOutput = Join-Path $KitDirectory 'packages'
    New-Item -ItemType Directory -Force -Path $packageOutput | Out-Null
    if (-not $NativeDirectory) { $NativeDirectory = Join-Path $buildPaths.build_directory 'bin\Release' }
    foreach ($nativeFile in @('Sukut.exe','SukutEngine.dll','SukutVolumeGuard.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $NativeDirectory $nativeFile))) { throw ('Missing native Store input: '+$nativeFile) }
    }
    & dotnet publish (Join-Path $windowsRoot 'src\sukut-ui\Sukut.csproj') -c Release -p:Platform=x64 ("-p:IntermediateOutputPath=obj/store-"+$appVersion+'-final/') '-p:SukutStoreBuild=true' ("-p:SukutStoreInput="+$inputRoot) ("-p:SukutDependencyRoot="+$buildPaths.dependencies) ("-p:SukutNativeDirectory="+$NativeDirectory) '-p:WindowsPackageType=MSIX' '-p:EnableMsixTooling=true' '-p:AppxPackageSigningEnabled=false' '-p:UapAppxPackageBuildMode=StoreUpload' ("-p:AppxPackageDir="+$packageOutput+'\') -o (Join-Path $windowsRoot ('build\sukut-'+$appVersion+'-msix'))
    if ($LASTEXITCODE -ne 0) { throw 'Store package build failed; retain the log and fix the cause before submission.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $upload = [IO.Compression.ZipFile]::OpenRead((Join-Path $packageOutput ('Sukut_'+$identity.packageVersion+'_x64.msixupload')))
    try {
        $msixEntries = @($upload.Entries | Where-Object { $_.FullName.EndsWith('.msix',[StringComparison]::OrdinalIgnoreCase) })
        if ($msixEntries.Count -ne 1) { throw 'Expected one MSIX in the Store upload archive' }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($msixEntries[0],(Join-Path $packageOutput ('Sukut-'+$identity.packageVersion+'-x64-Store.msix')),$true)
    } finally { $upload.Dispose() }
    # Validate the resource file inside the upload, not just the ordinary
    # publish output: the SDK maintains a separate incremental Upload PRI.
    $resourcePackage = [IO.Compression.ZipFile]::OpenRead((Join-Path $packageOutput ('Sukut-'+$identity.packageVersion+'-x64-Store.msix')))
    $resourceHasher = [Security.Cryptography.SHA256]::Create()
    try {
        $packagedManifestEntry = $resourcePackage.GetEntry('AppxManifest.xml')
        if (-not $packagedManifestEntry) { throw 'Store upload is missing its manifest.' }
        $manifestReader = [IO.StreamReader]::new($packagedManifestEntry.Open())
        try { [xml]$packagedManifest = $manifestReader.ReadToEnd() } finally { $manifestReader.Dispose() }
        if ($packagedManifest.Package.Identity.Name -ne $identity.packageName -or
            $packagedManifest.Package.Identity.Publisher -ne $identity.publisher -or
            $packagedManifest.Package.Identity.Version -ne $identity.packageVersion -or
            $packagedManifest.Package.Identity.ProcessorArchitecture -ne 'x64') {
            throw 'Store upload identity/version differs from the requested package.'
        }
        $resourceEntry = $resourcePackage.GetEntry('resources.pri')
        if (-not $resourceEntry) { throw 'Store upload is missing its resource index.' }
        $resourceStream = $resourceEntry.Open()
        try { $uploadedHash = [BitConverter]::ToString($resourceHasher.ComputeHash($resourceStream)) } finally { $resourceStream.Dispose() }
        $currentResources = [IO.File]::OpenRead((Join-Path (Join-Path $windowsRoot ('build\sukut-'+$appVersion+'-msix')) 'resources.pri'))
        try { $currentHash = [BitConverter]::ToString($resourceHasher.ComputeHash($currentResources)) } finally { $currentResources.Dispose() }
        if ($uploadedHash -ne $currentHash) { throw 'Store upload has stale XAML resources. Do not submit this candidate.' }
    } finally { $resourceHasher.Dispose(); $resourcePackage.Dispose() }
}
Write-Output ('Store inputs: '+$inputRoot)
Write-Output 'No Store upload, publication, app installation, driver installation or security setting change was performed.'
