$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$controller = Get-Content -LiteralPath (Join-Path $repoRoot 'HIKARI\Runtime\HIKARI_GamePresentationController.cpp') -Raw
$window = Get-Content -LiteralPath (Join-Path $repoRoot 'HIKARI\Platform\HIKARI_Win32Window.cpp') -Raw

function Get-CppFunctionBody([string]$source, [string]$signature) {
    $signatureIndex = $source.IndexOf($signature, [StringComparison]::Ordinal)
    if ($signatureIndex -lt 0) {
        throw "Function signature not found: $signature"
    }
    $bodyStart = $source.IndexOf('{', $signatureIndex)
    $depth = 0
    for ($index = $bodyStart; $index -lt $source.Length; ++$index) {
        if ($source[$index] -eq '{') {
            ++$depth
        } elseif ($source[$index] -eq '}') {
            --$depth
            if ($depth -eq 0) {
                return $source.Substring($bodyStart, $index - $bodyStart + 1)
            }
        }
    }
    throw "Function body is incomplete: $signature"
}

$restore = Get-CppFunctionBody $controller 'bool GamePresentationController::RestoreEditorSurface('
if ($restore -notmatch 'ShowWindow\(editorWindow\.GetHWND\(\), SW_SHOW\);' -or
    $restore -match 'ShowWindow\([^;]*SW_RESTORE') {
    throw 'Editor restoration must preserve the pre-Play maximized or windowed state.'
}
if ($restore -notmatch 'editorWindow\.RefreshClientSize\(\)' -or
    $restore -notmatch 'editorContext\.backBufferWidth' -or
    $restore -notmatch 'core\.Resize\(editorWindow\.Width\(\), editorWindow\.Height\(\)\)') {
    throw 'Editor restoration must reconcile the native client size and active presentation surface.'
}

$refresh = Get-CppFunctionBody $window 'bool Win32Window::RefreshClientSize()'
if ($refresh -notmatch 'GetClientRect' -or
    $refresh -notmatch 'width_\s*=\s*clientWidth' -or
    $refresh -notmatch 'height_\s*=\s*clientHeight') {
    throw 'Win32Window must refresh its cached dimensions from the real client rectangle.'
}

Write-Host 'Editor presentation restore contract checks passed.'
