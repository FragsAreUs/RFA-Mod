# The one install into bin\ while several people (or Claude sessions) work at once: each builds and
# checks in a staging copy of their own (tools\stage.ps1), and whoever runs the tests installs it all
# into bin\ with this, once, when it's time to play.
#
#   powershell -ExecutionPolicy Bypass -File tools\install-bin.ps1 [-Parts <parts>]
#
#   1. Stops if the game, its launcher, the editor or the Guerrilla builder is running from anywhere.
#   2. Builds everything from the source as it is now into bin\ (build.cmd).
#   3. Runs the Guerrilla builder in bin\: -Parts world, hud, vehicles, weapons, people (a comma list)
#      builds those parts again, then its table check; without -Parts, only the check.
#   4. Lists what went in: bin\'s programs and mods with their times, and the source files changed
#      since the last commit (all of them went into this build, finished or not).
# While it runs, bin\.installing tells tools\stage.ps1 not to copy bin\. Exit code 0 when the build
# and the builder both pass.
param([string]$Parts = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'bin'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8  # the builder writes UTF-8 when its output is piped

# Not the editor's MCP server (RfaEditorMcp, which "RfaEditor" would match too): it holds no library open,
# and build.cmd closes it before it builds into bin\.
$running = @(Get-Process | Where-Object { $_.ProcessName -match '^(RedFactionArmageddon|RFAModLauncher|RfaEditor|GuerrillaBuilder)' -and $_.ProcessName -ne 'RfaEditorMcp' })
if ($running.Count -gt 0) {
    Write-Host "Not installing: close these first: $(($running | ForEach-Object { "$($_.ProcessName) ($($_.Id))" }) -join ', ')"
    exit 1
}

$lock = Join-Path $bin '.installing'
New-Item -ItemType Directory -Force $bin | Out-Null
Set-Content -Path $lock -Value "tools\install-bin.ps1, started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -Encoding UTF8
$code = 1
try {
    Write-Host "Building into bin\ ..."
    & (Join-Path $root 'build.cmd')
    if ($LASTEXITCODE -ne 0) { Write-Host "bin\: the build failed (above); nothing else was done."; exit 1 }

    $builder = Join-Path $bin 'editor\GuerrillaBuilder.exe'
    $words = @($Parts -split '[,\s]+' | Where-Object { $_ })
    $builderArgs = @(if ($words.Count -gt 0) { @('build') + $words } else { 'check' })
    Write-Host "Guerrilla builder in bin\: $($builderArgs -join ' ')"
    & $builder @builderArgs | Out-Host
    $code = $LASTEXITCODE
}
finally {
    Remove-Item -Force $lock -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "==== In bin\ now ===="
$programs = @(Get-Item (Join-Path $bin 'RFAModLauncher.exe'), (Join-Path $bin 'rfa_loader.dll'), (Join-Path $bin 'editor\GuerrillaBuilder.exe'), (Join-Path $bin 'editor\RfaEditor.exe') -ErrorAction SilentlyContinue) +
            @(Get-ChildItem (Join-Path $bin 'mods\*.dll'))
$programs | ForEach-Object { '{0:yyyy-MM-dd HH:mm:ss}  {1}' -f $_.LastWriteTime, $_.FullName.Substring($bin.Length + 1) } | Write-Host
$record = Join-Path $bin 'mods\guerrilla_world\builder.ini'
if (Test-Path $record) {
    Write-Host "Guerrilla builder parts built:"
    $part = ''
    foreach ($line in Get-Content $record) {
        if ($line -match '^\[(.+)\]$') { $part = $Matches[1] }
        elseif ($line -match '^Built=(.+)$') { Write-Host ("  {0,-9} {1}" -f $part, $Matches[1]) }
    }
}
Write-Host "Source changed since the last commit (all of it is in this build):"
$changes = @(& git -C $root status --short -- src config build.cmd tools 2>$null)
if ($changes.Count -eq 0) { Write-Host "  (none)" } else { $changes | ForEach-Object { Write-Host "  $_" } }
Write-Host ""
if ($code -eq 0) { Write-Host "bin\: installed, and the builder passed ($($builderArgs -join ' '))." }
else { Write-Host "bin\: installed, but the builder FAILED ($($builderArgs -join ' '), exit code $code): don't play this build; see bin\logs\guerrilla_builder.log." }
exit $code
