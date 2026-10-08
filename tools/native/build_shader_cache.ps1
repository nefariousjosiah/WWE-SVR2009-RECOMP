# Build the native renderer's game shader cache: XenosRecomp over the shaders the game created.
#  1. Dump the shaders: a -DSVR_D3D_CENSUS=ON build run with SVR_DUMP_SHADERS=logs\shaders,
#     played through the menus and a match (tools/windows/setup.ps1 prints the steps).
#  2. powershell -ExecutionPolicy Bypass -File tools\native\build_shader_cache.ps1
# Builds the patched XenosRecomp (third_party/XenosRecomp, -DREBLUE_RECOMP) into out\xenosrecomp,
# converts logs\shaders into generated\native\shader_cache.cpp and leaves the HLSL of every shader
# in logs\shader_hlsl for debugging.
param([string]$Shaders = "logs\shaders")
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\..\windows\env.ps1"
Set-Location $Root
if (-not (Get-ChildItem $Shaders -File -ErrorAction SilentlyContinue)) { throw "no shaders in $Shaders (see step 1)" }

$Src = "$Root\third_party\XenosRecomp"
$Out = "$Root\out\xenosrecomp"
Run cmake @("-S", $Src, "-B", $Out, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_C_COMPILER=$Clang", "-DCMAKE_CXX_COMPILER=$ClangXX", "-DCMAKE_CXX_FLAGS=-DREBLUE_RECOMP")
Run cmake @("--build", $Out, "--target", "XenosRecomp")

New-Item -ItemType Directory -Force "generated\native", "logs\shader_hlsl" | Out-Null
Run "$Out\XenosRecomp\XenosRecomp.exe" @($Shaders, "generated\native\shader_cache.cpp",
                                          "$Src\XenosRecomp\shader_common.h", "logs\shader_hlsl")
