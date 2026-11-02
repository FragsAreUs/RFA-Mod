# Makes a clean copy of the mod and the editor to share with others, in dist\RFA-Mod\ and as a
# zip next to it. It holds what a new user needs and nothing of yours: the settings files are the
# templates from config\ (no GameDir, your console settings or video settings), the overrides are
# the mod's own from src\overrides (not the files you edited), the documents are the ones the
# README links to, and logs\ and link\ stay out.
# Every file packed is checked for anything naming this PC (your user folder or name, this
# project's folder, your game folder); the package is refused if one does.
#
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1             package what bin\ has now
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 -Build      run build.cmd first
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 -NoEditor   leave the editor out
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 -Version v0.1.0-beta
#       a release: the zip is named after the version, and version.txt says which commit it is
param([switch]$Build, [switch]$NoEditor, [string]$Version)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$bin = Join-Path $root 'bin'

if ($Build) {
    & cmd /c "`"$(Join-Path $root 'build.cmd')`""
    if ($LASTEXITCODE -ne 0) { throw 'The build failed, so nothing was packaged.' }
}

# What is shared, and where each part comes from.
$parts = @(
    @{ From = 'bin\RFAModLauncher.exe'; To = 'RFAModLauncher.exe' },
    @{ From = 'bin\rfa_loader.dll';     To = 'rfa_loader.dll' },
    @{ From = 'config\rfa_mods.ini';    To = 'rfa_mods.ini' },
    @{ From = 'config\console.ini';     To = 'console.ini' },
    @{ From = 'README.md';              To = 'README.md' },
    # The documents the README links to, by name: docs\ also holds private notes.
    @{ From = 'docs\editor-guide.md';          To = 'docs\editor-guide.md' },
    @{ From = 'docs\how-it-works.md';          To = 'docs\how-it-works.md' },
    @{ From = 'docs\editor-roadmap.md';        To = 'docs\editor-roadmap.md' },
    @{ From = 'docs\renderer-plan.md';         To = 'docs\renderer-plan.md' },
    @{ From = 'docs\third-party-notices.txt';  To = 'docs\third-party-notices.txt' },
    # The GPL asks that its text travels with the programs.
    @{ From = 'LICENSE';                To = 'LICENSE.txt' }
)
foreach ($dll in Get-ChildItem -LiteralPath (Join-Path $bin 'mods') -Filter '*.dll') {
    $parts += @{ From = "bin\mods\$($dll.Name)"; To = "mods\$($dll.Name)" }
}
# A mod's own scripts (guerrilla_hud's, which GuerrillaHud.exe packs with Guerrilla's documents),
# from src\mods: never bin\mods\<mod>\, which also holds files made from the player's own games.
foreach ($script in Get-ChildItem -LiteralPath (Join-Path $root 'src\mods') -Recurse -Filter '*.lua' -File) {
    $mod = $script.Directory.Name
    $parts += @{ From = "src\mods\$mod\$($script.Name)"; To = "mods\$mod\$($script.Name)" }
}
$overrides = Join-Path $root 'src\overrides'
foreach ($file in Get-ChildItem -LiteralPath $overrides -Recurse -File) {
    $parts += @{ From = "src\overrides\$($file.FullName.Substring($overrides.Length + 1))"; To = "overrides\$($file.FullName.Substring($overrides.Length + 1))" }
}
if (-not $NoEditor) {
    # The program and its settings files; not the .pdb debugging files.
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $bin 'editor') -File | Where-Object { $_.Extension -in '.exe', '.dll', '.json' }) {
        $parts += @{ From = "bin\editor\$($file.Name)"; To = "editor\$($file.Name)" }
    }
    # The libraries the editor ships with ask that their licence travels with them.
    $parts += @{ From = "docs\third-party-notices.txt"; To = "editor\third-party-notices.txt" }
}
foreach ($part in $parts) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $part.From))) { throw "Missing $($part.From): build first (or pass -Build)." }
}
if (-not $NoEditor) {
    # The editor must be 64-bit (its exe's PE header names the machine: 0x8664 is x64); a 32-bit
    # one runs out of memory on big levels.
    $exe = [System.IO.File]::ReadAllBytes((Join-Path $bin 'editor\RfaEditor.exe'))
    $machine = [BitConverter]::ToUInt16($exe, [BitConverter]::ToInt32($exe, 0x3C) + 4)
    if ($machine -ne 0x8664) { throw ('bin\editor\RfaEditor.exe is not 64-bit (machine 0x{0:X4}): build it again with build.cmd.' -f $machine) }
}

# Anything that would tie the package to this PC.
$personal = @($root, $env:USERPROFILE, $env:USERNAME)
$ini = Join-Path $bin 'rfa_mods.ini'
if (Test-Path -LiteralPath $ini) {
    $line = Get-Content -LiteralPath $ini | Where-Object { $_ -match '^\s*GameDir\s*=' } | Select-Object -First 1
    if ($line) { $personal += ($line -split '=', 2)[1].Trim() }
}
$personal = $personal | Where-Object { $_ -and $_.Length -ge 3 } | Select-Object -Unique
$found = @()
foreach ($part in $parts) {
    $bytes = [System.IO.File]::ReadAllBytes((Join-Path $root $part.From))
    # Text in files and programs is either one byte per letter or two (UTF-16), so look for both.
    $asText = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)  # Latin-1: every byte a letter
    $asWide = [System.Text.Encoding]::Unicode.GetString($bytes)
    foreach ($text in $personal) {
        foreach ($form in $text, $text.Replace('\', '/')) {
            if ($asText.IndexOf($form, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
                $asWide.IndexOf($form, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                $found += "$($part.From) names `"$text`""
                break
            }
        }
    }
}
if ($found) {
    $found | ForEach-Object { Write-Host "  $_" }
    throw 'Nothing was packaged: the files above name things on this PC.'
}

$dist = Join-Path $root 'dist'
$out = Join-Path $dist 'RFA-Mod'
if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
foreach ($part in $parts) {
    $to = Join-Path $out $part.To
    New-Item -ItemType Directory -Force -Path (Split-Path $to) | Out-Null
    Copy-Item -LiteralPath (Join-Path $root $part.From) -Destination $to
}
if ($Version) {
    # Which source the programs were built from, for bug reports and the GPL's offer of the source.
    # Only changes to tracked files count: files git doesn't track yet (work in progress) are never
    # built or packed.
    $commit = (& git -C $root rev-parse --short HEAD 2>$null)
    $dirty = (& git -C $root status --porcelain --untracked-files=no 2>$null)
    $from = if (-not $commit) { 'an unknown commit' } elseif ($dirty) { "commit $commit plus changes not yet committed" } else { "commit $commit" }
    Set-Content -LiteralPath (Join-Path $out 'version.txt') -Encoding ASCII -Value @(
        "RFA-Mod $Version, built from $from.",
        'Source code: https://github.com/FragsAreUs/RFA-Mod')
    $zip = Join-Path $dist "RFA-Mod-$Version.zip"
} else {
    $zip = Join-Path $dist ("RFA-Mod-{0:yyyy-MM-dd}.zip" -f (Get-Date))
}
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip
Write-Host "Packaged $($parts.Count) files into $out"
Write-Host "and $zip ($([math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)) MB)."
