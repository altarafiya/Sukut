$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
Start-Process -FilePath (Join-Path $projectRoot 'build\sukut-0.2.27\Sukut.exe') -WorkingDirectory $projectRoot
