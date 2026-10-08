# Build the launcher (Release) into out\launcher\Launcher.exe.
#   powershell -ExecutionPolicy Bypass -File launcher\build.ps1
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\..\tools\windows\env.ps1"
Set-Location $Root
Run cmake @("-S", "launcher", "-B", "out\launcher", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_C_COMPILER=$Clang", "-DCMAKE_CXX_COMPILER=$ClangXX")
Run cmake @("--build", "out\launcher")
