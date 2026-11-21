# Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
# GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
# content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
<#
.SYNOPSIS
    Builds the fixed-soul overlay for an official 0.63.9 install: release\FixedSoul and release\KCT-FixedSoul-0.63.9.zip.
    Both PCs must get the SAME zip: its build id is part of the connection check.
#>
[CmdletBinding()]
param([string] $BuildId = 'fixedsoul-0.63.9-3')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'release\FixedSoul'
$zip = Join-Path $root 'release\KCT-FixedSoul-0.63.9.zip'

[IO.File]::WriteAllText((Join-Path $root 'BUILD_ID'), $BuildId, (New-Object Text.UTF8Encoding($false)))
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Build-And-Install-Mod.ps1') -NoInstall
if ($LASTEXITCODE -ne 0) { throw 'the pak build failed' }
& dotnet build (Join-Path $root 'dotnet\KcdMp.Client\KcdMp.Client.csproj') -c Release --nologo -v q --no-incremental
if ($LASTEXITCODE -ne 0) { throw 'the agent build failed' }

$bin = Join-Path $root 'dotnet\KcdMp.Client\bin\Release\net8.0'
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null
Copy-Item (Join-Path $bin 'KcdMpClient.dll'), (Join-Path $bin 'KcdMp.Protocol.dll') $out
Copy-Item (Join-Path $root 'kdcmp\Data\kdcmp.pak') $out
Copy-Item (Join-Path $PSScriptRoot 'Install-FixedSoul.ps1') $out
Copy-Item (Join-Path $root 'docs\FIXED-SOULS.md'), (Join-Path $root 'LICENSE'), (Join-Path $root 'NOTICE'), (Join-Path $root 'AUTHORS') $out
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip
Get-ChildItem $out | ForEach-Object { '{0,-24} {1,12:N0}' -f $_.Name, $_.Length }
Write-Host "Built $zip (build id $BuildId)" -ForegroundColor Green
