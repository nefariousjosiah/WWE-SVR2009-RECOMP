# Build the public release zip: the shared launcher (launcher\ submodule) plus this game's native
# build, WITHOUT any game files. Players add their own disc image.
#   powershell -ExecutionPolicy Bypass -File tools\windows\package_release.ps1 [-Version 1.0]
# Output: dist\WWE-SVR2009-Native-v<version>-Windows-and-SteamDeck.zip (and the unpacked folder).
# Extracting this game's and the other game's release into one folder gives one launcher with both.
param([string]$Version = "dev")
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Id = "svr2009"
$Name = "WWE-SVR2009-Native"
$Build = "$Root\out\build\win-amd64-release"
$Launcher = "$Root\launcher"
if (-not (Test-Path "$Launcher\CMakeLists.txt")) { throw "launcher\ is empty: git submodule update --init --recursive" }
if (-not (Test-Path "$Build\$Id.exe")) { throw "Build the game first: tools\windows\build.ps1" }

& powershell -ExecutionPolicy Bypass -File "$Launcher\tools\build.ps1"
if ($LASTEXITCODE) { throw "launcher build failed" }

$Out = "$Root\dist\$Name-$Version"
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
& powershell -ExecutionPolicy Bypass -File "$Launcher\tools\package.ps1" -Game "$Id=$Build" -Out $Out
if ($LASTEXITCODE) { throw "packaging failed" }

# This game's own licences next to the launcher's (BSD / MIT notices travel with the binaries).
$Lic = "$Out\licenses\$Id"
New-Item -ItemType Directory -Force $Lic | Out-Null
$Notices = @{
  "LICENSE" = "this project (MIT).txt"; "THIRD_PARTY.md" = "THIRD_PARTY.md"
  "src\reblue\LICENSE.reblue" = "re-Blue renderer (BSD-3-Clause).txt"
  "third_party\rexglue-sdk\LICENSE" = "ReXGlue SDK (BSD-3-Clause).txt"
  "third_party\plume\LICENSE" = "plume (MIT).txt"
  "third_party\reblue_thirdparty\zstd\LICENSE" = "zstd (BSD).txt"
}
foreach ($k in $Notices.Keys) { Copy-Item "$Root\$k" "$Lic\$($Notices[$k])" -Force }

$Zip = "$Root\dist\$Name-v$Version-Windows-and-SteamDeck.zip"
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path "$Out\*" -DestinationPath $Zip
Write-Host ("Release: {0} ({1:N0} MB)" -f $Zip, ((Get-Item $Zip).Length / 1MB))
