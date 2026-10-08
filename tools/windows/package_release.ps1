# Build the public release zip: this game's native build, WITHOUT any game files.
#   powershell -ExecutionPolicy Bypass -File tools\windows\package_release.ps1 [-Version 1.0]
# Output: dist\WWE-SVR2009-Native-v<version>-Windows-and-SteamDeck.zip (and the unpacked folder).
# Players put their own disc image next to svr2009.exe; the game finds it there.
param([string]$Version = "dev")
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Id = "svr2009"
$Title = "WWE SmackDown vs. Raw 2009"
$Name = "WWE-SVR2009-Native"
$Build = "$Root\out\build\win-amd64-release"
if (-not (Test-Path "$Build\$Id.exe")) { throw "Build the game first: tools\windows\build.ps1" }
# A CMake cache reset once silently rebuilt with the emulated GPU: only the native build ships.
if (-not (Select-String -Path "$Build\CMakeCache.txt" -Pattern "^SVR_NATIVE_RENDERER:BOOL=ON" -Quiet)) {
  throw "$Build is not a native-renderer build"
}

$Out = "$Root\dist\$Name-v$Version"
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out, "$Out\licenses" | Out-Null
foreach ($f in "$Id.exe", "rexruntime.dll", "rexgpu-xenos.dll") { Copy-Item "$Build\$f" $Out }
Copy-Item "$Build\fonts" $Out -Recurse
# Visual C++ runtime, app-local (Proton and PCs without the redistributable).
foreach ($f in "msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll") {
  Copy-Item "$env:WINDIR\System32\$f" $Out
}

@"
# $Title settings. Edit with any text editor; command-line arguments override these.
fullscreen = true
svr_60fps = true
# Keyboard works as a controller too (rebind with F4 in game); a real controller is recommended.
mnk_mode = true
# Internal resolution: 0 = auto (1440p on 1080p/1440p screens, 4K on 4K screens, 720p on
# Steam Deck), 1 = 720p (original), 2 = 1440p, 3 = 4K.
svr_render_scale = 0
# Screen shape: 5 = the game's 16:9 (thin bars on 16:10 screens such as the Steam Deck),
# 6 = stretch to fill.
bd_aspect_ratio = 5
log_file = "game.log"
"@ | Set-Content "$Out\$Id.toml" -Encoding ascii

@"
$Title - native PC / Steam Deck version

YOU NEED YOUR OWN COPY OF THE GAME. This download contains no game files and never downloads
any. It only runs from a disc image (.iso) of your own disc (USA / Europe release). It does not
condone piracy.

Windows
 1. Put your disc image (.iso) in this folder, next to $Id.exe.
 2. Run $Id.exe.
 (Without an .iso here, the game asks you to pick one the first time and remembers it.)

Steam Deck
 1. In Desktop Mode, extract this folder to the Deck (for example /home/deck/Games/$Name)
    and put your .iso next to $Id.exe.
 2. Steam > Games > Add a Non-Steam Game to My Library > Browse > pick $Id.exe
    (set the file type filter to All files).
 3. The shortcut's Properties > Compatibility > Force the use of a specific Steam Play
    compatibility tool > Proton Experimental.
 4. Play it from Game Mode.

Settings: $Id.toml (resolution up to 4K, fullscreen, 60 or 30 fps, screen shape).
Saves: the userdata folder (created on first start). If something goes wrong, send game.log.
"@ | Set-Content "$Out\README.txt" -Encoding ascii

# Licence notices that travel with the binaries.
$Notices = @{
  "LICENSE" = "this project (MIT).txt"; "THIRD_PARTY.md" = "THIRD_PARTY.md"
  "src\reblue\LICENSE.reblue" = "re-Blue renderer (BSD-3-Clause).txt"
  "third_party\rexglue-sdk\LICENSE" = "ReXGlue SDK (BSD-3-Clause).txt"
  "third_party\plume\LICENSE" = "plume (MIT).txt"
  "third_party\reblue_thirdparty\zstd\LICENSE" = "zstd (BSD).txt"
  "res\fonts\OFL.txt" = "Press Start 2P font (OFL).txt"
}
foreach ($k in $Notices.Keys) { Copy-Item "$Root\$k" "$Out\licenses\$($Notices[$k])" }

$Zip = "$Root\dist\$Name-v$Version-Windows-and-SteamDeck.zip"
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path "$Out\*" -DestinationPath $Zip
Write-Host ("Release: {0} ({1:N0} MB)" -f $Zip, ((Get-Item $Zip).Length / 1MB))
