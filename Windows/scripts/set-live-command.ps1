param(
    [Parameter(Mandatory)][ValidateSet('Off','Default','Custom','Restart','Stop')][string]$Mode,
    [switch]$SetMix,
    [ValidateRange(-100,10)][int]$VoiceDb = 0,
    [ValidateRange(-100,10)][int]$MusicDb = -10,
    [ValidateRange(-100,10)][int]$OtherDb = -20,
    [ValidateRange(0,100)][int]$Reduction = 100,
    [ValidateRange(0,6)][int]$BoostDb = 0
)
$ErrorActionPreference = 'Stop'
if ($SetMix -and $Mode -ne 'Custom') { throw 'SetMix requires Mode Custom' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$commands = Join-Path $projectRoot 'runtime\live-commands.txt'
if (-not (Test-Path -LiteralPath $commands -PathType Leaf)) { throw 'Start the live controller first' }
$lastId = 0L
foreach ($line in Get-Content -LiteralPath $commands) {
    if ($line -match '^([0-9]+)\s') { $lastId = [Math]::Max($lastId,[long]$Matches[1]) }
}
$text = $Mode.ToLowerInvariant()
if ($SetMix) { $text = "custom $VoiceDb $MusicDb $OtherDb $Reduction $BoostDb" }
$written = $false
for ($attempt = 0; $attempt -lt 50; $attempt++) {
    try {"$($lastId+1) $text" | Add-Content -LiteralPath $commands -Encoding ASCII; $written = $true; break}
    catch [System.IO.IOException] {Start-Sleep -Milliseconds 10}
}
if (-not $written) {throw 'Command file remained locked; retry after checking the controller'}
Write-Host "Queued: $text"
