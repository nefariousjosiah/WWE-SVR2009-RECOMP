# Recompile default.xex -> generated\default. Extra args go to `rexglue codegen` (e.g. --ignore-stamp).
#   powershell -ExecutionPolicy Bypass -File tools\windows\regen.ps1
#   $env:FORCE = 1   # generate despite validation errors
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $Root
New-Item -ItemType Directory -Force logs | Out-Null
Remove-Item logs\codegen.log -ErrorAction SilentlyContinue  # --log-file appends
$force = if ($env:FORCE) { @("--force") } else { @() }
& "$Root\sdk\bin\rexglue.exe" @force --log-file logs\codegen.log codegen svr2009_manifest.toml @args
exit $LASTEXITCODE
