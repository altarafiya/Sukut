$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$cacheDir = Join-Path $projectRoot 'deps\cache'
$sourceDir = Join-Path $projectRoot 'deps\src'

New-Item -ItemType Directory -Force -Path $cacheDir, $sourceDir | Out-Null

$dependencies = @(
    @{
        Name = 'TensorFlow 2.16.1'
        Url = 'https://github.com/tensorflow/tensorflow/archive/refs/tags/v2.16.1.zip'
        Archive = 'tensorflow-v2.16.1.zip'
        Sha256 = '54C976F828182F85E10E03840DD3B0504109F57760A498075574E35E9AA983FE'
        Marker = 'tensorflow-2.16.1\tensorflow\lite\CMakeLists.txt'
    },
    @{
        Name = 'KissFFT 131.1.0'
        Url = 'https://github.com/mborgerding/kissfft/archive/refs/tags/131.1.0.zip'
        Archive = 'kissfft-131.1.0.zip'
        Sha256 = '9C2E19CC34ED910DCB509FD8AB561A523B923B6578703ACE8C8F37F5A286BB25'
        Marker = 'kissfft-131.1.0\CMakeLists.txt'
    },
    @{
        Name = 'FXdiv 63058eff'
        Url = 'https://codeload.github.com/Maratyszcza/FXdiv/zip/63058eff77e11aa15bf531df5dd34395ec3017c8'
        Archive = 'fxdiv-63058eff77e11aa15bf531df5dd34395ec3017c8.zip'
        Sha256 = '3D7B0E9C4C658A84376A1086126BE02F9B7F753CAA95E009D9AC38D11DA444DB'
        Marker = 'FXdiv-63058eff77e11aa15bf531df5dd34395ec3017c8\CMakeLists.txt'
    }
)

function Get-VerifiedArchive([hashtable]$Dependency) {
    $archivePath = Join-Path $cacheDir $Dependency.Archive
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        & curl.exe -L --fail --retry 3 --retry-delay 2 --output $archivePath $Dependency.Url
        if ($LASTEXITCODE -ne 0) {
            throw "Download failed for $($Dependency.Name) with exit code $LASTEXITCODE"
        }
    }

    $actual = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    if ($actual -ne $Dependency.Sha256) {
        throw "SHA-256 mismatch for $($Dependency.Name): expected $($Dependency.Sha256), got $actual"
    }
    return $archivePath
}

Add-Type -AssemblyName System.IO.Compression.FileSystem

foreach ($dependency in $dependencies) {
    $archivePath = Get-VerifiedArchive $dependency
    $markerPath = Join-Path $sourceDir $dependency.Marker
    if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
        [IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $sourceDir)
    }
    if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
        throw "Extraction did not produce the expected marker for $($dependency.Name): $markerPath"
    }
    Write-Host "$($dependency.Name): verified"
}

Write-Host 'Direct dependency archives and extracted source markers are ready.'

