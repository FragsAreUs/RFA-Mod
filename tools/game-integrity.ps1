# Snapshot / verify the Red Faction: Armageddon install so we can prove mods never touch it.
# The baseline is the publicbeta install; pass -Baseline reference\build-1291145\game_baseline.csv
# to check against the default branch instead.
#   powershell -ExecutionPolicy Bypass -File tools\game-integrity.ps1 -Mode Snapshot
#   powershell -ExecutionPolicy Bypass -File tools\game-integrity.ps1 -Mode Verify
# The game folder is -GameDir, or GameDir in bin\rfa_mods.ini (the one the launcher uses).
param(
    [ValidateSet('Snapshot', 'Verify')] [string]$Mode = 'Verify',
    [string]$GameDir = '',
    [string]$Baseline = (Join-Path $PSScriptRoot '..\reference\build-1932726-publicbeta\game_baseline.csv')
)
$ErrorActionPreference = 'Stop'
if (-not $GameDir) {
    $ini = Join-Path $PSScriptRoot '..\bin\rfa_mods.ini'
    if (Test-Path -LiteralPath $ini) {
        $line = Get-Content -LiteralPath $ini | Where-Object { $_ -match '^\s*GameDir\s*=' } | Select-Object -First 1
        if ($line) { $GameDir = ($line -split '=', 2)[1].Trim() }
    }
}
if (-not $GameDir) { throw 'Where is the game? Pass -GameDir <folder>, or set GameDir in bin\rfa_mods.ini.' }
$GameDir = (Resolve-Path $GameDir).Path.TrimEnd('\')

# Files the publicbeta build itself writes into its folder: the last level it loaded, an empty
# "load finished" marker, and the console's print_to_file output. Listed, not counted as changes.
$GameWrites = @('rfg_last_level_pc.txt', 'Load_Successful.txt', 'console_commands.txt')

function Get-Manifest {
    Get-ChildItem -LiteralPath $GameDir -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
        [pscustomobject]@{
            Path   = $_.FullName.Substring($GameDir.Length + 1)
            Length = $_.Length
            SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    }
}

if ($Mode -eq 'Snapshot') {
    $m = Get-Manifest
    $m | Export-Csv -LiteralPath $Baseline -NoTypeInformation -Encoding UTF8
    "Snapshot written: $($m.Count) files -> $Baseline"
    return
}

if (-not (Test-Path -LiteralPath $Baseline)) { throw "No baseline at $Baseline. Run with -Mode Snapshot first." }
$old = @{}; Import-Csv -LiteralPath $Baseline | ForEach-Object { $old[$_.Path] = $_ }
$new = @{}; Get-Manifest | ForEach-Object { $new[$_.Path] = $_ }
$problems = 0
foreach ($k in $old.Keys) {
    if (-not $new.ContainsKey($k)) { "MISSING  $k"; $problems++ }
    elseif ($new[$k].SHA256 -ne $old[$k].SHA256) { "CHANGED  $k"; $problems++ }
}
foreach ($k in $new.Keys) {
    if ($old.ContainsKey($k)) { continue }
    if ($GameWrites -contains $k) { "GAME     $k (written by the game itself)" }
    else { "ADDED    $k"; $problems++ }
}
if ($problems -eq 0) { "OK: all $($old.Count) game files match the baseline. Install is untouched." }
else { "$problems difference(s) from baseline."; exit 1 }
