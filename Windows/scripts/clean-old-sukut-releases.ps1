$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$buildRoot = (Resolve-Path -LiteralPath (Join-Path $projectRoot 'build')).Path
$distRoot = (Resolve-Path -LiteralPath (Join-Path $projectRoot 'dist')).Path
$newZip = Join-Path $distRoot 'Sukut-0.2.3-win-x64-update.zip'
if (!(Test-Path -LiteralPath $newZip) -or (Get-Item -LiteralPath $newZip).Length -eq 0) { throw 'Create the current release ZIP before cleaning old copies.' }
$runningPaths = @(Get-Process | ForEach-Object { try { if ($_.Path) { $_.Path } } catch {} })
$records = [Collections.Generic.List[object]]::new()
$buildNames = @(
    'brown-package-smoke', 'desktop-backup', 'sukut', 'sukut-0.2.0', 'sukut-0.2.1', 'sukut-0.2.2', 'sukut-0.2.3',
    'sukut-before-row', 'sukut-brown-before', 'sukut-fluent', 'sukut-fluent-debug', 'sukut-fluent-final',
    'sukut-fluent-publish', 'sukut-fluent-release', 'sukut-redesign', 'sukut-redesign-baseline',
    'sukut-redesign-engine', 'sukut-row-preview', 'sukut-setup'
)
$candidates = @($buildNames | ForEach-Object { Join-Path $buildRoot $_ } | Where-Object { Test-Path -LiteralPath $_ })
$candidates += @(Get-ChildItem -LiteralPath $distRoot | Where-Object { $_.Name -match '^Sukut-0\.1\.0-win-x64-\d{8}-\d{6}(\.zip)?$' -or $_.Name -match '^Sukut-0\.2\.3-win-x64(\.zip)?$' } | ForEach-Object FullName)
$candidates += @(Get-ChildItem -LiteralPath $buildRoot -Filter '*.xbf' -File | ForEach-Object FullName)
foreach ($candidate in $candidates) {
    $item = Get-Item -LiteralPath $candidate -Force
    $absolute = (Resolve-Path -LiteralPath $item.FullName).Path
    $parent = [IO.Path]::GetDirectoryName($absolute)
    # Each target must be an immediate, explicitly selected child of build/dist.
    # Recursive removal never receives a root or an unchecked computed path.
    if (($parent -ne $buildRoot -and $parent -ne $distRoot) -or
        !($absolute.StartsWith($projectRoot + '\', [StringComparison]::OrdinalIgnoreCase))) { throw "Unsafe cleanup target: $absolute" }
    $reason = ''
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { $reason = 'reparse point preserved' }
    if ($item.PSIsContainer) {
        if (Test-Path -LiteralPath (Join-Path $absolute 'CMakeCache.txt')) { $reason = 'development build configuration preserved' }
        if (Test-Path -LiteralPath (Join-Path $absolute 'runtime')) { $reason = 'local runtime/settings preserved' }
        if ($runningPaths | Where-Object { $_.StartsWith($absolute + '\', [StringComparison]::OrdinalIgnoreCase) }) { $reason = 'running application preserved' }
        $contents = @(Get-ChildItem -LiteralPath $absolute -Recurse -Force)
        if ($contents | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { $reason = 'contains reparse point; preserved' }
        if ($parent -eq $buildRoot -and $item.Name -ne 'brown-package-smoke' -and $item.Name -ne 'desktop-backup' -and
            !(Test-Path -LiteralPath (Join-Path $absolute 'Sukut.exe')) -and !(Test-Path -LiteralPath (Join-Path $absolute 'SukutEngine.dll'))) { $reason = 'not a recognized published app; preserved' }
        if ($item.Name -eq 'desktop-backup' -and ($contents | Where-Object { !$_.PSIsContainer -and $_.Name -notmatch '^Sukut.*\.exe$' })) { $reason = 'unexpected backup contents; preserved' }
        $bytes = ($contents | Where-Object { !$_.PSIsContainer } | Measure-Object -Property Length -Sum).Sum
    } else { $bytes = $item.Length }
    if (!$reason) { Remove-Item -LiteralPath $absolute -Recurse -Force; $status = 'removed' } else { $status = 'preserved' }
    $records.Add([ordered]@{path=$absolute; bytes=[long]$bytes; status=$status; reason=$reason})
}
$reportDirectory = Join-Path $projectRoot 'docs\redesign\release-0.2.3-update'
New-Item -ItemType Directory -Force -Path $reportDirectory | Out-Null
$records | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $reportDirectory 'cleanup.json') -Encoding UTF8
$removed = @($records | Where-Object { $_.status -eq 'removed' })
$removedBytes = ($removed | ForEach-Object { $_.bytes } | Measure-Object -Sum).Sum
Write-Output ("Removed {0} obsolete generated items ({1:N2} GiB)." -f $removed.Count, ($removedBytes / 1GB))
Write-Output ('Preserved: source, tests, scripts, models, dependencies, native bootstrap, development tools/evidence and all Android files.')
foreach ($record in $records | Where-Object { $_.status -ne 'removed' }) { Write-Output ("Preserved {0}: {1}" -f $record.path, $record.reason) }
