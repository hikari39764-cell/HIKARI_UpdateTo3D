$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$services = Get-Content -LiteralPath (Join-Path $repoRoot 'HIKARI\HIKARI_Services.h') -Raw
$runtime = Get-Content -LiteralPath (Join-Path $repoRoot 'HIKARI\Render3D\Upscaling\HIKARI_StreamlineRuntime.cpp') -Raw
$frameGeneration = Get-Content -LiteralPath (Join-Path $repoRoot 'HIKARI\Render3D\Upscaling\HIKARI_StreamlineFrameGeneration.cpp') -Raw

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

$contextRefresh = Get-CppFunctionBody $services 'inline void UpdateGpuContexts()'
if ($contextRefresh -notmatch 'RENDER3D::UPSCALING::UpdateStreamlineContext\(gCtx\);') {
    throw 'UpdateGpuContexts must refresh the Streamline context after swap-chain resize.'
}

$recordResult = Get-CppFunctionBody $runtime 'bool RecordResult('
$resourcePressurePattern = 'state\.stats\.status\s*=\s*result\s*==\s*sl::Result::eWarnOutOfVRAM\s*\?\s*StreamlineRuntimeStatus::ResourcePressure\s*:\s*StreamlineRuntimeStatus::RuntimeFailure;'
if ($recordResult -notmatch $resourcePressurePattern) {
    throw 'RecordResult must preserve out-of-VRAM as Streamline resource pressure.'
}

$submitFrameGeneration = Get-CppFunctionBody $frameGeneration 'bool SubmitStreamlineFrameGenerationInputs('
$pressureReleasePattern = '(?s)const bool releaseFrameGenerationResources\s*=\s*state\.stats\.status\s*==\s*StreamlineRuntimeStatus::ResourcePressure;.*?SuspendStreamlineFrameGeneration\(\s*releaseFrameGenerationResources\);'
if ($submitFrameGeneration -notmatch $pressureReleasePattern) {
    throw 'DLSS resource pressure must release retained frame-generation resources before retry.'
}

Write-Host 'Streamline lifecycle contract checks passed.'
