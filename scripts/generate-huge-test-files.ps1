param(
    [string] $OutputDir = "C:\temp",
    [double] $Scale = 1.0,
    [switch] $Force
)

$ErrorActionPreference = "Stop"

$Omega = [char]0x03A9
$Cjk = [char]0x4E2D
$CombiningAcute = [char]0x0301
$Emoji = [char]::ConvertFromUtf32(0x1F600)

function Get-LineCount($Count) {
    $scaled = [int][Math]::Round($Count * $Scale)

    if($scaled -lt 1) {
        return 1
    }

    return $scaled
}

function New-TestFile($Path) {
    if((Test-Path -LiteralPath $Path) -and !$Force) {
        $item = Get-Item -LiteralPath $Path
        Write-Host ("exists  {0,-34} {1,12} bytes" -f $item.Name, $item.Length)
        return $false
    }

    if(Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Force
    }

    return $true
}

function Write-LinesFile($Name, [System.Text.Encoding] $Encoding, [string] $NewLine, [int] $LineCount, [scriptblock] $LineFactory) {
    $path = Join-Path $OutputDir $Name

    if(!(New-TestFile $path)) {
        return
    }

    $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)

    try {
        $sw = New-Object System.IO.StreamWriter($fs, $Encoding, 1MB)

        try {
            $sw.NewLine = $NewLine

            for($i = 0; $i -lt (Get-LineCount $LineCount); $i++) {
                $sw.WriteLine((& $LineFactory $i))
            }
        }
        finally {
            $sw.Dispose()
        }
    }
    finally {
        $fs.Dispose()
    }

    $item = Get-Item -LiteralPath $path
    Write-Host ("created {0,-34} {1,12} bytes" -f $item.Name, $item.Length)
}

function Write-NoNewlineFile($Name, [System.Text.Encoding] $Encoding, [int] $Megabytes) {
    $path = Join-Path $OutputDir $Name

    if(!(New-TestFile $path)) {
        return
    }

    $chunk = "abcdefghijklmnopqrstuvwxyz 0123456789 UTF-8 no newline test -- "

    while($chunk.Length -lt 65536) {
        $chunk += $chunk
    }

    $chunk = $chunk.Substring(0, 65536)

    $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)

    try {
        $sw = New-Object System.IO.StreamWriter($fs, $Encoding, 1MB)

        try {
            for($i = 0; $i -lt (Get-LineCount ($Megabytes * 16)); $i++) {
                $sw.Write($chunk)
            }
        }
        finally {
            $sw.Dispose()
        }
    }
    finally {
        $fs.Dispose()
    }

    $item = Get-Item -LiteralPath $path
    Write-Host ("created {0,-34} {1,12} bytes" -f $item.Name, $item.Length)
}

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$Utf8Bom      = New-Object System.Text.UTF8Encoding($true)
$Utf8NoBom    = New-Object System.Text.UTF8Encoding($false)
$Utf16LEBom   = New-Object System.Text.UnicodeEncoding($false, $true)
$Utf16LENoBom = New-Object System.Text.UnicodeEncoding($false, $false)
$Utf16BEBom   = New-Object System.Text.UnicodeEncoding($true, $true)
$Utf16BENoBom = New-Object System.Text.UnicodeEncoding($true, $false)

Write-LinesFile "huge-utf8-bom-crlf.txt"       $Utf8Bom      "`r`n" 750000  { param($i) ("line {0:D7} UTF-8 BOM CRLF cafe{1} {2} emoji {3} 0123456789" -f $i, $CombiningAcute, $Omega, $Emoji) }
Write-LinesFile "huge-utf8-nobom-crlf.txt"     $Utf8NoBom    "`r`n" 750000  { param($i) ("line {0:D7} UTF-8 no BOM CRLF cafe{1} {2} emoji {3} 0123456789" -f $i, $CombiningAcute, $Omega, $Emoji) }
Write-LinesFile "huge-utf8-bom-lf.txt"         $Utf8Bom      "`n"   750000  { param($i) ("line {0:D7} UTF-8 BOM LF cafe{1} {2} emoji {3} 0123456789" -f $i, $CombiningAcute, $Omega, $Emoji) }
Write-LinesFile "huge-utf8-nobom-lf.txt"       $Utf8NoBom    "`n"   750000  { param($i) ("line {0:D7} UTF-8 no BOM LF cafe{1} {2} emoji {3} 0123456789" -f $i, $CombiningAcute, $Omega, $Emoji) }
Write-LinesFile "huge-utf8-bom-mixed-crlf.txt" $Utf8Bom      "`r`n" 1000000 { param($i) ("line {0:D7} UTF-8 cafe{1} omega {2} emoji {3} cjk {4} 0123456789" -f $i, $CombiningAcute, $Omega, $Emoji, $Cjk) }
Write-LinesFile "huge-utf8-lf.txt"             $Utf8NoBom    "`n"   1200000 { param($i) ("line {0:D7} LF only abcdefghijklmnopqrstuvwxyz 0123456789" -f $i) }
Write-LinesFile "huge-utf8-cr.txt"             $Utf8NoBom    "`r"   500000  { param($i) ("line {0:D7} CR only abcdefghijklmnopqrstuvwxyz 0123456789" -f $i) }

Write-LinesFile "huge-utf16le-bom-crlf.txt"    $Utf16LEBom   "`r`n" 350000  { param($i) ("line {0:D7} UTF-16LE BOM CRLF omega {1} emoji {2} 0123456789" -f $i, $Omega, $Emoji) }
Write-LinesFile "huge-utf16le-nobom-crlf.txt"  $Utf16LENoBom "`r`n" 350000  { param($i) ("line {0:D7} UTF-16LE no BOM CRLF omega {1} emoji {2} 0123456789" -f $i, $Omega, $Emoji) }
Write-LinesFile "huge-utf16be-bom-crlf.txt"    $Utf16BEBom   "`r`n" 350000  { param($i) ("line {0:D7} UTF-16BE BOM CRLF omega {1} emoji {2} 0123456789" -f $i, $Omega, $Emoji) }
Write-LinesFile "huge-utf16be-nobom-crlf.txt"  $Utf16BENoBom "`r`n" 350000  { param($i) ("line {0:D7} UTF-16BE no BOM CRLF omega {1} emoji {2} 0123456789" -f $i, $Omega, $Emoji) }

Write-NoNewlineFile "huge-utf8-no-newlines.txt" $Utf8NoBom 64

Get-ChildItem -LiteralPath $OutputDir -Filter "huge-*" |
    Sort-Object Name |
    Select-Object Name, Length
