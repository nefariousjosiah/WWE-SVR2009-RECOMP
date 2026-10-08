# WWE SmackDown vs. Raw 2009 (native PC version) - one-shot Windows setup for building from source.
# Players don't need this: they download a release (see README.md).
#
# From the project folder, in PowerShell:
#   powershell -ExecutionPolicy Bypass -File tools\windows\setup.ps1 -Iso "D:\your disc image.iso"
#
# Every step is skipped when its output already exists, so it is safe to re-run after a failure.
#  1. Installs tools with winget: Git, CMake, Ninja, LLVM (clang 20+), Python, 7-Zip,
#     Visual Studio 2022 Build Tools (C++ workload, for the Windows SDK + linker libs).
#  2. Fetches the third-party sources at the pinned commits and applies this project's patches
#     (patches\): ReXGlue SDK, plume (renderer backend), XenosRecomp (shader recompiler).
#  3. Builds + installs the SDK into .\sdk (win-amd64, Release).
#  4. Extracts YOUR disc image (iso\ or -Iso) -> assets\.
#  5. Recompiles default.xex -> generated\default.
#  6. Builds the game. Without a shader cache yet (generated\native\shader_cache.cpp, made from
#     your own copy's shaders) it builds the shader-dump version first and tells you what to do.
param(
  [string]$Iso = "",
  [switch]$SkipInstall
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $Root
$Id = "svr2009"
# Pinned third-party commits (patches\ apply on top of exactly these).
$SdkCommit = "c94f5eb"     # ReXGlue SDK v0.10.0
$PlumeCommit = "e0c8871"   # zolaware/plume
$XenosCommit = "339af41"   # zolaware/reblue-XenosRecomp

function Step($msg) { Write-Host "`n=== $msg" -ForegroundColor Cyan }
function Run($exe, [string[]]$argv) {
  & $exe @argv
  if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" }
}
function Refresh-Path {
  $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
              [Environment]::GetEnvironmentVariable("Path", "User")
}
# Clone a repository at a commit with its submodules, then apply our patch to it.
function Fetch($url, $dir, $commit, $patch) {
  if (Test-Path "$dir\CMakeLists.txt") { return }
  Step "Fetching $url ($commit)"
  New-Item -ItemType Directory -Force (Split-Path $dir) | Out-Null
  # core.longpaths: some submodule paths pass 260 chars under a Desktop project folder.
  Run git @("-c", "core.longpaths=true", "clone", $url, $dir)
  Run git @("-C", $dir, "config", "core.longpaths", "true")
  Run git @("-C", $dir, "checkout", $commit)
  Run git @("-c", "core.longpaths=true", "-C", $dir, "submodule", "update", "--init", "--recursive",
            "--depth", "1", "--jobs", "8")
  if ($patch) { Run git @("-C", $dir, "apply", $patch) }
}

# --- 1. Tools ------------------------------------------------------------------
if (-not $SkipInstall) {
  Step "Installing build tools (winget)"
  $packages = @("Git.Git", "Kitware.CMake", "Ninja-build.Ninja", "LLVM.LLVM", "Python.Python.3.12", "7zip.7zip")
  foreach ($pkg in $packages) {
    winget list --id $pkg -e --accept-source-agreements | Out-Null
    if ($LASTEXITCODE -ne 0) {
      Write-Host "installing $pkg"
      winget install --id $pkg -e --accept-package-agreements --accept-source-agreements
    }
  }
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  $haveVs = (Test-Path $vswhere) -and (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
  if (-not $haveVs) {
    Write-Host "installing Visual Studio 2022 Build Tools (C++); this takes a while"
    winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-package-agreements --accept-source-agreements `
      --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
  }
  Refresh-Path
}

# MSVC environment (Windows SDK headers/libs, linker) for clang.
. "$PSScriptRoot\env.ps1"
Set-Location $Root

# --- 2. Third-party sources ----------------------------------------------------
Fetch "https://github.com/rexglue/rexglue-sdk.git" "$Root\third_party\rexglue-sdk" $SdkCommit "$Root\patches\rexglue-sdk.patch"
Fetch "https://github.com/zolaware/plume.git" "$Root\third_party\plume" $PlumeCommit "$Root\patches\plume.patch"
Fetch "https://github.com/zolaware/reblue-XenosRecomp.git" "$Root\third_party\XenosRecomp" $XenosCommit "$Root\patches\xenosrecomp.patch"

# --- 3. SDK build --------------------------------------------------------------
if (-not (Test-Path "$Root\sdk\bin\rexglue.exe")) {
  Step "Building ReXGlue SDK (win-amd64 Release) -> sdk\"
  Push-Location "$Root\third_party\rexglue-sdk"
  Run cmake @("--preset", "win-amd64", "-DREXGLUE_USE_VULKAN=ON",
              "-DCMAKE_C_COMPILER=$Clang", "-DCMAKE_CXX_COMPILER=$ClangXX",
              "-DCMAKE_INSTALL_PREFIX=$Root\sdk")
  Run cmake @("--build", "out\build\win-amd64", "--config", "Release", "--target", "install", "--parallel")
  Pop-Location
}

# --- 4. Game data (your own disc) ----------------------------------------------
if (-not (Test-Path "$Root\assets\default.xex")) {
  Step "Extracting your disc image"
  if (-not $Iso) {
    $Iso = (Get-ChildItem "$Root\iso\*.iso" -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
  }
  if (-not $Iso) { throw "Disc image not found: put your .iso in the iso\ folder or pass -Iso <path>" }
  Run python @("$Root\tools\xiso_extract.py", $Iso, "$Root\assets")
}

# --- 5. Codegen ----------------------------------------------------------------
if (-not (Test-Path "$Root\generated\default\sources.cmake")) {
  Step "Recompiling default.xex -> generated\default"
  New-Item -ItemType Directory -Force "$Root\logs" | Out-Null
  Run "$Root\sdk\bin\rexglue.exe" @("--log-file", "$Root\logs\codegen.log", "codegen", "${Id}_manifest.toml")
}

# --- 6. Game build -------------------------------------------------------------
if (Test-Path "$Root\generated\native\shader_cache.cpp") {
  Step "Building $Id.exe (native renderer)"
  Run powershell @("-ExecutionPolicy", "Bypass", "-File", "$PSScriptRoot\build.ps1")
  Step "Done. Play with:  tools\windows\run.ps1   (or package a release: tools\windows\package_release.ps1)"
} else {
  Step "Building the shader-dump version (no shader cache yet)"
  $env:SVR_D3D_CENSUS = "ON"
  Run powershell @("-ExecutionPolicy", "Bypass", "-File", "$PSScriptRoot\build.ps1")
  Remove-Item Env:SVR_D3D_CENSUS
  Write-Host @"

The native renderer needs your game's shaders, which aren't in this repository. Collect them once:
  1. Run the game with shader dumping on (it uses the emulated GPU this time):
       `$env:SVR_DUMP_SHADERS = "logs\shaders"
       powershell -ExecutionPolicy Bypass -File tools\windows\run.ps1 --gpu_backend=vulkan
     Go through the menus and play a match or two (more variety = more shaders), then quit.
  2. powershell -ExecutionPolicy Bypass -File tools\native\build_shader_cache.ps1
  3. Run this setup again: it builds the native renderer.
"@ -ForegroundColor Yellow
}
