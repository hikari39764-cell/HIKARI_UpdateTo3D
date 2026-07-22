$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourcePath = Join-Path $repoRoot 'HIKARI\Gfx\HIKARI_PixProfiler.cpp'
if (-not (Test-Path -LiteralPath $sourcePath)) {
    throw 'PIX profiler source file was not found.'
}

$source = [System.IO.File]::ReadAllText(
    $sourcePath,
    [System.Text.Encoding]::UTF8)

if ($source.IndexOf(
        'PIXSetHUDOptions(',
        [StringComparison]::Ordinal) -lt 0) {
    throw 'PIX capturer initialization must configure its HUD explicitly.'
}
if ($source.IndexOf(
        'PIX_HUD_SHOW_ON_NO_WINDOWS',
        [StringComparison]::Ordinal) -lt 0) {
    throw 'Normal HIKARI windows must not show the PIX capture HUD.'
}
if ($source.IndexOf(
        'PIXLoadLatestWinPixGpuCapturerLibrary()',
        [StringComparison]::Ordinal) -lt 0) {
    throw 'Hiding the HUD must not remove explicit in-process PIX capture support.'
}

Write-Host 'PIX profiler contract checks passed.'
