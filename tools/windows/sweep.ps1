# Re-derive config/pointer_funcs.toml on Windows: pointer-referenced code the analyzer missed
# (tools/sweep.sh does the same on macOS with Ghidra).
#  1. Launch the game once with SVR_DUMP_IMAGE=logs\default_image.bin (boot_test.ps1 is enough).
#  2. powershell -ExecutionPolicy Bypass -File tools\windows\sweep.ps1
# Steps: scan the image for candidate entries, codegen without the sweep to learn what analysis
# finds on its own, add the rest, then prune any that split a function until codegen is clean.
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $Root
$Image = "logs\default_image.bin"
if (-not (Test-Path $Image)) { throw "$Image missing: launch the game once with SVR_DUMP_IMAGE=$Image" }

$targets = python tools\native\scan_pointer_targets.py $Image
if ($LASTEXITCODE) { throw "scan_pointer_targets.py failed" }
Set-Content -Path logs\pointer_targets.txt -Value $targets -Encoding ascii

function Regen {
  $ErrorActionPreference = "Continue"  # codegen fails validation until the sweep is clean
  & powershell -ExecutionPolicy Bypass -File "$PSScriptRoot\regen.ps1" --ignore-stamp *> $null
}
function Write-Funcs($argv) {
  $out = python tools\pointer_funcs.py @argv
  if ($LASTEXITCODE) { throw "pointer_funcs.py failed" }
  Set-Content -Path config\pointer_funcs.toml -Value $out -Encoding ascii
}

Set-Content -Path config\pointer_funcs.toml -Value "[functions]" -Encoding ascii
Regen
Write-Funcs @("logs\pointer_targets.txt")
for ($pass = 1; $pass -le 6; $pass++) {
  Regen
  if (-not (Select-String -Path logs\codegen.log -Pattern "Unresolved" -Quiet)) {
    $n = (Select-String -Path config\pointer_funcs.toml -Pattern "^0x").Count
    Write-Host "sweep: clean after pass $pass ($n entries)"
    exit 0
  }
  Write-Funcs @("logs\pointer_targets.txt", "--prune", "logs\codegen.log")
}
Write-Host "sweep: still failing after 6 passes; see logs\codegen.log"
exit 1
