param([string]$DataPath, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$model = Get-Content -LiteralPath $DataPath -Raw -Encoding UTF8 | ConvertFrom-Json
[void][System.IO.Directory]::CreateDirectory($OutputDirectory)
$ink = '#1D2B3A'; $muted = '#4C6072'; $blue = '#24597C'; $line = '#93AABD'
function ColorOf([string]$v) { [System.Drawing.ColorTranslator]::FromHtml($v) }
function BeginFigure([int]$width,[int]$height,[string]$name) {
  $script:figureName=$name
  $script:bitmap=[System.Drawing.Bitmap]::new($width,$height)
  $script:g=[System.Drawing.Graphics]::FromImage($script:bitmap)
  $script:g.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $script:g.TextRenderingHint=[System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
  $script:g.Clear([System.Drawing.Color]::White)
  $script:svg=[System.Collections.Generic.List[string]]::new()
  $script:svg.Add('<svg xmlns="http://www.w3.org/2000/svg" width="'+$width+'" height="'+$height+'" viewBox="0 0 '+$width+' '+$height+'">')
  $script:svg.Add('<rect width="100%" height="100%" fill="white"/>')
}
function TextAt([string]$text,[float]$x,[float]$y,[float]$size,[string]$color,[float]$maxWidth=2100) {
  $font=[System.Drawing.Font]::new('Microsoft YaHei',$size,[System.Drawing.FontStyle]::Regular,[System.Drawing.GraphicsUnit]::Pixel)
  $brush=[System.Drawing.SolidBrush]::new((ColorOf $color))
  $measured=$script:g.MeasureString($text,$font)
  if ($measured.Width -gt $maxWidth) { throw ('Text exceeds reserved width: '+$text+' ('+$measured.Width+' > '+$maxWidth+')') }
  $script:g.DrawString($text,$font,$brush,$x,$y)
  $esc=[System.Security.SecurityElement]::Escape($text)
  $script:svg.Add('<text x="'+$x+'" y="'+($y+$size)+'" fill="'+$color+'" font-family="Microsoft YaHei, sans-serif" font-size="'+$size+'">'+$esc+'</text>')
  $brush.Dispose(); $font.Dispose()
}
function Box([float]$x,[float]$y,[float]$w,[float]$h,[string]$fill,[string]$stroke) {
  $b=[System.Drawing.SolidBrush]::new((ColorOf $fill)); $p=[System.Drawing.Pen]::new((ColorOf $stroke),2)
  $script:g.FillRectangle($b,$x,$y,$w,$h); $script:g.DrawRectangle($p,$x,$y,$w,$h)
  $script:svg.Add('<rect x="'+$x+'" y="'+$y+'" width="'+$w+'" height="'+$h+'" fill="'+$fill+'" stroke="'+$stroke+'" stroke-width="2"/>')
  $b.Dispose(); $p.Dispose()
}
function Line([float]$x1,[float]$y1,[float]$x2,[float]$y2,[string]$color,[float]$width=2) {
  $p=[System.Drawing.Pen]::new((ColorOf $color),$width)
  $script:g.DrawLine($p,$x1,$y1,$x2,$y2); $p.Dispose()
  $script:svg.Add('<line x1="'+$x1+'" y1="'+$y1+'" x2="'+$x2+'" y2="'+$y2+'" stroke="'+$color+'" stroke-width="'+$width+'"/>')
}
function Curve([float]$x1,[float]$y1,[float]$x2,[float]$y2,[string]$color) {
  $mid=($x1+$x2)/2
  $p=[System.Drawing.Pen]::new((ColorOf $color),2)
  $script:g.DrawBezier($p,$x1,$y1,$mid,$y1,$mid,$y2,$x2,$y2); $p.Dispose()
  $script:svg.Add('<path d="M '+$x1+' '+$y1+' C '+$mid+' '+$y1+', '+$mid+' '+$y2+', '+$x2+' '+$y2+'" fill="none" stroke="'+$color+'" stroke-width="2"/>')
}
function Arrow([float]$x1,[float]$y,[float]$x2,[string]$color) {
  Line $x1 $y $x2 $y $color 3
  Line ($x2-12) ($y-8) $x2 $y $color 3
  Line ($x2-12) ($y+8) $x2 $y $color 3
}
function EndFigure {
  $script:svg.Add('</svg>')
  $script:bitmap.Save((Join-Path $OutputDirectory ($script:figureName+'.png')),[System.Drawing.Imaging.ImageFormat]::Png)
  [System.IO.File]::WriteAllLines((Join-Path $OutputDirectory ($script:figureName+'.svg')),$script:svg,[System.Text.UTF8Encoding]::new($false))
  $script:g.Dispose(); $script:bitmap.Dispose()
}

BeginFigure 2200 1440 '003-porting-mindmap'
TextAt $model.mindmap.title 70 45 48 $ink
TextAt $model.mindmap.subtitle 70 120 28 $muted
for($i=0;$i -lt 6;$i++) {
  $left=$i -lt 3; $x=if($left){70}else{1470}; $y=220+($i%3)*350
  if($left){Curve 850 715 720 ($y+130) $line}else{Curve 1350 715 1470 ($y+130) $line}
}
Box 850 620 500 190 '#EDF4F8' '#93AABD'
TextAt $model.mindmap.root[0] 895 648 42 $ink 410
TextAt $model.mindmap.root[1] 895 713 29 $ink 410
TextAt $model.mindmap.root[2] 895 762 29 $ink 410
for($i=0;$i -lt 6;$i++) {
  $x=if($i -lt 3){70}else{1470}; $y=220+($i%3)*350
  $b=$model.mindmap.branches[$i]
  Box $x $y 650 275 '#F7F9FB' '#CED8E0'
  TextAt $b.title ($x+24) ($y+20) 31 $blue 600
  for($j=0;$j -lt $b.lines.Count;$j++){TextAt $b.lines[$j] ($x+24) ($y+83+43*$j) 27 $ink 600}
}
Line 70 1285 2120 1285 '#CED8E0'
TextAt $model.mindmap.footer 70 1320 27 $muted 2050
EndFigure

BeginFigure 2200 1330 '003-command-handoff'
TextAt $model.handoff.title 70 45 48 $ink
TextAt $model.handoff.subtitle 70 120 27 $muted 2060
for($i=0;$i -lt 4;$i++) {
  $x=70+535*$i; $n=$model.handoff.nodes[$i]
  Box $x 235 455 260 '#F2F6F9' '#B9CBD8'
  TextAt $n.title ($x+20) 260 32 $blue 410
  for($j=0;$j -lt $n.lines.Count;$j++){TextAt $n.lines[$j] ($x+20) (332+48*$j) 26 $ink 410}
  if($i -lt 3){Arrow ($x+462) 370 ($x+523) $line}
}
TextAt '观察的是同一组参数版本，不是逐字段“碰巧读对”' 70 560 32 $ink
for($i=0;$i -lt 3;$i++) {
  $y=635+$i*88
  Line 70 ($y+67) 2130 ($y+67) '#DCE4EA'
  TextAt $model.handoff.phases[$i][0] 90 $y 29 $blue 410
  TextAt $model.handoff.phases[$i][1] 530 $y 29 $ink 1500
}
Box 70 965 2060 190 '#FCF3EB' '#DCC6B0'
TextAt $model.handoff.stopTitle 95 985 32 '#704F31' 1980
TextAt $model.handoff.stopLines[0] 95 1045 27 $ink 1980
TextAt $model.handoff.stopLines[1] 95 1092 27 $ink 1980
TextAt $model.handoff.footer 70 1220 26 $muted 2060
EndFigure
Write-Output 'Rendered two PNG/SVG diagrams; all label-width checks passed.'
