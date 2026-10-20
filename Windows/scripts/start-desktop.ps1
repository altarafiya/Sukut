param([string]$SettingsFile)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$exe=Join-Path $projectRoot 'build\bootstrap\bin\Release\CleanWave.exe'
if(-not(Test-Path -LiteralPath $exe -PathType Leaf)){throw 'Build the desktop app with scripts\build-step8.ps1 first'}
if($SettingsFile){
    $settingsPath=if([IO.Path]::IsPathRooted($SettingsFile)){$SettingsFile}else{Join-Path $projectRoot $SettingsFile}
    if($settingsPath.Contains('"')){throw 'Unexpected quote in settings path'}
    Start-Process -FilePath $exe -ArgumentList @('--settings',('"'+$settingsPath+'"'))
}else{Start-Process -FilePath $exe}
