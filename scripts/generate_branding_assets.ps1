param(
    [string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) "branding")
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

function New-RoundedRectanglePath {
    param(
        [float]$X,
        [float]$Y,
        [float]$Width,
        [float]$Height,
        [float]$Radius
    )

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $diameter = $Radius * 2.0
    $path.AddArc($X, $Y, $diameter, $diameter, 180, 90)
    $path.AddArc($X + $Width - $diameter, $Y, $diameter, $diameter, 270, 90)
    $path.AddArc($X + $Width - $diameter, $Y + $Height - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($X, $Y + $Height - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function Draw-AuraMark {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$Size
    )

    $Graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $Graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $Graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $Graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $Graphics.Clear([System.Drawing.Color]::Transparent)

    $backgroundPath = New-RoundedRectanglePath -X ($Size * 0.047) -Y ($Size * 0.047) -Width ($Size * 0.906) -Height ($Size * 0.906) -Radius ($Size * 0.219)
    $backgroundBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF ($Size * 0.08), ($Size * 0.08)),
        (New-Object System.Drawing.PointF ($Size * 0.92), ($Size * 0.92)),
        ([System.Drawing.ColorTranslator]::FromHtml("#111827")),
        ([System.Drawing.ColorTranslator]::FromHtml("#020617"))
    )
    $Graphics.FillPath($backgroundBrush, $backgroundPath)

    $ringPen = New-Object System.Drawing.Pen ([System.Drawing.ColorTranslator]::FromHtml("#123047"), ($Size * 0.035))
    $ringPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $ringPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $Graphics.DrawEllipse($ringPen, $Size * 0.234, $Size * 0.234, $Size * 0.531, $Size * 0.531)

    $orbitPen = New-Object System.Drawing.Pen ([System.Drawing.ColorTranslator]::FromHtml("#1E3A5F"), ($Size * 0.035))
    $orbitPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $orbitPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $orbitPen.Color = [System.Drawing.Color]::FromArgb(150, $orbitPen.Color)
    $Graphics.DrawArc($orbitPen, $Size * 0.234, $Size * 0.234, $Size * 0.531, $Size * 0.531, 198, 122)

    $glowPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(110, 103, 232, 249), ($Size * 0.055))
    $glowPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $glowPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $Graphics.DrawArc($glowPen, $Size * 0.234, $Size * 0.234, $Size * 0.531, $Size * 0.531, 178, 140)

    $primaryPen = New-Object System.Drawing.Pen ([System.Drawing.ColorTranslator]::FromHtml("#67E8F9"), ($Size * 0.055))
    $primaryPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $primaryPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $Graphics.DrawArc($primaryPen, $Size * 0.234, $Size * 0.234, $Size * 0.531, $Size * 0.531, 178, 140)

    $accentPen = New-Object System.Drawing.Pen ([System.Drawing.ColorTranslator]::FromHtml("#F59E0B"), ($Size * 0.035))
    $accentPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $accentPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $Graphics.DrawArc($accentPen, $Size * 0.234, $Size * 0.234, $Size * 0.531, $Size * 0.531, 342, 128)

    $aPen = New-Object System.Drawing.Pen ([System.Drawing.ColorTranslator]::FromHtml("#F8FAFC"), ($Size * 0.066))
    $aPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $aPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $aPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $Graphics.DrawLine($aPen, $Size * 0.363, $Size * 0.676, $Size * 0.5, $Size * 0.313)
    $Graphics.DrawLine($aPen, $Size * 0.5, $Size * 0.313, $Size * 0.637, $Size * 0.676)

    $barPen = New-Object System.Drawing.Pen ([System.Drawing.ColorTranslator]::FromHtml("#F8FAFC"), ($Size * 0.055))
    $barPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $barPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $Graphics.DrawLine($barPen, $Size * 0.418, $Size * 0.543, $Size * 0.582, $Size * 0.543)

    $dotBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml("#F59E0B"))
    $dotRadius = $Size * 0.031
    $Graphics.FillEllipse($dotBrush, $Size * 0.727, $Size * 0.345, $dotRadius * 2, $dotRadius * 2)

    $haloPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(65, 245, 158, 11), ($Size * 0.02))
    $Graphics.DrawEllipse($haloPen, $Size * 0.707, $Size * 0.325, $Size * 0.102, $Size * 0.102)

    $haloPen.Dispose()
    $dotBrush.Dispose()
    $barPen.Dispose()
    $aPen.Dispose()
    $accentPen.Dispose()
    $primaryPen.Dispose()
    $glowPen.Dispose()
    $orbitPen.Dispose()
    $ringPen.Dispose()
    $backgroundBrush.Dispose()
    $backgroundPath.Dispose()
}

function Get-PngBytes {
    param([int]$Size)

    $bitmap = New-Object System.Drawing.Bitmap $Size, $Size
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)

    try {
        Draw-AuraMark -Graphics $graphics -Size $Size
        $stream = New-Object System.IO.MemoryStream
        try {
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            [byte[]]$payload = $stream.ToArray()
            return $payload
        } finally {
            $stream.Dispose()
        }
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Save-AuraIcon {
    param([string]$DestinationPath)

    $bitmap = New-Object System.Drawing.Bitmap 256, 256
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)

    try {
        Draw-AuraMark -Graphics $graphics -Size 256
        $icon = [System.Drawing.Icon]::FromHandle($bitmap.GetHicon())
        try {
            $stream = [System.IO.File]::Create($DestinationPath)
            try {
                $icon.Save($stream)
            } finally {
                $stream.Dispose()
            }
        } finally {
            $icon.Dispose()
        }
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

if (-not (Test-Path $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}

[byte[]]$previewBytes = Get-PngBytes -Size 512
[System.IO.File]::WriteAllBytes((Join-Path $OutputDirectory "aura-mark-512.png"), $previewBytes)
Save-AuraIcon -DestinationPath (Join-Path $OutputDirectory "aura.ico")

Write-Host "Generated branding assets in $OutputDirectory"
