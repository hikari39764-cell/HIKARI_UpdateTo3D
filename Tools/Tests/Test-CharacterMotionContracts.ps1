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

$removedFiles = @(
    'HIKARI\Scene\Components\HIKARI_PlayerControllerComponent.h',
    'HIKARI\Scene\Components\HIKARI_PlayerControllerComponent.cpp',
    'HIKARI\Scene\Components\HIKARI_PlayerInputComponent.h',
    'HIKARI\Scene\Components\HIKARI_PlayerInputComponent.cpp',
    'HIKARI\Scene\HIKARI_PlayerInputSystem.h',
    'HIKARI\Scene\HIKARI_PlayerInputSystem.cpp',
    'HIKARI\Scene\HIKARI_PlayerMovementSystem.h',
    'HIKARI\Scene\HIKARI_PlayerMovementSystem.cpp',
    'HIKARI\Scene\Components\HIKARI_CharacterMotorComponent.h',
    'HIKARI\Scene\Components\HIKARI_CharacterMotorComponent.cpp',
    'HIKARI\Scene\HIKARI_CharacterMotorSystem.h',
    'HIKARI\Scene\HIKARI_CharacterMotorSystem.cpp',
    'HIKARI\Scene\HIKARI_CharacterMotorSystemLifecycle.cpp',
    'HIKARI\Scene\HIKARI_CharacterMotorSystemUpdate.cpp'
)
foreach ($relativePath in $removedFiles) {
    if (Test-Path -LiteralPath (Join-Path $repoRoot $relativePath)) {
        throw "Parallel player physics ownership must be removed: $relativePath"
    }
}

$intentTypes = Read-Source 'HIKARI\Gameplay\Motion\HIKARI_MotionIntentTypes.h'
$intentServiceHeader = Read-Source 'HIKARI\Gameplay\Motion\HIKARI_MotionIntentService.h'
$intentService = Read-Source 'HIKARI\Gameplay\Motion\HIKARI_MotionIntentService.cpp'
$motionTypes = Read-Source 'HIKARI\Physics\HIKARI_KinematicMotionTypes.h'
$motionService = Read-Source 'HIKARI\Physics\HIKARI_KinematicMotionService.cpp'
$backend = Read-Source 'HIKARI\Physics\HIKARI_IPhysicsWorldBackend.h'
$physicsSystem = Read-Source 'HIKARI\Physics\HIKARI_PhysicsSystem.cpp'
$kinematicSystem = Read-Source 'HIKARI\Physics\HIKARI_PhysicsSystemKinematicMotion.cpp'
$kinematicSolver = Read-Source 'HIKARI\Physics\HIKARI_PhysicsSystemKinematicSolver.cpp'
$joltCharacters = Read-Source 'HIKARI\Physics\Backends\Jolt\HIKARI_JoltCharacters.cpp'
$joltCharacterSimulation = Read-Source 'HIKARI\Physics\Backends\Jolt\HIKARI_JoltCharacterSimulation.cpp'
$feature = Read-Source 'HIKARI\Scene\Features\HIKARI_GameplayRuntimeFeature.cpp'
$locomotion = Read-Source 'HIKARI\Scene\HIKARI_CharacterLocomotionSystem.cpp'
$locomotionComponent = Read-Source 'HIKARI\Scene\Components\HIKARI_CharacterLocomotionComponent.cpp'
$componentRegistry = Read-Source 'HIKARI\Scene\HIKARI_ComponentRegistry.h'
$componentAuthoring = Read-Source 'HIKARI\Editor\HIKARI_DocumentComponentAuthoringService.cpp'
$inputDefaults = Read-Source 'HIKARI\Input\Assets\HIKARI_InputActionMapJson.cpp'
$documentScene = Read-Source 'HIKARI\Scene\Scenes\HIKARI_DocumentSceneBase.cpp'
$inputComponent = Read-Source 'HIKARI\Scene\Components\HIKARI_CharacterInputComponent.cpp'
$inputActionField = Read-Source 'HIKARI\Editor\Widgets\HIKARI_InputActionFieldWidget.cpp'
$componentAuthoringSection = Read-Source 'HIKARI\Editor\Authoring\HIKARI_SceneComponentAuthoringSection.cpp'
$project = Read-Source 'HIKARI_UpdateTo3D.vcxproj'

Assert-Contains $intentTypes 'struct MotionIntent' `
    'Control sources need a backend-independent motion intent contract.'
Assert-Contains $intentService 'slot.priority > selected->priority' `
    'Multiple intent sources must resolve deterministically by priority.'
