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

function Assert-NotContains(
    [string]$source,
    [string]$unexpected,
    [string]$message) {
    if ($source.IndexOf($unexpected, [StringComparison]::Ordinal) -ge 0) {
        throw $message
    }
}

$assetTypes = Read-Source 'HIKARI\Animation\Assets\HIKARI_AnimationAssetTypes.h'
$pose = Read-Source 'HIKARI\Animation\Runtime\HIKARI_AnimationPose.h'
$sampler = Read-Source 'HIKARI\Animation\Runtime\HIKARI_AnimationClipSampler.cpp'
$animationSystem = Read-Source 'HIKARI\Animation\Runtime\HIKARI_AnimationSystem.cpp'
$animationFeature = Read-Source 'HIKARI\Scene\Features\HIKARI_AnimationRuntimeFeature.cpp'
$renderingFeature = Read-Source 'HIKARI\Scene\Features\HIKARI_RenderingRuntimeFeature.cpp'
$sceneSync = Read-Source 'HIKARI\Scene\HIKARI_SceneRenderCacheSync.cpp'
$surfaceResolver = Read-Source 'HIKARI\Render3D\Runtime\HIKARI_RenderSurfaceResolver.cpp'
$poseBuilder = Read-Source 'HIKARI\Render3D\GpuDriven\HIKARI_GpuScenePoseBuilder.cpp'
$modelManager = Read-Source 'HIKARI\Render3D\Core\HIKARI_ModelManager.cpp'
$hmodelFormat = Read-Source 'HIKARI\Assets\Formats\HIKARI_HmodelFormat.cpp'
$motionState = Read-Source 'HIKARI\Gameplay\Motion\HIKARI_CharacterMotionState.h'
$motionStateSystem = Read-Source 'HIKARI\Scene\HIKARI_CharacterMotionStateSystem.cpp'
$project = Read-Source 'HIKARI_UpdateTo3D.vcxproj'
$projectSettings = Read-Source 'ProjectSettings\project_settings.json'
$projectSettingsSource = Read-Source 'HIKARI\Project\HIKARI_ProjectSettings.cpp'

Assert-Contains $assetTypes 'struct AnimationClipId' `
    'Animation clips need deterministic subresource identities.'
Assert-Contains $assetTypes 'inTangent' `
    'Cubic spline input tangents must be retained in animation assets.'
Assert-NotContains $assetTypes 'AnimationTargetPath : uint8_t' `
    'HMODEL v3 enum storage must not change without a versioned migration.'
Assert-Contains $pose 'struct AnimationPoseSnapshot' `
    'Animation runtime needs one published pose contract.'
Assert-Contains $pose 'AnimationRootMotionDelta' `
    'Root motion needs an explicit optional seam instead of implicit transform writes.'
Assert-Contains $sampler 'BlendAnimationPoses' `
    'Clip transitions must blend in the animation runtime.'
Assert-Contains $animationSystem 'AnimationPoseService' `
    'AnimationSystem must publish evaluated poses through the runtime service.'
Assert-Contains $animationFeature 'RuntimeFeatureIds::Animation' `
    'Animation must be installable as its own LEGO-style runtime feature.'
Assert-NotContains $renderingFeature 'AnimatorComponent' `
    'Rendering must not own animation playback components.'
Assert-NotContains $renderingFeature 'AnimationSystem' `
    'Rendering must not own animation evaluation systems.'
Assert-NotContains $sceneSync 'AnimatorComponent' `
    'The Scene-to-Renderer bridge must depend only on published poses.'

foreach ($renderConsumer in @($surfaceResolver, $poseBuilder)) {
    Assert-NotContains $renderConsumer 'FindAnimationClip' `
        'Render3D must not resolve clips.'
    Assert-NotContains $renderConsumer 'SampleAnimationClip' `
        'Render3D must not sample clips.'
    Assert-NotContains $renderConsumer 'animationTimeSec' `
        'Render3D must not own playback time.'
}

Assert-Contains $modelManager 'valueStride = cubicSpline ? 12u : 4u' `
    'glTF cubic quaternion key triples must be imported correctly.'
Assert-Contains $modelManager 'valueStride = cubicSpline ? 9u : 3u' `
    'glTF cubic vector key triples must be imported correctly.'
Assert-Contains $hmodelFormat 'kHmodelVersion = 4' `
    'Animation tangent storage needs an explicit HMODEL version.'
Assert-Contains $hmodelFormat 'if (version < 4u)' `
    'HMODEL v1-v3 animation keys must remain readable.'
Assert-Contains $hmodelFormat 'LegacyAnimationKeyframe' `
    'Legacy animation keyframes need an explicit migration layout.'
Assert-Contains $motionState 'struct CharacterMotionState' `
    'Animation logic needs a gameplay-owned read-only motion snapshot.'
Assert-Contains $motionStateSystem 'PeekIntent(' `
    'Motion observation must not consume gameplay input.'
Assert-Contains $projectSettings '"Animation"' `
    'The project must enable the separated Animation feature.'
Assert-Contains $projectSettingsSource 'sourceVersion < 3u' `
    'Existing projects must migrate Rendering-owned animation automatically.'

foreach ($projectEntry in @(
    'HIKARI_AnimationClipSampler.cpp',
    'HIKARI_AnimationPoseService.cpp',
    'HIKARI_AnimationSystem.cpp',
    'HIKARI_AnimationRuntimeFeature.cpp',
    'HIKARI_CharacterMotionStateService.cpp',
    'HIKARI_CharacterMotionStateSystem.cpp')) {
    Assert-Contains $project $projectEntry `
        "Visual Studio project is missing $projectEntry"
}

if (Test-Path -LiteralPath (Join-Path $repoRoot `
        'HIKARI\Scene\HIKARI_AnimationSystem.cpp')) {
    throw 'The legacy Scene-owned AnimationSystem must stay removed.'
}

Write-Host 'Animation runtime boundary contract checks passed.'
