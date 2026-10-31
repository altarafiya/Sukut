param([string]$Device='')
$ErrorActionPreference='Stop'
$targetArgs=@()
if($Device){$targetArgs=@('-s',$Device)}
& adb @targetArgs get-state
if($LASTEXITCODE -ne 0){throw 'Connect an authorized USB phone. With multiple transports, pass -Device SERIAL.'}
$scriptPath=Join-Path $PSScriptRoot 'activate-installed-worker.sh'
$activationScript=[IO.File]::ReadAllText($scriptPath).Replace("`r",'')
$activationScript | & adb @targetArgs shell -T sh -s
if($LASTEXITCODE -ne 0){throw 'The installed Sukut worker did not return a matching READY handshake.'}
