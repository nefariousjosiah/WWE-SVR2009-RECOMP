# Smoke test: launch the game for N seconds, capture guest frames + the real window, summarise the log.
#   powershell -ExecutionPolicy Bypass -File tools\windows\boot_test.ps1 [-Seconds 60] [-Interval 3] [extra cvars...]
# Output: logs\game.log, logs\frames\frame_<t>s.png (what the game rendered),
#         logs\frames\window_<t>s.png (what actually reached the window).
# -Build win-amd64-dev runs the developer build (out\build\win-amd64-dev), leaving the playable one alone.
param([int]$Seconds = 60, [int]$Interval = 3, [string]$Build = "win-amd64-release")
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
# Game files: this project's own assets\ folder (extracted from your ISO). SDK: .\sdk.
$Main = $Root  # this project keeps its own game files in assets\
$Frames = "$Root\logs\frames"
Remove-Item $Frames -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "$Root\logs\game.log" -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Frames | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
}
"@

function Save-Window($proc, $path) {
  $proc.Refresh()
  $h = $proc.MainWindowHandle
  if ($h -eq [IntPtr]::Zero) { return "no window" }
  $r = New-Object Win+RECT
  [void][Win]::GetWindowRect($h, [ref]$r)
  $w = $r.R - $r.L; $hgt = $r.B - $r.T
  if ($w -le 0 -or $hgt -le 0) { return "window minimised" }
  $bmp = New-Object System.Drawing.Bitmap $w, $hgt
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $dc = $g.GetHdc()
  [void][Win]::PrintWindow($h, $dc, 2)  # PW_RENDERFULLCONTENT: includes D3D content, even if occluded
  $g.ReleaseHdc($dc); $g.Dispose()
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $sum = 0L; $n = 0
  for ($y = 0; $y -lt $hgt; $y += 8) { for ($x = 0; $x -lt $w; $x += 8) {
    $c = $bmp.GetPixel($x, $y); $sum += $c.R + $c.G + $c.B; $n++ } }
  $bmp.Dispose()
  return "${w}x${hgt} mean $([int]($sum / [Math]::Max(1, 3 * $n)))"
}

$env:SVR_FRAME_DUMP = $Frames
$env:SVR_FRAME_DUMP_INTERVAL = "$Interval"
# Tests run in a small muted window so they don't take over the screen (the SDK defaults to
# fullscreen); later arguments override these.
$exeArgs = @("--game_data_root=`"$Main\assets`"", "--user_data_root=`"$Root\userdata`"",
             "--log_file=`"$Root\logs\game.log`"", "--fullscreen=false", "--window_width=960",
             "--window_height=540", "--audio_mute=true") + $args
$proc = Start-Process "$Root\out\build\$Build\svr2009.exe" -ArgumentList $exeArgs -PassThru
for ($t = 10; $t -le $Seconds; $t += 10) {
  Start-Sleep -Seconds 10
  if ($proc.HasExited) { Write-Host "exited after ~${t}s with code $($proc.ExitCode)"; break }
  Write-Host "window ${t}s: $(Save-Window $proc "$Frames\window_${t}s.png")"
}
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Write-Host "still running after ${Seconds}s (stopped)" }
Start-Sleep -Seconds 1

# Guest frames: PPM -> PNG
python -c "import glob,os,sys;from PIL import Image;[ (Image.open(p).save(p[:-4]+'.png'), os.remove(p)) for p in glob.glob(sys.argv[1]+'/*.ppm')]" $Frames

$log = "$Root\logs\game.log"
if (Test-Path $log) {
  Write-Host "`n--- guest frames"
  Select-String -Path $log -Pattern "\[frame\]" | ForEach-Object { ($_.Line -replace '^\[[^\]]*\] ', '' -replace '\[t\d+\] ', '' -replace ' -> .*', '') }
  Write-Host "`n--- errors / warnings (top 20)"
  Select-String -Path $log -Pattern "\[(error|critical|warning)\]" | ForEach-Object {
    $_.Line -replace '^\[[^\]]*\] ', '' -replace '\[t\d+\] ', '' -replace '0x[0-9A-Fa-f]+', '0x?' } |
    Group-Object | Sort-Object Count -Descending | Select-Object -First 20 |
    ForEach-Object { "{0,6} {1}" -f $_.Count, $_.Name.Substring(0, [Math]::Min(200, $_.Name.Length)) }
  Write-Host "`n--- fatal"
  Select-String -Path $log -Pattern "FATAL" | ForEach-Object { $_.Line.Substring(0, [Math]::Min(250, $_.Line.Length)) }
}