Assert-Contains $intentService 'found->jumpLatched || intent.jumpPressed' `
    'Short jump presses must survive until the next fixed step.'
Assert-Contains $intentServiceHeader 'bool PeekIntent(' `
    'Non-owning gameplay observers need to inspect motion intent without consuming it.'
Assert-Contains $intentService 'std::as_const(*this).FindSelectedSlot' `
    'Resolve and peek must share one deterministic intent arbitration path.'

Assert-Contains $motionTypes 'struct KinematicMotionRequest' `
    'Gameplay controllers need a physics-owned movement request contract.'
Assert-Contains $motionTypes 'struct KinematicBodyLandedEvent' `
    'Landing events must be reusable without a CharacterMotor component.'
Assert-Contains $motionService 'slot.priority > selected->priority' `
    'Multiple movement controllers must resolve deterministically.'
Assert-Contains $motionService 'ConsumeExternalVelocity' `
    'Gameplay systems need an additive knockback seam.'
Assert-Contains $motionService 'ConsumeTeleport' `
    'Gameplay systems need an object-level kinematic teleport seam.'

Assert-Contains $backend 'CreateCharacter(' `
    'The backend interface must expose the internal kinematic solver primitive.'
Assert-Contains $backend 'StepCharacter(' `
    'Kinematic solving must stay behind the backend interface.'
Assert-Contains $joltCharacters 'BuildCompoundShape(' `
    'The internal solver must reuse authored Collider geometry.'
Assert-NotContains $joltCharacters 'mInnerBodyShape' `
    'The internal solver must not create a second query/contact body.'
Assert-NotContains $joltCharacters 'SetCharacterVsCharacterCollision' `
    'Character-to-character presence belongs to standard PhysicsBody objects.'
Assert-Contains $joltCharacters '-supportHeight' `
    'The supporting plane must stay relative to the authored character origin.'
Assert-NotContains $joltCharacters 'localBounds.mMin.GetY() + supportHeight' `
    'Center-of-mass-relative Jolt bounds must not shift the supporting plane below the character feet.'
Assert-NotContains $joltCharacterSimulation 'CharacterShapeFilter' `
    'Character shape filtering must not reject intermediate compound hierarchy nodes.'
Assert-Contains $joltCharacterSimulation 'CharacterContactListener validates the final leaf' `
    'Per-Collider masks must be validated only after Jolt resolves the final leaf.'

Assert-Contains $feature 'locomotion.requiredComponents = {' `
    'Character Locomotion must declare its standard physics dependency.'
Assert-Contains $feature '"PhysicsBodyComponent"' `
    'Character Locomotion must build on Physics Body.'
Assert-Contains $feature 'body->properties["motionType"] = "Kinematic"' `
    'Newly auto-added bodies need a usable character preset.'
Assert-Contains $feature 'collider->properties["shape"] = "Capsule"' `
    'Newly auto-added colliders need a usable capsule preset.'
Assert-NotContains $feature 'CharacterMotorComponent' `
    'Gameplay registration must not restore parallel collision ownership.'
Assert-Contains $componentRegistry 'ConfigureDependenciesFn' `
    'Component presets need a generic dependency initialization seam.'
Assert-Contains $componentAuthoring 'newlyAddedDependencies' `
    'Dependency presets must know which supporting components were added in the same transaction.'
Assert-Contains $feature 'if (SceneComponentData* body = FindComponentData(' `
    'Character authoring must repair a pre-existing Physics Body into the required kinematic contract.'

Assert-Contains $locomotion 'PhysicsBodyComponent' `
    'Locomotion must consume the existing Physics Body component.'
Assert-Contains $locomotion 'KinematicMotionRequest' `
    'Locomotion must submit requests rather than own collision.'
Assert-Contains $locomotion 'state->groundVelocity' `
    'Ground movement must remain relative to moving platforms.'
Assert-Contains $locomotion 'GetAirDeceleration()' `
    'Releasing movement while briefly unsupported must not preserve horizontal speed forever.'
Assert-NotContains $locomotion 'acceleration = 0.0f' `
    'Unsupported movement must use authored air braking instead of disabling deceleration.'
Assert-Contains $locomotion 'runtime.jumpBufferRemaining' `
    'Short jump input must remain available until a fixed step can use it.'
Assert-Contains $locomotion 'runtime.groundGraceRemaining' `
    'Brief support loss while crossing collision seams must retain jump permission.'
Assert-Contains $locomotion 'intent.jumpHeld &&' `
    'Fixed-step locomotion must recover a held-button rising edge if the render-frame press edge was missed.'
Assert-Contains $locomotion '!runtime.jumpHeldLastFixedTick' `
    'Held jump recovery must trigger once instead of auto-jumping every fixed tick.'
