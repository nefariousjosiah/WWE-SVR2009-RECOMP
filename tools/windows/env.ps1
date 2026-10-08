# Dot-source for the build environment: VS 2022 x64 (Windows SDK, linker, Ninja) + LLVM clang.
#   . "$PSScriptRoot\env.ps1"
# Sets $Root, $Clang, $ClangXX and defines Run (throws on a non-zero exit code).
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
# Game files: this project's own assets\ folder (extracted from your ISO). SDK: .\sdk.
$Main = $Root  # this project keeps its own game files in assets\
$Clang = "C:\Program Files\LLVM\bin\clang.exe"
$ClangXX = "C:\Program Files\LLVM\bin\clang++.exe"
if (-not (Test-Path $ClangXX)) { throw "LLVM not found at C:\Program Files\LLVM (winget install LLVM.LLVM)" }

if (-not $env:VSCMD_VER) {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $vsPath) { throw "Visual Studio 2022 C++ tools not found (run tools\windows\setup.ps1)" }
  Import-Module "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
  Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
}

function Run($exe, [string[]]$argv) {
  & $exe @argv
  if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" }
}
