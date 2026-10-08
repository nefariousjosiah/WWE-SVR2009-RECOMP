# Startup window: pick render resolution and display mode, then launch through run.ps1.
#   powershell -ExecutionPolicy Bypass -File tools\windows\launcher.ps1 [extra cvars...]
# The game renders at 1280x720; --resolution_scale renders the 3D at an integer multiple of that
# (2 = 2560x1440, 3 = 3840x2160) and the result is scaled to the window. Last choice is saved in
# userdata\launcher_settings.json.
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$SettingsFile = "$Root\userdata\launcher_settings.json"

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Dpi { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); }
"@
[void][Dpi]::SetProcessDPIAware()  # real pixel sizes for the monitor, and a crisp window
[System.Windows.Forms.Application]::EnableVisualStyles()

# Monitor size in physical pixels; the 16:9 picture fills at most min(height, width * 9 / 16).
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$fitHeight = [Math]::Min($screen.Height, [int]($screen.Width * 9 / 16))
$autoScale = [Math]::Max(1, [Math]::Min(7, [int][Math]::Ceiling($fitHeight / 720.0)))

$resolutions = @(
  @{ Label = "Auto: match this monitor ($($screen.Width) x $($screen.Height), renders $(1280 * $autoScale) x $(720 * $autoScale))"; Scale = 0 },
  @{ Label = "720p: original (1280 x 720)"; Scale = 1 },
  @{ Label = "1440p (2560 x 1440)"; Scale = 2 },
  @{ Label = "4K (3840 x 2160)"; Scale = 3 },
  @{ Label = "5K (5120 x 2880)"; Scale = 4 }
)
$displays = @("Windowed", "Fullscreen")

$saved = $null
try { $saved = Get-Content $SettingsFile -Raw | ConvertFrom-Json } catch {}

$form = New-Object System.Windows.Forms.Form
$form.Text = "WWE SVR 2009 (0.1)"
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox = $false
$form.MinimizeBox = $false
$form.StartPosition = "CenterScreen"
$form.AutoScaleMode = "Dpi"
$form.ClientSize = New-Object System.Drawing.Size(460, 230)
$form.Font = New-Object System.Drawing.Font("Segoe UI", 10)

function Add-Label($text, $y) {
  $label = New-Object System.Windows.Forms.Label
  $label.Text = $text
  $label.Location = New-Object System.Drawing.Point(20, $y)
  $label.AutoSize = $true
  $form.Controls.Add($label)
}
function Add-Combo($items, $y) {
  $combo = New-Object System.Windows.Forms.ComboBox
  $combo.DropDownStyle = "DropDownList"
  $combo.Location = New-Object System.Drawing.Point(20, $y)
  $combo.Width = 420
  foreach ($item in $items) { [void]$combo.Items.Add($item) }
  $form.Controls.Add($combo)
  return $combo
}

Add-Label "Render resolution" 15
$resCombo = Add-Combo ($resolutions | ForEach-Object { $_.Label }) 40
Add-Label "Display" 80
$dispCombo = Add-Combo $displays 105

$resIndex = 0
if ($saved) {
  for ($i = 0; $i -lt $resolutions.Count; $i++) { if ($resolutions[$i].Scale -eq $saved.scale) { $resIndex = $i } }
}
$resCombo.SelectedIndex = $resIndex
$dispCombo.SelectedIndex = if ($saved -and $saved.display -eq "Fullscreen") { 1 } else { 0 }

$note = New-Object System.Windows.Forms.Label
$note.Text = "Higher resolutions are sharper but need more GPU power."
$note.ForeColor = [System.Drawing.Color]::DimGray
$note.Location = New-Object System.Drawing.Point(20, 145)
$note.AutoSize = $true
$form.Controls.Add($note)

$play = New-Object System.Windows.Forms.Button
$play.Text = "Play"
$play.Location = New-Object System.Drawing.Point(250, 180)
$play.Size = New-Object System.Drawing.Size(90, 32)
$play.DialogResult = [System.Windows.Forms.DialogResult]::OK
$form.AcceptButton = $play
$form.Controls.Add($play)

$cancel = New-Object System.Windows.Forms.Button
$cancel.Text = "Cancel"
$cancel.Location = New-Object System.Drawing.Point(350, 180)
$cancel.Size = New-Object System.Drawing.Size(90, 32)
$cancel.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
$form.CancelButton = $cancel
$form.Controls.Add($cancel)

if ($env:SVR_LAUNCHER_PREVIEW) {
  # Development aid: save a picture of the window instead of showing it (shown off-screen).
  $form.StartPosition = "Manual"
  $form.Location = New-Object System.Drawing.Point(-3000, -3000)
  $form.ShowInTaskbar = $false
  $form.Show()
  [System.Windows.Forms.Application]::DoEvents()
  $bmp = New-Object System.Drawing.Bitmap $form.Width, $form.Height
  $form.DrawToBitmap($bmp, (New-Object System.Drawing.Rectangle 0, 0, $form.Width, $form.Height))
  $bmp.Save($env:SVR_LAUNCHER_PREVIEW, [System.Drawing.Imaging.ImageFormat]::Png)
  exit 0
}

$form.Topmost = $true
if ($form.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 0 }

$choice = $resolutions[$resCombo.SelectedIndex]
$display = $displays[$dispCombo.SelectedIndex]
$scale = if ($choice.Scale -eq 0) { $autoScale } else { $choice.Scale }

New-Item -ItemType Directory -Force (Split-Path $SettingsFile) | Out-Null
@{ scale = $choice.Scale; display = $display } | ConvertTo-Json | Set-Content $SettingsFile -Encoding ascii

# Windowed: 16:9 window at three quarters of the monitor width.
$winWidth = [int]($screen.Width * 0.75)
$winHeight = [int]($winWidth * 9 / 16)
$gameArgs = @("--resolution_scale=$scale")
if ($display -eq "Fullscreen") {
  $gameArgs += "--fullscreen=true"
} else {
  $gameArgs += @("--fullscreen=false", "--window_width=$winWidth", "--window_height=$winHeight")
}
Write-Host ("Render {0} x {1} ({2}x), {3}" -f (1280 * $scale), (720 * $scale), $scale, $display) -ForegroundColor Cyan
# Splatting needs a plain variable; `@(...)` would pass the whole list as one argument.
$runArgs = @($gameArgs) + @($args)
& "$PSScriptRoot\run.ps1" @runArgs
