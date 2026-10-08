# Launch the natively recompiled WWE SmackDown vs. Raw 2009 on Windows (D3D12) and show its log live.
#   powershell -ExecutionPolicy Bypass -File tools\windows\run.ps1 [extra cvars...]
# Frame capture for debugging:  $env:SVR_FRAME_DUMP = "logs\frames"; then run.
# The log is logs\game.log; the previous session's is kept as logs\game.prev.log.
#  --mnk_mode: keyboard as a backup pad; controllers (DualSense etc.) work through SDL.
#  --texture_cache_memory_limit_*: keep textures resident instead of evicting them mid-match
#    (eviction was the prime suspect for a D3D12 GPU page fault ~2 min into matches).
#  --fullscreen=false + window size: windowed for now; pass --fullscreen=true to override.
#  --svr_60fps: matches ask D3D for present interval 2 (30 fps); src/fps_hooks.cpp turns it into 1.
#    TEST: game logic may be tied to frames. Pass --svr_60fps=false for stock 30 fps.
#  --anisotropic_override=5: 16x anisotropic filtering (runtime default 3 = 4x); sharper textures
#    at grazing angles (ring mat, floor, crowd).
#  --gpu_backend=vulkan: default renderer. D3D12 hits a GPU page fault ~1-2 min into matches on the
#    RX 6650 XT (README "Open issue"); Vulkan ran matches without it. --gpu_backend=d3d12 to compare.
# "Diagnose crash (slow).bat" adds --d3d12_debug --d3d12_gpu_validation: the D3D12 debug layer and
# GPU-based validation, with their messages in the log ("D3D12 debug [...]"). Gameplay becomes very
# slow, so it's only for crash-hunting runs.
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
# Game files: this project's own assets\ folder (extracted from your ISO). SDK: .\sdk.
$Main = $Root  # this project keeps its own game files in assets\
$Log = "$Root\logs\game.log"
$Host.UI.RawUI.WindowTitle = "WWE SVR 2009 - log"
New-Item -ItemType Directory -Force "$Root\logs" | Out-Null
if (Test-Path $Log) { Move-Item $Log "$Root\logs\game.prev.log" -Force }

# Extra args override these defaults. The game's parser rejects a setting given twice (and then
# ignores the whole command line), so a default is only added when the caller didn't pass it.
$extra = @($args | ForEach-Object { $_ })  # flatten: a caller may pass a list as a single argument
$given = @{}
foreach ($a in $extra) { if ("$a" -match '^--([^=]+)') { $given[$Matches[1]] = $true } }
$defaults = @(
  "--mnk_mode=true", "--texture_cache_memory_limit_soft=3072", "--texture_cache_memory_limit_hard=4096",
  "--fullscreen=false", "--window_width=1600", "--window_height=900",
  "--gpu_backend=vulkan",
  "--svr_60fps=true",
  "--anisotropic_override=5"
) | Where-Object { -not ($_ -match '^--([^=]+)' -and $given.ContainsKey($Matches[1])) }
$gameArgs = @(
  "`"--game_data_root=$Main\assets`"", "`"--user_data_root=$Root\userdata`"", "`"--log_file=$Log`""
) + @($defaults) + $extra
$game = Start-Process "$Root\out\build\win-amd64-release\svr2009.exe" -ArgumentList $gameArgs -PassThru
Write-Host ("Launching with: " + ($gameArgs -join ' ')) -ForegroundColor DarkGray
Write-Host "WWE SVR 2009 started (pid $($game.Id)). Live log below; closing this window does not close the game.`n" -ForegroundColor Cyan

# Tail the log until the game exits (the game keeps the file open, so share it for reading).
while (-not (Test-Path $Log) -and -not $game.HasExited) { Start-Sleep -Milliseconds 200 }
$reader = $null
if (Test-Path $Log) {
  $stream = [IO.File]::Open($Log, 'Open', 'Read', 'ReadWrite, Delete')
  $reader = New-Object IO.StreamReader($stream)
}
function Show-NewLines {
  if (-not $reader) { return }
  while ($null -ne ($line = $reader.ReadLine())) {
    if ($line -match '\[(error|critical)\]') { Write-Host $line -ForegroundColor Red }
    elseif ($line -match '\[warning\]') { Write-Host $line -ForegroundColor Yellow }
    else { Write-Host $line }
  }
}
while (-not $game.HasExited) { Show-NewLines; Start-Sleep -Milliseconds 250 }
Start-Sleep -Milliseconds 300
Show-NewLines
if ($reader) { $reader.Close() }

$code = $game.ExitCode
if ($code -eq 0) {
  Write-Host "`nGame closed normally." -ForegroundColor Green
} else {
  Write-Host ("`nGame exited with code 0x{0:X8}, probably a crash. Log: $Log" -f $code) -ForegroundColor Red
}
Read-Host "Press Enter to close this window"
