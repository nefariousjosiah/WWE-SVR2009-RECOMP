# Build the public release zip: this game's native build, WITHOUT any game files.
#   powershell -ExecutionPolicy Bypass -File tools\windows\package_release.ps1 [-Version 1.0]
# Output: dist\WWE-SVR2009-RECOMP-v<version>-Windows-and-SteamDeck.zip (and the unpacked folder).
# Players open Launcher.exe and point it at their own disc image (or put it next to svr2009.exe).
param([string]$Version = "dev")
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Id = "svr2009"
$Title = "WWE SmackDown vs. Raw 2009"
$Name = "WWE-SVR2009-RECOMP"
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
# The launcher: picks the disc image, keeps the settings, starts the game.
& powershell -ExecutionPolicy Bypass -File "$Root\launcher\build.ps1"
if ($LASTEXITCODE) { throw "launcher build failed" }
Copy-Item "$Root\out\launcher\Launcher.exe" $Out
Copy-Item "$Root\launcher\res\fonts\Roboto-Medium.ttf" "$Out\fonts"
Copy-Item "$Root\launcher\res\cover.jpg" "$Out\cover.jpg"  # the game's box art (THQ / WWE)

@"
# $Title settings. Edit with any text editor; command-line arguments override these.
fullscreen = true
svr_60fps = true
# Keyboard controls are not tested yet; a controller is recommended (rebind keys with F4 in game).
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

YOU NEED YOUR OWN COPY OF THE GAME. This download contains none of the files from the game disc
and never downloads any: the game's data (models, textures, sound, video) is read from a disc
image (.iso) of your own disc, USA / Europe release. It does not condone piracy.

Windows
 1. Run Launcher.exe.
 2. Press "Choose disc image..." and pick your .iso (it is checked and remembered).
 3. Press Play.
 (Or put the .iso in this folder: the launcher finds it by itself.)

Steam Deck (start the game itself, not the launcher: the launcher still has bugs on the Deck)
 1. In Desktop Mode, extract this folder to the Deck (for example /home/deck/Games/$Name)
    and copy your .iso into it, next to $Id.exe. The game finds it there by itself.
 2. Steam > Games > Add a Non-Steam Game to My Library > Browse > pick $Id.exe
    (set the file type filter to All files).
 3. The shortcut's Properties > Compatibility > Force the use of a specific Steam Play
    compatibility tool > Proton Experimental.
 4. Start it from Game Mode. Its settings are in $Id.toml (bd_aspect_ratio = 6 stretches the
    picture to fill the Deck's 16:10 screen).

Settings: the launcher's Settings button (resolution up to 4K, fullscreen or window, 60 or
30 fps, screen shape), saved in $Id.toml. Box art: cover.jpg (drop another image on the
launcher to change it).
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
  "third_party\rexglue-sdk\thirdparty\sdl3\LICENSE.txt" = "SDL3 (zlib).txt"
  "launcher\third_party\imgui\LICENSE.txt" = "Dear ImGui (MIT).txt"
  "launcher\res\fonts\LICENSE-Roboto.txt" = "Roboto font (Apache-2.0).txt"
  # Libraries inside rexruntime.dll / rexgpu-xenos.dll (THIRD_PARTY.md, "Built into the game's DLLs").
  "third_party\rexglue-sdk\thirdparty\FFmpeg\COPYING.LGPLv2.1" = "FFmpeg (LGPL-2.1).txt"
  "third_party\rexglue-sdk\thirdparty\libmspack\libmspack\COPYING.LIB" = "libmspack (LGPL-2.1).txt"
  "third_party\rexglue-sdk\thirdparty\fmt\LICENSE" = "fmt (MIT).txt"
  "third_party\rexglue-sdk\thirdparty\spdlog\LICENSE" = "spdlog (MIT).txt"
  "third_party\rexglue-sdk\thirdparty\tomlplusplus\LICENSE" = "toml++ (MIT).txt"
  "third_party\rexglue-sdk\thirdparty\xxHash\LICENSE" = "xxHash (BSD-2-Clause).txt"
  "third_party\rexglue-sdk\thirdparty\utfcpp\LICENSE" = "utf8-cpp (BSL-1.0).txt"
  "third_party\licenses\disruptorplus-LICENSE.txt" = "disruptorplus (MIT).txt"
  "third_party\rexglue-sdk\thirdparty\o1heap\LICENSE" = "o1heap (MIT).txt"
  "third_party\rexglue-sdk\thirdparty\aes_128\LICENSE" = "aes_128 (MIT).txt"
  "third_party\rexglue-sdk\thirdparty\simde\COPYING" = "SIMDe (MIT).txt"
  "third_party\rexglue-sdk\thirdparty\glslang\LICENSE.txt" = "glslang.txt"
  "third_party\rexglue-sdk\thirdparty\vulkan-memory-allocator\LICENSE.txt" = "Vulkan Memory Allocator (MIT).txt"
  "third_party\rexglue-sdk\thirdparty\vulkan-headers\LICENSE.md" = "Vulkan headers.txt"
  "third_party\rexglue-sdk\thirdparty\spirv-headers\LICENSE" = "SPIR-V headers.txt"
  "third_party\licenses\renderdoc_app-LICENSE.txt" = "RenderDoc API header (MIT).txt"
}
foreach ($k in $Notices.Keys) { Copy-Item "$Root\$k" "$Out\licenses\$($Notices[$k])" }
"stb_image (Sean Barrett): public domain, or MIT License at your choice; see the end of
stb_image.h at https://github.com/nothings/stb" | Set-Content "$Out\licenses\stb_image.txt" -Encoding ascii

$Zip = "$Root\dist\$Name-v$Version-Windows-and-SteamDeck.zip"
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path "$Out\*" -DestinationPath $Zip
Write-Host ("Release: {0} ({1:N0} MB)" -f $Zip, ((Get-Item $Zip).Length / 1MB))
