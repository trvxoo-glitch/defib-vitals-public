<#
Defib smoke test (main-menu level): start the game through F4SE, wait until Vitals says the main menu is ready,
end the game, and run the log analyzer. Needs Vitals installed in the slot being tested.
This answers the plan's open question "can launch and quit be automated" for the main-menu level.
It does NOT load a save yet.

  powershell -File tools\smoke_test.ps1 -GameDir "C:\...\Fallout 4" [-TimeoutSeconds 240] [-Out report.md]

Slot 2 runs through a portable Mod Organizer 2, whose files are only visible to programs MO2 starts. There, start
the game through MO2's F4SE entry instead of f4se_loader.exe, and point -PluginsDir at the Vitals mod folder:

  powershell -File tools\smoke_test.ps1 -GameDir "D:\Games\Fallout 4" `
      -LaunchWith "D:\MO2\ModOrganizer.exe" -LaunchArgs "moshortcut://:F4SE" -Compare

-Compare adds "what changed since the last run" (handy for the census: run once without a mod, once with it).

It never edits your files. It stops the game with Stop-Process after Vitals reports the main menu (no save is written).
Use it on a test slot or the clean Vortex profile, not mid-playthrough.
#>
param(
  [Parameter(Mandatory = $true)][string]$GameDir,
  [int]$TimeoutSeconds = 240,
  [string]$MyGames = "$env:USERPROFILE\Documents\My Games\Fallout4",
  [string]$Out = "smoke_report.md",
  [string]$LaunchWith = "",   # e.g. MO2's ModOrganizer.exe; default is <GameDir>\f4se_loader.exe
  [string]$LaunchArgs = "",   # e.g. "moshortcut://:F4SE" (the name of the F4SE entry in MO2)
  [string]$PluginsDir = "",   # where the F4SE plugins really are (MO2: a mod folder); default <GameDir>\Data\F4SE\Plugins
  [switch]$Compare
)
$ErrorActionPreference = "Stop"
$status = Join-Path $MyGames "F4SE\Vitals\status.json"
if (Test-Path $status) { Remove-Item $status }
$loader = if ($LaunchWith) { $LaunchWith } else { Join-Path $GameDir "f4se_loader.exe" }
if (-not (Test-Path $loader)) { throw "$loader not found" }

Write-Host "Starting $loader $LaunchArgs ..."
$startArgs = @{ FilePath = $loader; WorkingDirectory = (Split-Path -Parent $loader); PassThru = $true }
if ($LaunchArgs) { $startArgs.ArgumentList = $LaunchArgs }
$launched = Get-Date
$p = Start-Process @startArgs
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$ready = $false
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Seconds 3
  if (Test-Path $status) {
    try { $s = Get-Content $status -Raw | ConvertFrom-Json } catch { continue }
    if ($s.main_menu_ready) { $ready = $true; break }
  }
}
if ($ready) { Write-Host "Main menu reached (last phase: $($s.last_phase))." } else { Write-Host "TIMEOUT: main menu not reported within $TimeoutSeconds s." }
Start-Sleep -Seconds 5
# Stop only the game this script started: Vitals writes its process id to status.json. Without it, only a Fallout4
# process from this GameDir that started after the launch above. Another running copy of the game is never touched.
$gamePid = if ($s -and $s.pid) { [int]$s.pid } else { 0 }
$game = if ($gamePid) { Get-Process -Id $gamePid -ErrorAction SilentlyContinue } else {
  $exe = (Resolve-Path (Join-Path $GameDir "Fallout4.exe") -ErrorAction SilentlyContinue).Path
  Get-Process -Name "Fallout4" -ErrorAction SilentlyContinue | Where-Object {
    $exe -and $_.Path -and ($_.Path -ieq $exe) -and $_.StartTime -ge $launched }
}
if ($game) { $game | Stop-Process -Force } else { Write-Host "No game process from this launch found to stop." }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 3

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $PluginsDir) { $PluginsDir = Join-Path $GameDir "Data\F4SE\Plugins" }
python "$here\defib_logs.py" $MyGames --plugins $PluginsDir --out $Out
Write-Host "Report: $Out"
if ($Compare) {
  $cmp = [System.IO.Path]::ChangeExtension($Out, ".compare.md")
  python "$here\defib_logs.py" $MyGames --compare --out $cmp
  Write-Host "Comparison with the previous run: $cmp"
}
if (-not $ready) { exit 1 }
