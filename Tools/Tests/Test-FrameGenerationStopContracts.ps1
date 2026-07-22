$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$services = Get-Content -LiteralPath (Join-Path $repoRoot 'HIKARI\HIKARI_Services.h') -Raw
$frameGeneration = Get-Content -LiteralPath (Join-Path $repoRoot 'HIKARI\Render3D\Upscaling\HIKARI_StreamlineFrameGeneration.cpp') -Raw
$synchronization = Get-Content -LiteralPath (Join-Path $repoRoot 'HIKARI\Render3D\Upscaling\HIKARI_StreamlineFrameGenerationSynchronization.cpp') -Raw
$project = Get-Content -LiteralPath (Join-Path $repoRoot 'HIKARI_UpdateTo3D.vcxproj') -Raw

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

$endFrame = Get-CppFunctionBody $services 'inline bool EndFrame()'
$presentIndex = $endFrame.IndexOf('gCore.EndFrame()', [StringComparison]::Ordinal)
$captureIndex = $endFrame.IndexOf('CaptureStreamlineFrameGenerationCompletionAfterPresent()', [StringComparison]::Ordinal)
if ($presentIndex -lt 0 -or $captureIndex -le $presentIndex) {
    throw 'DLSS-G completion state must be captured on the presenting thread after Present.'
}

$beginStop = Get-CppFunctionBody $services 'inline bool BeginInProcessGamePresentationStop()'
$idleIndex = $beginStop.IndexOf('gCore.WaitForIdle()', [StringComparison]::Ordinal)
$deactivateIndex = $beginStop.IndexOf('DeactivateStreamlineFrameGeneration(true)', [StringComparison]::Ordinal)
if ($idleIndex -lt 0 -or $deactivateIndex -le $idleIndex) {
    throw 'The presenting queue must drain before DLSS-G resources are deactivated.'
}

$deactivate = Get-CppFunctionBody $frameGeneration 'bool DeactivateStreamlineFrameGeneration(bool releaseResources)'
$waitIndex = $deactivate.IndexOf('WaitForStreamlineFrameGenerationInputs()', [StringComparison]::Ordinal)
$freeIndex = $deactivate.IndexOf('slFreeResources(sl::kFeatureDLSS_G', [StringComparison]::Ordinal)
if ($waitIndex -lt 0 -or $freeIndex -le $waitIndex) {
    throw 'DLSS-G input processing must finish before viewport resources are freed.'
}

if ($synchronization -notmatch 'lastPresentInputsProcessingCompletionFenceValue' -or
    $synchronization -notmatch 'SetEventOnCompletion' -or
    $synchronization -notmatch 'slDLSSGGetState\(after Present\)') {
    throw 'The Streamline synchronization bridge must capture and wait on the SDK completion fence.'
}

if ($project -notmatch 'HIKARI_StreamlineFrameGenerationSynchronization\.cpp') {
    throw 'The Streamline synchronization bridge must be compiled by the project.'
}

Write-Host 'Frame-generation stop contract checks passed.'
