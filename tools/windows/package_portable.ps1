# Assemble a self-contained copy of the native build for another PC or a Steam Deck (Proton).
#   powershell -ExecutionPolicy Bypass -File tools\windows\package_portable.ps1 [-NoAssets] [-NoSave]
# Output: dist\WWE-SVR2009-Native\ with svr2009.exe, its DLLs, the Visual C++ runtime, svr2009.toml
# (settings), userdata\ (your save) and assets\ (YOUR game files, copied from this project's assets\
# folder). The folder runs with no arguments: the game finds assets\ and userdata\ beside it.
# It contains your own game files: it is for your own devices only, never share or upload it.
param([switch]$NoAssets, [switch]$NoSave)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Main = $Root  # this project keeps its own game files in assets\
$Build = "$Root\out\build\win-amd64-release"
$Out = "$Root\dist\WWE-SVR2009-Native"

if (-not (Test-Path "$Build\svr2009.exe")) { throw "Build first: $Build\svr2009.exe is missing" }
New-Item -ItemType Directory -Force $Out | Out-Null

# Program files.
foreach ($f in "svr2009.exe", "rexruntime.dll", "rexgpu-xenos.dll") {
  Copy-Item "$Build\$f" $Out -Force
}
Copy-Item "$Build\fonts" $Out -Recurse -Force
# Visual C++ runtime, app-local (Proton/Wine and PCs without the redistributable).
foreach ($f in "msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll") {
  Copy-Item "$env:WINDIR\System32\$f" $Out -Force
}

# Settings (command-line arguments still override these).
@"
# WWE SVR 2009 (native renderer) settings. Command-line arguments override these.
fullscreen = true
svr_60fps = true
# Internal resolution: 0 = auto (1440p on 1080p/1440p screens, 4K on 4K screens, 720p on
# Steam Deck), 1 = 720p (original), 2 = 1440p, 3 = 4K. Higher is sharper and needs more GPU.
svr_render_scale = 0
# Screen shape: 5 = the game's 16:9 (thin bars on 16:10 screens such as the Steam Deck),
# 6 = stretch to fill.
bd_aspect_ratio = 5
log_file = "game.log"
"@ | Set-Content "$Out\svr2009.toml" -Encoding ascii

if (-not $NoSave) {
  New-Item -ItemType Directory -Force "$Out\userdata" | Out-Null
  if (Test-Path "$Root\userdata") {
    robocopy "$Root\userdata" "$Out\userdata" /E /XD cache /NFL /NDL /NJH /NJS /NP | Out-Null
  }
}
if (-not $NoAssets) {
  Write-Host "Copying game files from $Main\assets (about 6 GB)..."
  robocopy "$Main\assets" "$Out\assets" /E /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { throw "Copying assets failed (robocopy $LASTEXITCODE)" }
}

@"
WWE SVR 2009 - native renderer build (personal copy)

This folder contains YOUR game files (assets). Do not share or upload it.

Windows: run svr2009.exe.

Steam Deck:
 1. Copy this whole folder to the Deck (USB drive, microSD card or a network share), e.g. to
    /home/deck/Games/WWE-SVR2009-Native
 2. In Desktop Mode open Steam > Games > Add a Non-Steam Game to My Library > Browse,
    and pick svr2009.exe in that folder.
 3. In Steam, open the game's Properties > Compatibility, tick "Force the use of a specific
    Steam Play compatibility tool" and choose Proton Experimental (or the newest Proton).
 4. Launch it (Game Mode works too). Controls use the Deck's built-in controller.

Settings are in svr2009.toml (for example fullscreen = false, or svr_render_scale = 3
to render at 4K). If something goes wrong, send
game.log from this folder.
"@ | Set-Content "$Out\README.txt" -Encoding ascii

$size = (Get-ChildItem $Out -Recurse -File | Measure-Object Length -Sum).Sum / 1GB
Write-Host ("Done: {0} ({1:N1} GB)" -f $Out, $size)
