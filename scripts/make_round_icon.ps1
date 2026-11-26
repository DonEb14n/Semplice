# Makes the round application icon from the owner's picture resources\Icon.png:
# a perfectly round white disc (transparent outside, smooth edge) with the picture, cropped to its drawing, inside.
#   resources\AppIcon.ico           10 sizes 16-256 px (32-bit bitmaps with alpha, read by every Windows tool)
#   resources\icons\app\app-<n>.png the same sizes for the window / taskbar icon (embedded in the program)
# Usage: powershell -ExecutionPolicy Bypass -File scripts\make_round_icon.ps1
Add-Type -AssemblyName System.Drawing
$root = Join-Path $PSScriptRoot '..'
$picture = New-Object System.Drawing.Bitmap (Join-Path $root 'resources\Icon.png')

function New-RoundIcon([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.PixelOffsetMode = 'HighQuality'
    $g.CompositingQuality = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($picture, 0, 0, $size, $size)
    $g.Dispose()
    return $bmp
}

# 32-bit DIB frame of an ICO: BITMAPINFOHEADER (height doubled), bottom-up BGRA pixels, empty AND mask
function Get-IconFrame($bmp) {
    $size = $bmp.Width
    $ms = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter $ms
    $w.Write([uint32]40); $w.Write([int32]$size); $w.Write([int32]($size * 2)); $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]0); $w.Write([uint32]($size * $size * 4)); $w.Write([int32]0); $w.Write([int32]0); $w.Write([uint32]0); $w.Write([uint32]0)
    $rect = New-Object System.Drawing.Rectangle 0, 0, $size, $size
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($size * $size * 4)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)
    for ($y = $size - 1; $y -ge 0; $y--) { $w.Write($bytes, $y * $size * 4, $size * 4) }
    $maskRow = [int]([Math]::Ceiling($size / 32.0) * 4)
    $w.Write((New-Object byte[] ($maskRow * $size)))
    $w.Flush()
    return , $ms.ToArray()
}

$sizes = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256
$pngDir = Join-Path $root 'resources\icons\app'
New-Item -ItemType Directory -Force $pngDir | Out-Null
Get-ChildItem $pngDir -Filter 'app-*.png' | ForEach-Object { $_.Delete() }
$frames = @()
foreach ($s in $sizes) {
    $bmp = New-RoundIcon $s
    $bmp.Save((Join-Path $pngDir "app-$s.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $frames += , (Get-IconFrame $bmp)
    $bmp.Dispose()
}
$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $out
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $dim = if ($s -ge 256) { 0 } else { $s }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$frames[$i].Length); $w.Write([uint32]$offset)
    $offset += $frames[$i].Length
}
foreach ($f in $frames) { $w.Write($f) }
[System.IO.File]::WriteAllBytes((Join-Path $root 'resources\AppIcon.ico'), $out.ToArray())
Write-Host 'wrote resources\AppIcon.ico and resources\icons\app\app-*.png'
