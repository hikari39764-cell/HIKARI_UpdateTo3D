$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Read-Source([string]$relativePath) {
    $path = Join-Path $repoRoot $relativePath
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Source file not found: $relativePath"
    }
    return [System.IO.File]::ReadAllText(
        $path,
        [System.Text.Encoding]::UTF8)
}

function Assert-Contains(
    [string]$source,
    [string]$expected,
    [string]$message) {
    if ($source.IndexOf($expected, [StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

$types = Read-Source 'HIKARI\Input\Runtime\HIKARI_InputTypes.h'
$backend = Read-Source 'HIKARI\Input\Platform\HIKARI_Win32InputBackend.cpp'
$service = Read-Source 'HIKARI\Input\Runtime\HIKARI_InputService.cpp'
$presentation = Read-Source 'HIKARI\HIKARI_Services.h'

Assert-Contains $types 'enum class MouseCaptureMode' `
    'Mouse ownership must be an input-layer contract.'
Assert-Contains $backend 'MouseCaptureMode::Relative' `
    'The Win32 backend must support relative mouse input.'
Assert-Contains $backend 'SetCursorPos(center.x, center.y)' `
    'Relative mouse input must not stop at a desktop edge.'
Assert-Contains $backend 'ClipCursor(nullptr)' `
    'Losing gameplay ownership must release cursor confinement.'
Assert-Contains $backend 'ReleaseRelativeMouseCapture();' `
    'Backend shutdown and focus loss must restore the OS cursor.'
Assert-Contains $service 'SetMouseCaptureMode(MouseCaptureMode mode)' `
    'Presentation code must not depend directly on Win32 input details.'
Assert-Contains $presentation 'INPUT::MouseCaptureMode::Relative' `
    'Game presentation must acquire relative mouse input.'
Assert-Contains $presentation 'IsStandaloneGameHost()' `
    'Standalone game input must use the same capture contract as Play.'
Assert-Contains $presentation 'INPUT::MouseCaptureMode::Free' `
    'Stopping Game Preview must restore the editor cursor.'

Write-Host 'Relative mouse capture contract checks passed.'
