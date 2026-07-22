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

$rigTypes = Read-Source 'HIKARI\Scene\Camera\HIKARI_CameraRigTypes.h'
$rigService = Read-Source 'HIKARI\Scene\Camera\HIKARI_CameraRigService.cpp'
$director = Read-Source 'HIKARI\Scene\HIKARI_CameraDirector.cpp'
$follow = Read-Source 'HIKARI\Scene\HIKARI_CameraFollowSystem.cpp'
$followComponent = Read-Source 'HIKARI\Scene\Components\HIKARI_CameraFollowComponent.cpp'
$activation = Read-Source 'HIKARI\Scene\HIKARI_CameraActivationVolumeSystem.cpp'
$feature = Read-Source 'HIKARI\Scene\Features\HIKARI_CameraRuntimeFeature.cpp'
$sequenceDriver = Read-Source 'HIKARI\Scene\Sequencer\Drivers\HIKARI_CameraSequenceTrackDriver.cpp'
$documentScene = Read-Source 'HIKARI\Scene\Scenes\HIKARI_DocumentSceneBase.cpp'
$project = Read-Source 'HIKARI_UpdateTo3D.vcxproj'
$inputDefaults = Read-Source 'HIKARI\Input\Assets\HIKARI_InputActionMapJson.cpp'

Assert-Contains $rigTypes 'struct CameraRigPose' `
    'Custom C++ camera systems need a backend-independent pose contract.'
Assert-Contains $rigTypes 'MakeCameraRigSourceId(' `
    'Custom C++ camera systems need readable stable source identifiers.'
Assert-Contains $rigTypes 'struct CameraModifierSubmission' `
    'Temporary camera effects need a reusable modifier contract.'
Assert-Contains $rigService 'candidatePriority > currentPriority' `
    'Multiple rig sources must resolve deterministically by priority.'
Assert-Contains $rigService 'applyDuringOverride' `
    'Camera modifiers must explicitly choose whether they affect cinematics.'

Assert-Contains $director 'Find<CAMERA::CameraRigService>()' `
    'CameraDirector must consume the shared rig service.'
Assert-Contains $director 'TryResolveFallbackCamera(gameplaySource)' `
    'A rig-driven camera must remain usable before a default is explicitly selected.'
Assert-Contains $director '!IsCameraObjectEnabled(world, gameplaySource)' `
    'A disabled or missing default camera must fall back to an active rig camera.'
Assert-Contains $director 'winningOverride != nullptr' `
    'Existing override arbitration must remain the final camera owner.'

Assert-Contains $follow 'LateUpdate(' `
    'Gameplay cameras must resolve after ordinary object movement.'
Assert-Contains $follow 'SubmitPose(submission)' `
    'Built-in follow behavior must use the shared rig contract.'
Assert-Contains $follow 'PhysicsShapeCastQuery' `
    'The built-in gameplay rig must protect the camera from wall clipping.'
Assert-Contains $follow 'GetAxis2D(' `
    'The built-in gameplay rig must consume project input actions.'
Assert-Contains $follow 'yaw -= look[0]' `
    'Orbit yaw must turn the viewed direction with the input direction.'
Assert-Contains $follow 'GetInvertVerticalLook() ? -1.0f : 1.0f' `
    'Default vertical orbit input must follow the viewed direction.'
Assert-Contains $follow 'GetMouseSensitivityDegreesPerPixel()' `
    'Mouse deltas must use frame-rate-independent sensitivity.'
Assert-Contains $follow 'GetRuntimeFollowPivot()' `
    'Camera follow must smooth one orbit center instead of the eye twice.'
Assert-Contains $follow 'targetSpeed > 0.05f' `
    'Automatic recentering must not fight the player while the target is idle.'
Assert-Contains $follow 'motionIntentService_->PeekIntent(' `
    'Camera policy must observe the shared motion contract instead of hard-coding input keys.'
Assert-Contains $follow 'ResolveTargetWorldMatrix(' `
    'Gameplay camera follow must resolve its target through one transform boundary.'
Assert-Contains $follow 'presentationTransforms->TryGetWorldMatrix(' `
    'Gameplay camera follow and rendering must consume the same interpolated target pose.'
Assert-Contains $follow '!movingBackward' `
    'Automatic recentering must not chase a camera-relative backward input feedback loop.'
Assert-NotContains $follow 'camera->SetLookAt' `
    'Camera systems must not bypass CameraDirector by writing the global camera.'
Assert-Contains $followComponent 'SceneObjectIdPicker(' `
    'Camera targets must use the scene object picker instead of raw numeric IDs.'
Assert-Contains $followComponent 'InputActionIdPicker(' `
    'Camera input authoring must use action-aware fields.'

Assert-Contains $activation 'PushOverride(request)' `
    'Camera volumes must reuse CameraDirector activation requests.'
Assert-Contains $activation 'ReleaseOverride' `
    'Leaving or removing a camera volume must restore the previous camera.'
Assert-Contains $feature '"CameraActivationVolumeComponent"' `
    'Camera activation volumes must be registered as an optional camera component.'
Assert-Contains $feature 'follow.requiredComponents = {' `
    'The built-in gameplay rig must declare CameraComponent as its base block.'

Assert-Contains $sequenceDriver 'cameraDirector_.PushOverride(request)' `
    'Sequence camera tracks must retain the existing Director integration.'
Assert-Contains $sequenceDriver 'request.affectsControlBasis = false' `
    'Cinematic camera cuts must not silently rotate gameplay movement controls.'
Assert-Contains $documentScene 'services.Register(cameraDirector_)' `
    'Game-specific systems need access to the shared activation owner.'
Assert-Contains $documentScene 'services.Register(cameraRigService_)' `
    'Game-specific systems need access to the shared pose and modifier service.'
Assert-Contains $documentScene 'cameraRigService_.BeginFrame(frame.frameIndex)' `
    'Per-frame camera submissions must have an explicit lifetime boundary.'

Assert-Contains $inputDefaults '"Gameplay.CameraZoom"' `
    'The default input project must expose camera zoom.'
Assert-Contains $inputDefaults '"Gameplay.CameraRecenter"' `
    'The default input project must expose camera recentering.'
Assert-Contains $inputDefaults '{ "Gameplay.Look", "Look", InputActionValueType::Axis2D, false }' `
    'Mouse look deltas must not be clamped to a unit stick vector.'
Assert-Contains $project 'HIKARI_CameraRigService.cpp' `
    'The camera rig service must be included in the Visual Studio project.'
Assert-Contains $project 'HIKARI_CameraActivationVolumeSystem.cpp' `
    'The camera activation system must be included in the Visual Studio project.'

Write-Host 'Camera rig foundation contract checks passed.'
