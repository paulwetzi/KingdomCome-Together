# Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
# GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
# content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
<#
.SYNOPSIS
    Puts the modified fixed-soul build over an installed official 0.63.9. Run on BOTH PCs, with the game and launcher closed.
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\Install-FixedSoul.ps1 -PlayerName "Steam name" -Soul ttkc_woman_9
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\Install-FixedSoul.ps1 -Uninstall
#>
[CmdletBinding()]
param(
    [string] $PlayerName,
    [string] $Soul,
    [string] $InstallDir = (Join-Path $env:LOCALAPPDATA 'KCDMP'),
    [string] $ModDir,
    [switch] $Uninstall
)
$ErrorActionPreference = 'Stop'
$Package = $PSScriptRoot
$Backup = Join-Path $env:LOCALAPPDATA 'KCDMP-FixedSoul-Backup'
$ExpectedVersion = '0.63.9'

function Fail([string] $Text) { Write-Host "FAILED: $Text" -ForegroundColor Red; exit 1 }

if (-not (Test-Path (Join-Path $InstallDir 'install-manifest.txt'))) { Fail "no Kingdom Come: Together install in $InstallDir. Run the official Setup $ExpectedVersion first." }
$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match '^(KingdomCome|KcdMpClient|KcdMpServer|KCDMP_launcher)' }
if ($running) { Fail "close the game and the launcher first ($(($running.ProcessName | Sort-Object -Unique) -join ', '))." }

if (-not $ModDir) {
    $steam = (Get-ItemProperty 'HKCU:\SOFTWARE\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    $roots = @()
    if ($steam) {
        $roots += $steam
        $vdf = Join-Path $steam 'config\libraryfolders.vdf'
        if (Test-Path $vdf) { foreach ($l in Get-Content $vdf) { if ($l -match '"path"\s+"([^"]+)"') { $roots += $Matches[1].Replace('\\', '\') } } }
    }
    $hits = @(foreach ($r in ($roots | Sort-Object -Unique)) {
        Get-ChildItem (Join-Path $r 'steamapps\common') -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName 'Mods\kdcmp' } | Where-Object { Test-Path (Join-Path $_ 'Data\kdcmp.pak') }
    })
    if ($hits.Count -ne 1) { Fail "found $($hits.Count) installed copies of the mod. Pass the right one with -ModDir '<Modding Tools>\Mods\kdcmp'." }
    $ModDir = $hits[0]
}

$Targets = @(
    @{ Key = 'APP|KcdMpClient.dll';            Path = Join-Path $InstallDir 'KcdMpClient.dll';            From = 'KcdMpClient.dll' },
    @{ Key = 'APP|KcdMp.Protocol.dll';         Path = Join-Path $InstallDir 'KcdMp.Protocol.dll';         From = 'KcdMp.Protocol.dll' },
    @{ Key = 'APP|KCDMP.dll';                  Path = Join-Path $InstallDir 'KCDMP.dll';                  From = 'KCDMP.dll' },
    @{ Key = 'MOD|Data\kdcmp.pak';             Path = Join-Path $ModDir 'Data\kdcmp.pak';                 From = 'kdcmp.pak' },
    @{ Key = 'APP|mod\kdcmp\Data\kdcmp.pak';   Path = Join-Path $InstallDir 'mod\kdcmp\Data\kdcmp.pak';   From = 'kdcmp.pak' }
)
$Manifest = Join-Path $InstallDir 'install-manifest.txt'

if ($Uninstall) {
    if (-not (Test-Path $Backup)) { Fail "no backup in $Backup." }
    foreach ($t in $Targets) {
        $saved = Join-Path $Backup ($t.Key -replace '[|\\]', '_')
        if (Test-Path $saved) { Copy-Item $saved $t.Path -Force }
    }
    Copy-Item (Join-Path $Backup 'install-manifest.txt') $Manifest -Force
    Remove-Item (Join-Path $InstallDir 'fixed-souls.json') -Force -ErrorAction SilentlyContinue
    Remove-Item $Backup -Recurse -Force
    Write-Host 'Official files restored.' -ForegroundColor Green
    exit 0
}

if (($PlayerName -and -not $Soul) -or ($Soul -and -not $PlayerName)) { Fail 'pass -PlayerName and -Soul together.' }
foreach ($t in $Targets) { if (-not (Test-Path (Join-Path $Package $t.From))) { Fail "$($t.From) is missing from this package." } }
if (-not (Test-Path $Backup)) {
    $installed = (Get-Item (Join-Path $InstallDir 'KcdMpClient.dll')).VersionInfo.ProductVersion
    if (-not "$installed".StartsWith($ExpectedVersion)) { Fail "this package fits $ExpectedVersion, installed is $installed." }
    New-Item -ItemType Directory -Force -Path $Backup | Out-Null
    foreach ($t in $Targets) { if (Test-Path $t.Path) { Copy-Item $t.Path (Join-Path $Backup ($t.Key -replace '[|\\]', '_')) -Force } }
    Copy-Item $Manifest (Join-Path $Backup 'install-manifest.txt') -Force
}
# A backup made by an older package lacks files this one replaces for the first time (the official KCDMP.dll): keep them for -Uninstall.
foreach ($t in $Targets) {
    $saved = Join-Path $Backup ($t.Key -replace '[|\\]', '_')
    if ((Test-Path $t.Path) -and -not (Test-Path $saved)) { Copy-Item $t.Path $saved -Force }
}

$lines = [IO.File]::ReadAllLines($Manifest)
foreach ($t in $Targets) {
    if (-not (Test-Path (Split-Path $t.Path))) { continue }
    Copy-Item (Join-Path $Package $t.From) $t.Path -Force
    $info = Get-Item $t.Path
    $hash = (Get-FileHash $t.Path -Algorithm SHA256).Hash.ToLowerInvariant()
    $prefix = $t.Key + '|'
    for ($i = 0; $i -lt $lines.Length; $i++) {
        if ($lines[$i].StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { $lines[$i] = "$($t.Key)|$($info.Length)|$hash" }
    }
}
[IO.File]::WriteAllLines($Manifest, $lines, (New-Object Text.UTF8Encoding($false)))

if ($PlayerName) {
    $json = @{ $PlayerName = $Soul } | ConvertTo-Json -Compress
    [IO.File]::WriteAllText((Join-Path $InstallDir 'fixed-souls.json'), $json, (New-Object Text.UTF8Encoding($false)))
    Write-Host "Mapping written: $Soul" -ForegroundColor Green
} elseif (-not (Test-Path (Join-Path $InstallDir 'fixed-souls.json'))) {
    Write-Host 'Im Spiel: Einfg druecken, dann Charakter wechseln.' -ForegroundColor Cyan
}
Write-Host "Installed into $InstallDir and $ModDir" -ForegroundColor Green
