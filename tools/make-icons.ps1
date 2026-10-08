<#
.SYNOPSIS
    ChildrenBlock 아이콘(.ico) 생성기

.DESCRIPTION
    둥근 사각형 타일 위에 흰색 시계를 그린 아이콘을 여러 크기(16~256px)로 그려
    src\res\*.ico 로 저장한다. 타일 색만 바꿔 트레이 상태 아이콘도 함께 만든다.
    디자인을 바꾸면 이 스크립트를 다시 실행한 뒤 빌드하면 된다.

    powershell -ExecutionPolicy Bypass -File tools\make-icons.ps1
#>
param(
    [string]$OutDir = (Join-Path $PSScriptRoot '..\src\res'),
    [switch]$Preview  # 미리보기 PNG 도 저장
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# 이름 = 타일 위쪽 색, 아래쪽 색
$variants = [ordered]@{
    'ChildrenBlock' = @('#6E9BF0', '#3F6FD8')  # 프로그램 아이콘 / 사용 중
    'TrayBreakSoon' = @('#F5BD5C', '#E39A2B')  # 휴식 임박
    'TrayBreak'     = @('#8C96A6', '#5F6978')  # 휴식 중
    'TrayShutdown'  = @('#F07575', '#D64545')  # 종료 임박
}
$sizes = 16, 20, 24, 32, 40, 48, 64, 256

function New-RoundedRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

function New-IconBitmap([int]$s, [string]$top, [string]$bottom) {
    $bmp = New-Object Drawing.Bitmap $s, $s, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([Drawing.Color]::Transparent)

    # 타일: 작은 크기는 여백 없이 꽉 채워 또렷하게
    $inset = if ($s -ge 48) { $s * 0.06 } else { 0 }
    $tile = $s - 2 * $inset
    $path = New-RoundedRect $inset $inset $tile $tile ($tile * 0.24)
    $rect = New-Object Drawing.RectangleF $inset, $inset, $tile, $tile
    $brush = New-Object Drawing.Drawing2D.LinearGradientBrush $rect, `
        ([Drawing.ColorTranslator]::FromHtml($top)), ([Drawing.ColorTranslator]::FromHtml($bottom)), 90.0
    $g.FillPath($brush, $path)

    # 시계: 원 + 바늘(12시, 3시 방향)
    $c = $s / 2.0
    $r = $tile * 0.29
    $stroke = [Math]::Max(1.6, $tile * 0.085)
    $pen = New-Object Drawing.Pen ([Drawing.Color]::White), $stroke
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    $g.DrawEllipse($pen, [float]($c - $r), [float]($c - $r), [float](2 * $r), [float](2 * $r))
    $g.DrawLine($pen, [float]$c, [float]$c, [float]$c, [float]($c - $r * 0.58))
    $g.DrawLine($pen, [float]$c, [float]$c, [float]($c + $r * 0.42), [float]$c)

    $pen.Dispose(); $brush.Dispose(); $path.Dispose(); $g.Dispose()
    return $bmp
}

# 256px 는 PNG, 그보다 작은 크기는 호환성을 위해 32비트 DIB(BMP) 로 저장
function Get-IcoFrame([Drawing.Bitmap]$img) {
    $ms = New-Object IO.MemoryStream
    if ($img.Width -ge 256) {
        $img.Save($ms, [Drawing.Imaging.ImageFormat]::Png)
        return , $ms.ToArray()
    }
    $s = $img.Width
    $maskStride = [int][Math]::Floor(($s + 31) / 32) * 4
    $w = New-Object IO.BinaryWriter $ms
    $w.Write([UInt32]40); $w.Write([Int32]$s); $w.Write([Int32]($s * 2))
    $w.Write([UInt16]1); $w.Write([UInt16]32); $w.Write([UInt32]0)
    $w.Write([UInt32]($s * $s * 4 + $maskStride * $s))
    $w.Write([Int32]0); $w.Write([Int32]0); $w.Write([UInt32]0); $w.Write([UInt32]0)

    $rect = New-Object Drawing.Rectangle 0, 0, $s, $s
    $data = $img.LockBits($rect, 'ReadOnly', ([Drawing.Imaging.PixelFormat]::Format32bppArgb))
    $row = New-Object byte[] ($s * 4)
    for ($y = $s - 1; $y -ge 0; $y--) {  # DIB 는 아래 줄부터
        [Runtime.InteropServices.Marshal]::Copy([IntPtr]($data.Scan0.ToInt64() + $y * $data.Stride), $row, 0, $s * 4)
        $w.Write($row)
    }
    $img.UnlockBits($data)
    $w.Write((New-Object byte[] ($maskStride * $s)))  # AND 마스크: 알파 채널을 쓰므로 0
    $w.Flush()
    return , $ms.ToArray()
}

function Save-Ico([Drawing.Bitmap[]]$images, [string]$path) {
    $pngs = foreach ($img in $images) { , (Get-IcoFrame $img) }
    $fs = [IO.File]::Create($path)
    $w = New-Object IO.BinaryWriter $fs
    $w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]$images.Count)
    $offset = 6 + 16 * $images.Count
    for ($i = 0; $i -lt $images.Count; $i++) {
        $dim = if ($images[$i].Width -ge 256) { 0 } else { $images[$i].Width }
        $w.Write([byte]$dim); $w.Write([byte]$dim)
        $w.Write([byte]0); $w.Write([byte]0)
        $w.Write([UInt16]1); $w.Write([UInt16]32)
        $w.Write([UInt32]$pngs[$i].Length); $w.Write([UInt32]$offset)
        $offset += $pngs[$i].Length
    }
    foreach ($png in $pngs) { $w.Write($png) }
    $w.Close()
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
foreach ($name in $variants.Keys) {
    $colors = $variants[$name]
    $images = foreach ($s in $sizes) { New-IconBitmap $s $colors[0] $colors[1] }
    $ico = Join-Path $OutDir "$name.ico"
    Save-Ico $images $ico
    Write-Host "생성: $ico"

    if ($Preview) {
        $images[-1].Save((Join-Path $OutDir "$name-256.png"), [Drawing.Imaging.ImageFormat]::Png)
    }
    $images | ForEach-Object { $_.Dispose() }
}
