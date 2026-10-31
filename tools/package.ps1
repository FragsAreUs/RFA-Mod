# Makes a clean copy of the mod and the editor to share with others, in dist\RFA-Mod\ and as a
# zip next to it. It holds what a new user needs and nothing of yours: the settings files are the
# templates from config\ (no GameDir, your console settings or video settings), the overrides are
# the mod's own from src\overrides (not the files you edited), and logs\ and link\ stay out.
# Every file packed is checked for anything naming this PC (your user folder or name, this
# project's folder, your game folder); the package is refused if one does.
#
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1             package what bin\ has now
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 -Build      run build.cmd first
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 -NoEditor   leave the editor out
param([switch]$Build, [switch]$NoEditor)
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
    @{ From = 'README.md';              To = 'README.md' }
)
foreach ($dll in Get-ChildItem -LiteralPath (Join-Path $bin 'mods') -Filter '*.dll') {
    $parts += @{ From = "bin\mods\$($dll.Name)"; To = "mods\$($dll.Name)" }
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
$zip = Join-Path $dist ("RFA-Mod-{0:yyyy-MM-dd}.zip" -f (Get-Date))
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip
Write-Host "Packaged $($parts.Count) files into $out"
Write-Host "and $zip ($([math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)) MB)."
