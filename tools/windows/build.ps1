# Configure + build svr2009.exe. Codegen re-runs as part of the build when its inputs change.
#   powershell -ExecutionPolicy Bypass -File tools\windows\build.ps1 [extra cmake --build args]
#   $env:PRESET = "win-amd64-relwithdebinfo"   # default: win-amd64-release
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\env.ps1"
Set-Location $Root
$Preset = if ($env:PRESET) { $env:PRESET } else { "win-amd64-release" }
# The renderer choice is passed every time: if CMake ever drops the cache (it does when a cached
# compiler path changes), the build must not fall back to the emulated GPU unnoticed.
#   $env:SVR_D3D_CENSUS = "ON"   # developer census / shader dump build (emulated GPU)
$Census = if ($env:SVR_D3D_CENSUS -eq "ON") { "ON" } else { "OFF" }
$Native = if ($Census -eq "ON") { "OFF" } else { "ON" }
Run cmake @("--preset", $Preset, "-DCMAKE_PREFIX_PATH=$Root\sdk", "-DCMAKE_CXX_COMPILER=$ClangXX",
            "-DSVR_NATIVE_RENDERER=$Native", "-DSVR_D3D_CENSUS=$Census")
Run cmake (@("--build", "out\build\$Preset") + $args)
# CMake stages the SDK DLLs only when svr2009.exe relinks; resync after an SDK-only rebuild.
Copy-Item "$Root\sdk\bin\*.dll" "$Root\out\build\$Preset\" -Force
# The Direct3D 12 program, in a folder of its own (the renderer's backend is chosen at compile
# time; src/graphics_api.h). Releases ship it as svr2009.exe and the Vulkan one as
# svr2009_vulkan.exe.
#   $env:SVR_SKIP_D3D12 = "1"   # Vulkan only (faster iteration)
if ($Native -eq "ON" -and $env:SVR_SKIP_D3D12 -ne "1") {
  $D3D12 = "$Preset-d3d12"
  Run cmake @("--preset", $Preset, "-B", "out\build\$D3D12", "-DCMAKE_PREFIX_PATH=$Root\sdk",
              "-DCMAKE_CXX_COMPILER=$ClangXX", "-DSVR_NATIVE_RENDERER=ON", "-DSVR_D3D_CENSUS=OFF",
              "-DSVR_D3D12=ON")
  Run cmake (@("--build", "out\build\$D3D12") + $args)
  Copy-Item "$Root\sdk\bin\*.dll" "$Root\out\build\$D3D12\" -Force
}
