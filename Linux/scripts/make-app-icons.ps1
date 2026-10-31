# Linux app icons (Linux/PLAN.md section 9): the phone launcher icon with the
# same 22.5% rounded corners as Windows (work/windows-phone-design/rounded_icon.ps1),
# as hicolor PNGs at 16-512 px. Run once on Windows; the PNGs are committed and
# the build never regenerates them.
param(
  [string]$Source = (Join-Path $PSScriptRoot '..\..\app\src\main\res\drawable-nodpi\sukut_app_icon.png'),
  [string]$Out = (Join-Path $PSScriptRoot '..\data\icons\hicolor'),
  [double]$Radius = 0.225
)
Add-Type -AssemblyName System.Drawing
$image = [Drawing.Image]::FromFile((Resolve-Path $Source))
foreach ($size in 16, 22, 24, 32, 48, 64, 128, 256, 512) {
  $bitmap = New-Object Drawing.Bitmap $size, $size, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [Drawing.Graphics]::FromImage($bitmap)
  $g.SmoothingMode = 'AntiAlias'; $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'; $g.CompositingQuality = 'HighQuality'
  $g.Clear([Drawing.Color]::Transparent)
  $scaled = New-Object Drawing.Bitmap $size, $size
  $sg = [Drawing.Graphics]::FromImage($scaled); $sg.InterpolationMode = 'HighQualityBicubic'; $sg.PixelOffsetMode = 'HighQuality'
  $sg.DrawImage($image, 0, 0, $size, $size); $sg.Dispose()
  $brush = New-Object Drawing.TextureBrush $scaled
  $r = [Math]::Max(2.0, $size * $Radius); $d = 2 * $r; $e = $size
  $path = New-Object Drawing.Drawing2D.GraphicsPath
  $path.AddArc(0, 0, $d, $d, 180, 90); $path.AddArc($e - $d, 0, $d, $d, 270, 90)
  $path.AddArc($e - $d, $e - $d, $d, $d, 0, 90); $path.AddArc(0, $e - $d, $d, $d, 90, 90); $path.CloseFigure()
  $g.FillPath($brush, $path)
  $dir = Join-Path $Out "${size}x${size}\apps"
  New-Item -ItemType Directory -Force $dir | Out-Null
  $bitmap.Save((Join-Path $dir 'io.github.altarafiya.Sukut.png'), [Drawing.Imaging.ImageFormat]::Png)
  $path.Dispose(); $brush.Dispose(); $scaled.Dispose(); $g.Dispose(); $bitmap.Dispose()
}
$image.Dispose()
"app icons written to $Out"
