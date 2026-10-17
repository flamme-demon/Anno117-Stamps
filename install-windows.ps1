#Requires -Version 5.1
<# Install the extracted release. Run with -DryRun to preview destinations. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [string]$Documents = '',
    [string]$DriveC = 'C:\',
    [string]$Profile = '',
    [Alias('WhatIf')][switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$modId = 'anno117-stamps-native-ui'
if (-not $Documents) {
    $Documents = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Anno 117 - Pax Romana'
}
$GameDir = [IO.Path]::GetFullPath($GameDir)
$Documents = [IO.Path]::GetFullPath($Documents)
$DriveC = [IO.Path]::GetFullPath($DriveC)
$bin = Join-Path $GameDir 'Bin/Win64'
$currentDll = Join-Path $bin 'dbghelp.dll'
$originalDll = Join-Path $bin 'dbghelp_orig.dll'
$sourceDll = Join-Path $PSScriptRoot 'dbghelp.dll'
if (-not [IO.File]::Exists($sourceDll)) {
    $sourceDll = Join-Path $PSScriptRoot 'build/native-stamps-ui/dbghelp.dll'
}
$modSource = Join-Path $PSScriptRoot "mods/$modId"
$runtime = Join-Path $DriveC 'anno117-stamps/native-ui'
$selector = Join-Path $Documents 'mods/profile.txt'
foreach ($file in @($sourceDll, (Join-Path $modSource 'modinfo.json'),
    (Join-Path $modSource 'scripts/native-stamps-data.lua'),
    (Join-Path $modSource 'data/base/config/export/assets.xml'),
    (Join-Path $modSource 'native-ui/icons.tsv'), (Join-Path $modSource 'native-ui/catalogue.tsv'))) {
    if (-not [IO.File]::Exists($file)) { throw "Incomplete package. Extract the entire release ZIP. Missing: $file" }
}
function Test-WindowsDll([string]$Path) {
    if (-not [IO.File]::Exists($Path)) { throw "DLL not found: $Path" }
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw "Invalid Windows DLL: $Path" }
    return [Text.Encoding]::ASCII.GetString($bytes).Contains('dbghelp_orig')
}
$null = Test-WindowsDll $sourceDll
$currentIsProxy = Test-WindowsDll $currentDll
if ([IO.File]::Exists($originalDll)) {
    if (Test-WindowsDll $originalDll) { throw 'dbghelp_orig.dll is a proxy, not the original game DLL.' }
} elseif ($currentIsProxy) {
    throw 'Proxy already installed but dbghelp_orig.dll is missing. Restore the original game DLL first.'
}
if (Get-Process -Name 'Anno117', 'Anno117_plus' -ErrorAction SilentlyContinue) { throw 'Close Anno before installing.' }
if (-not $Profile -and [IO.File]::Exists($selector)) {
    $match = [regex]::Match([IO.File]::ReadAllText($selector), '(?m)^[\t ]*ActiveProfile[\t ]+([^\r\n]+)')
    if ($match.Success) { $Profile = $match.Groups[1].Value.Trim() }
}
if (-not $Profile) { $Profile = 'default' }
if ($Profile -in @('.', '..') -or $Profile -match '[<>:"/\\|?*\x00-\x1f]' -or $Profile -match '[. ]$') {
    throw 'Invalid profile name.'
}
$profileFile = Join-Path $Documents "mods/profiles/$Profile.txt"
$utf8 = New-Object Text.UTF8Encoding($false)
$stage = Join-Path ([IO.Path]::GetTempPath()) ('anno117-stamps-install-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($stage)
try {
    $profileLines = New-Object 'System.Collections.Generic.List[string]'
    $found = $false
    if ([IO.File]::Exists($profileFile)) {
        foreach ($line in [IO.File]::ReadAllLines($profileFile)) {
            if ($line -match ('^\s*#?\s*' + [regex]::Escape($modId) + '(\s+#.*)?\s*$')) {
                if (-not $found) { $profileLines.Add($modId); $found = $true }
            } else { $profileLines.Add($line) }
        }
    }
    if (-not $found) { $profileLines.Add($modId) }
    [IO.File]::WriteAllText((Join-Path $stage 'profile.txt'), ($profileLines -join "`r`n") + "`r`n", $utf8)
    $selectorLines = New-Object 'System.Collections.Generic.List[string]'
    $found = $false
    if ([IO.File]::Exists($selector)) {
        foreach ($line in [IO.File]::ReadAllLines($selector)) {
            if ($line -match '^\s*ActiveProfile(\s|$)') {
                if (-not $found) { $selectorLines.Add("ActiveProfile $Profile"); $found = $true }
            } else { $selectorLines.Add($line) }
        }
    }
    if (-not $found) { $selectorLines.Add("ActiveProfile $Profile") }
    [IO.File]::WriteAllText((Join-Path $stage 'selector.txt'), ($selectorLines -join "`r`n") + "`r`n", $utf8)

    $changes = New-Object 'System.Collections.Generic.List[object]'
    function Add-File([string]$Source, [string]$Target) {
        if ([IO.Directory]::Exists($Target)) { throw "A directory occupies a destination file: $Target" }
        $changes.Add([pscustomobject]@{ Source = $Source; Target = $Target; Before = [IO.File]::Exists($Target) })
    }
    if (-not [IO.File]::Exists($originalDll)) { Add-File $currentDll $originalDll }
    foreach ($file in [IO.Directory]::GetFiles($modSource, '*', [IO.SearchOption]::AllDirectories)) {
        $relative = $file.Substring($modSource.Length).TrimStart([char[]]@('/', '\'))
        Add-File $file (Join-Path (Join-Path $Documents "mods/$modId") $relative)
    }
    Add-File (Join-Path $modSource 'native-ui/icons.tsv') (Join-Path $runtime 'icons.tsv')
    if (-not [IO.File]::Exists((Join-Path $runtime 'catalogue.tsv'))) {
        Add-File (Join-Path $modSource 'native-ui/catalogue.tsv') (Join-Path $runtime 'catalogue.tsv')
    }
    Add-File (Join-Path $stage 'profile.txt') $profileFile
    Add-File (Join-Path $stage 'selector.txt') $selector
    Add-File $sourceDll $currentDll
    Write-Host "Game: $GameDir`nDocuments: $Documents`nRuntime: $runtime`nProfile: $Profile"
    if ($DryRun) {
        foreach ($change in $changes) { Write-Host "Would install: $($change.Target)" }
        Write-Host 'Dry run complete. No installation files changed.'
        return
    }

    $backupName = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N')
    $backup = Join-Path $Documents "stamps-mod-backups/$backupName"
    [void][IO.Directory]::CreateDirectory((Join-Path $backup 'files'))
    $manifest = New-Object 'System.Collections.Generic.List[object]'
    for ($i = 0; $i -lt $changes.Count; $i++) {
        $change = $changes[$i]
        $saved = Join-Path $backup "files/$i.before"
        if ($change.Before) { [IO.File]::Copy($change.Target, $saved, $false) }
        $manifest.Add([pscustomobject]@{ Target = $change.Target; Backup = $saved; PreviouslyPresent = $change.Before })
    }
    [IO.File]::WriteAllText((Join-Path $backup 'files.json'), (ConvertTo-Json -InputObject $manifest.ToArray() -Depth 3), $utf8)
    function Write-Atomic([string]$Source, [string]$Target) {
        $parent = [IO.Path]::GetDirectoryName($Target)
        [void][IO.Directory]::CreateDirectory($parent)
        $temporary = Join-Path $parent ('.anno117-stamps-' + [guid]::NewGuid().ToString('N') + '.tmp')
        try {
            [IO.File]::Copy($Source, $temporary, $false)
            if ([IO.File]::Exists($Target)) { [IO.File]::Replace($temporary, $Target, [NullString]::Value) }
            else { [IO.File]::Move($temporary, $Target) }
        } finally {
            if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
        }
    }
    $attempted = 0
    try {
        foreach ($change in $changes) {
            $attempted++
            Write-Atomic $change.Source $change.Target
        }
        [void][IO.Directory]::CreateDirectory((Join-Path $Documents 'stamps'))
    } catch {
        $failure = $_
        for ($i = $attempted - 1; $i -ge 0; $i--) {
            try {
                if ($changes[$i].Before) { Write-Atomic (Join-Path $backup "files/$i.before") $changes[$i].Target }
                elseif ([IO.File]::Exists($changes[$i].Target)) { [IO.File]::Delete($changes[$i].Target) }
            } catch { Write-Warning "Restore manually: $($changes[$i].Target)" }
        }
        Write-Warning "Installation failed. Rollback attempted; backup: $backup"
        throw $failure
    }
    Write-Host "Installed. Backup: $backup"
} finally {
    if ([IO.Directory]::Exists($stage)) { [IO.Directory]::Delete($stage, $true) }
}
