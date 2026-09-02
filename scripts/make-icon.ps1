# app-icon-src.png -> app.ico
#
# Called by the build whenever the PNG is newer than the .ico, so drawing the
# icon and pressing build is the whole of the work. The .ico is checked in
# because this needs PowerShell and System.Drawing and so cannot run on a
# cross-build; where there is no PowerShell the checked-in file is used as it is.
#
# The source is a sheet of two hand-drawn sizes side by side: the 32x32 at
# (0,0) and the 16x16 at (32,0). Each ships as drawn — pixel art resampled
# "with quality" is pixel art blurred — and the larger entries are the 32
# scaled up by nearest neighbour, which keeps every pixel a crisp square.

param(
  [Parameter(Mandatory = $true)][string]$Sheet,
  [Parameter(Mandatory = $true)][string]$Out
)

# Fail fast: a half-run must not leave a truncated .ico behind looking valid.
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$source = New-Object System.Drawing.Bitmap((Resolve-Path $Sheet).Path)
$art32 = $source.Clone((New-Object System.Drawing.Rectangle(0, 0, 32, 32)),
  [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$art16 = $source.Clone((New-Object System.Drawing.Rectangle(32, 0, 16, 16)),
  [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$source.Dispose()

function Scaled([System.Drawing.Bitmap]$art, [int]$size) {
  $bitmap = New-Object System.Drawing.Bitmap($size, $size,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
  $graphics.InterpolationMode =
    [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
  $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
  $graphics.Clear([System.Drawing.Color]::Transparent)
  $graphics.DrawImage($art, 0, 0, $size, $size)
  $graphics.Dispose()
  return $bitmap
}

# The hand-drawn sizes as drawn; everything else scaled from the nearest one.
$entries = @(
  @(256, (Scaled $art32 256)),
  @(128, (Scaled $art32 128)),
  @(64,  (Scaled $art32 64)),
  @(48,  (Scaled $art32 48)),
  @(32,  $art32),
  @(24,  (Scaled $art16 24)),
  @(16,  $art16)
)

$images = @()
foreach ($entry in $entries) {
  # Each entry is a PNG, which every Windows since Vista reads inside an .ico
  # and which keeps the alpha channel without a mask.
  $stream = New-Object System.IO.MemoryStream
  $entry[1].Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
  $images += ,@($entry[0], $stream.ToArray())
  $entry[1].Dispose()
  $stream.Dispose()
}

$file = [System.IO.File]::Create($Out)
$writer = New-Object System.IO.BinaryWriter($file)
$writer.Write([UInt16]0)                    # reserved
$writer.Write([UInt16]1)                    # type: icon
$writer.Write([UInt16]$images.Count)

# The directory comes first and every entry names an offset, so the offsets are
# only knowable once the header's own length is: 6 bytes plus 16 per entry.
$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
  $size = $image[0]
  $bytes = $image[1]
  # 256 is written as 0 in a single byte, which is what the format says and is
  # the one thing an icon writer usually gets wrong.
  $writer.Write([Byte]($(if ($size -ge 256) { 0 } else { $size })))
  $writer.Write([Byte]($(if ($size -ge 256) { 0 } else { $size })))
  $writer.Write([Byte]0)                    # palette colours: none, it is a PNG
  $writer.Write([Byte]0)                    # reserved
  $writer.Write([UInt16]1)                  # colour planes
  $writer.Write([UInt16]32)                 # bits per pixel
  $writer.Write([UInt32]$bytes.Length)
  $writer.Write([UInt32]$offset)
  $offset += $bytes.Length
}
foreach ($image in $images) { $writer.Write($image[1]) }
$writer.Flush()
$writer.Dispose()
$file.Dispose()

Write-Host "wrote $Out ($($images.Count) sizes)"