Assert-Contains $locomotion 'request.jumpRequested = hasJumpRequest' `
    'Standard locomotion must keep buffered jump requests visible to physics.'
Assert-Contains $locomotion 'request.allowJumpWithoutGroundContact =' `
    'Standard locomotion must submit explicit ground-grace authorization.'
Assert-Contains $locomotion 'camera->GetTarget() - camera->GetPosition()' `
    'Camera-relative W input must follow the viewed planar direction.'
Assert-Contains $locomotion 'MATH::Cross(' `
    'Camera-relative horizontal input must derive a matching screen-right axis.'
Assert-Contains $locomotionComponent '"Camera Relative"' `
    'Movement-space authoring must describe its gameplay behavior clearly.'
Assert-Contains $locomotionComponent '"Use Third-Person Movement"' `
    'Third-person movement needs a safe one-click authoring preset.'
Assert-Contains $locomotionComponent '"airDeceleration"' `
    'Air braking must remain an authored reusable locomotion setting.'
Assert-Contains $locomotionComponent '"jumpBufferSeconds"' `
    'Jump buffering must survive scene serialization.'
Assert-Contains $locomotionComponent '"groundGraceSeconds"' `
    'Ground grace must survive scene serialization.'
Assert-Contains $kinematicSolver 'binding.shapes' `
    'PhysicsSystem must build its solver from the actual body shapes.'
Assert-Contains $kinematicSystem 'SetKinematicTarget(' `
    'Solved motion must be applied to the actual Physics Body.'
Assert-Contains $kinematicSystem 'ApplyPhysicsWorldPose' `
    'Solved motion must update the authoritative scene transform.'
Assert-Contains $kinematicSystem 'velocity.y = request.jumpSpeed +' `
    'Physics must apply accepted jump velocity through the kinematic solver.'
Assert-Contains $kinematicSystem 'const bool canApplyJump = onGround ||' `
    'Physics must use its current contact state for ordinary jump requests.'
Assert-Contains $kinematicSystem 'request.allowJumpWithoutGroundContact' `
    'Physics must accept explicit ground-grace authorization.'
Assert-Contains $kinematicSolver 'const PhysicsCharacterHandle previous' `
    'Solver settings hot reload must replace only after candidate creation.'

$processMarker = $physicsSystem.IndexOf(
    'ProcessKinematicMotions(world, frame)',
    [StringComparison]::Ordinal)
$stepMarker = $physicsSystem.IndexOf(
    'service_->Step(frame.fixedDt)',
    [StringComparison]::Ordinal)
if ($processMarker -lt 0 -or $stepMarker -lt $processMarker) {
    throw 'Kinematic requests must be solved before the shared physics world step.'
}

Assert-Contains $inputDefaults '"Gameplay.Sprint"' `
    'The default character input contract must include a sprint action.'
Assert-Contains $documentScene 'editorInputContextWasActive_' `
    'Runtime Play must snapshot editor input ownership before switching contexts.'
Assert-Contains $documentScene 'inputContexts.SetActive("Editor", false)' `
    'Runtime Play must release editor camera controls to gameplay.'
Assert-Contains $documentScene 'gameplayInputContextWasActive_' `
    'Stopping Play must restore the previous gameplay input context state.'
Assert-Contains $inputComponent 'InputActionIdPicker(' `
    'Character Input authoring must use action-aware fields instead of raw text IDs.'
Assert-Contains $inputActionField 'action.valueType == expectedType' `
    'Action fields must filter choices by the required value type.'
Assert-Contains $componentAuthoringSection '&scene.GetWorld().Services()' `
    'Document component inspectors must receive runtime services for action pickers and status.'
Assert-Contains $project 'HIKARI_PhysicsSystemKinematicMotion.cpp' `
    'The unified kinematic motion path must be part of the Visual Studio project.'
Assert-Contains $project 'HIKARI_PhysicsSystemKinematicSolver.cpp' `
    'The kinematic solver lifecycle must be part of the Visual Studio project.'
Assert-Contains $project 'HIKARI_KinematicMotionService.cpp' `
    'The kinematic request service must be part of the Visual Studio project.'
Assert-Contains $project 'HIKARI_InputActionFieldWidget.cpp' `
    'The reusable input action field must be part of the Visual Studio project.'
Assert-NotContains $project 'HIKARI_CharacterMotor' `
    'Removed CharacterMotor files must not remain in the project.'

$actionsPath = Join-Path $repoRoot 'ProjectSettings\Input\actions.json'
$actions = Get-Content -LiteralPath $actionsPath -Raw | ConvertFrom-Json
if (-not ($actions.actions.id -contains 'Gameplay.Sprint')) {
    throw 'Project input settings are missing Gameplay.Sprint.'
}

Write-Host 'Character motion foundation contract checks passed.'
