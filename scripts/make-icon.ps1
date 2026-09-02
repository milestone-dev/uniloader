# app-icon-src.png -> app.ico
#
# Called by the build whenever the PNG is newer than the .ico, so drawing the
# icon and pressing build is the whole of the work. The .ico is checked in
# because this needs PowerShell and System.Drawing and so cannot run on a
# cross-build; where there is no PowerShell the checked-in file is used as it is.
#
# Every standard size is written, each resampled from the one source with high
# quality: an .ico holding only 256x256 is scaled by the shell for the taskbar
# and the result is mush at 16x16, which is where most people will see it.

param(
  [Parameter(Mandatory = $true)][string]$Sheet,
  [Parameter(Mandatory = $true)][string]$Out
)

Add-Type -AssemblyName System.Drawing

$source = [System.Drawing.Image]::FromFile((Resolve-Path $Sheet))
$sizes = @(256, 128, 64, 48, 32, 24, 16)

$images = @()
foreach ($size in $sizes) {
  $bitmap = New-Object System.Drawing.Bitmap($size, $size,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
  $graphics.InterpolationMode =
    [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  $graphics.Clear([System.Drawing.Color]::Transparent)
  $graphics.DrawImage($source, 0, 0, $size, $size)
  $graphics.Dispose()

  # Each entry is a PNG, which every Windows since Vista reads inside an .ico
  # and which keeps the alpha channel without a mask.
  $stream = New-Object System.IO.MemoryStream
  $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
  $images += ,@($size, $stream.ToArray())
  $bitmap.Dispose()
  $stream.Dispose()
}
$source.Dispose()

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
