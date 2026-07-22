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

$types = Read-Source 'HIKARI\Physics\HIKARI_PhysicsTypes.h'
$backend = Read-Source 'HIKARI\Physics\HIKARI_IPhysicsWorldBackend.h'
$validator = Read-Source 'HIKARI\Physics\HIKARI_PhysicsBodyValidator.cpp'
$system = Read-Source 'HIKARI\Physics\HIKARI_PhysicsSystem.cpp'
$bodyLifecycle = Read-Source 'HIKARI\Physics\HIKARI_PhysicsSystemBodyLifecycle.cpp'
$synchronization = Read-Source 'HIKARI\Physics\HIKARI_PhysicsSystemSynchronization.cpp'
$presentation = Read-Source 'HIKARI\Physics\HIKARI_PhysicsPresentation.cpp'
$kinematicMotion = Read-Source 'HIKARI\Physics\HIKARI_PhysicsSystemKinematicMotion.cpp'
$store = Read-Source 'HIKARI\Physics\HIKARI_PhysicsCollisionGeometryStore.cpp'
$setup = Read-Source 'HIKARI\Assets\Collision\HIKARI_ModelCollisionSetup.cpp'
$artifact = Read-Source 'HIKARI\Assets\Collision\HIKARI_ModelCollisionArtifact.cpp'
$format = Read-Source 'HIKARI\Assets\Collision\HIKARI_HcollisionFormat.cpp'
$generator = Read-Source 'HIKARI\Assets\Collision\HIKARI_ModelCollisionGenerator.cpp'
$generationHeader = Read-Source 'HIKARI\Assets\Collision\HIKARI_ModelCollisionGenerator.h'
$workspace = Read-Source 'HIKARI\Editor\Workspaces\HIKARI_ModelCollisionWorkspaceController.cpp'
$workspaceWindows = Read-Source 'HIKARI\Editor\Workspaces\HIKARI_ModelCollisionWorkspaceWindows.cpp'
$interaction = Read-Source 'HIKARI\Editor\Workspaces\HIKARI_ModelCollisionWorkspaceInteraction.cpp'
$raycast = Read-Source 'HIKARI\Editor\Workspaces\HIKARI_ModelCollisionRaycast.cpp'
$gizmo = Read-Source 'HIKARI\Scene\Debug\HIKARI_ColliderGizmoProvider.cpp'
$joltShapeFactory = Read-Source 'HIKARI\Physics\Backends\Jolt\HIKARI_JoltShapeFactory.cpp'

Assert-Contains $types 'enum class PhysicsErrorCode' `
    'Physics failures must use structured error codes.'
Assert-Contains $types 'struct PhysicsShapeKey' `
    'Contact and query results need stable authored-shape identity.'
Assert-Contains $backend 'PhysicsBodyCreateResult CreateBody' `
    'Physics backends must return a structured body creation result.'
Assert-Contains $backend 'PhysicsStepResult Step' `
    'Physics backends must report fixed-step failures explicitly.'
Assert-Contains $validator 'PhysicsErrorCode::FullyLockedDynamic' `
    'Fully locked dynamic bodies must be rejected instead of silently downgraded.'
Assert-Contains $validator 'PhysicsErrorCode::UnsupportedScale' `
    'Ambiguous collider scale must be rejected at the engine boundary.'

$createMarker = $bodyLifecycle.IndexOf(
    'PhysicsBodyCreateResult created =',
    [StringComparison]::Ordinal)
$destroyMarker = $bodyLifecycle.IndexOf(
    '(void)service_->DestroyBody(retainedBinding->body);',
    $createMarker,
    [StringComparison]::Ordinal)
if ($createMarker -lt 0 -or $destroyMarker -lt $createMarker) {
    throw 'Body replacement must create the candidate before destroying the last-known-good body.'
}
Assert-Contains $bodyLifecycle 'PhysicsBodyRuntimeState::RetainedPrevious' `
    'Failed hot reload must expose that the last-known-good body is retained.'
Assert-Contains $bodyLifecycle '"physics body definition recovered"' `
    'A retained body must return to Ready when its definition recovers.'
Assert-Contains $synchronization 'PresentationTransformService' `
    'Render interpolation must stay separate from authoritative scene transforms.'
Assert-Contains $synchronization 'interpolatesControlledKinematic' `
    'Controller-driven kinematic bodies need fixed-step presentation interpolation.'
Assert-Contains $synchronization 'presentationDiscontinuity' `
    'Body rebuilds and teleports must snap instead of interpolating across discontinuities.'
Assert-Contains $presentation 'NlerpShortest' `
    'Physics presentation rotation must use the shortest quaternion path.'
Assert-Contains $presentation 'std::isfinite(alpha)' `
    'Physics presentation interpolation must sanitize invalid frame alpha values.'
Assert-Contains $kinematicMotion 'CommitFixedState(' `
    'Solved character motion must publish one coherent fixed-step state.'
Assert-Contains $kinematicMotion 'teleportedState,' `
    'Kinematic teleports must publish an explicit presentation discontinuity.'

Assert-Contains $store 'GetContentRevision()' `
    'Collision hot reload must follow the asset database content revision.'
Assert-Contains $store 'readInfo.contentHash' `
    'Runtime body signatures must use collision artifact content identity.'
Assert-NotContains $store 'last_write_time' `
    'Fixed-step collision reconciliation must not stat files on disk.'

Assert-Contains $setup 'CommitFileReplacementTransaction' `
    'Collision setup JSON and geometry sidecar must commit transactionally.'
Assert-Contains $setup 'LoadModelCollisionSetup(' `
    'Staged collision setup data must be read back before commit.'
Assert-Contains $artifact 'ReadHcollisionFile(' `
    'Staged runtime collision artifacts must be read back before commit.'
Assert-Contains $artifact 'CommitFileReplacementTransaction' `
    'The runtime collision artifact must preserve its previous valid version on failure.'
Assert-Contains $format 'kSerializedShapeBytes = 76u' `
    'HCOLLISION validation must use the complete serialized shape record size.'
Assert-Contains $format 'kMaximumPayloadBytes' `
    'HCOLLISION loading must enforce a memory budget before allocation.'

Assert-Contains $generationHeader 'RequestCancel()' `
    'Long collision generation needs an explicit cancellation contract.'
Assert-Contains $generator 'IsCancellationRequested()' `
    'Generation must observe cancellation between processing batches.'
Assert-NotContains $generator 'ConvexHullFallback' `
    'Convex generation must not silently replace a failed hull with under-covering point samples.'
Assert-Contains $workspace 'generationDraft_ = std::move(output);' `
    'Generated shapes must remain a reviewable draft until explicitly applied.'
Assert-Contains $workspaceWindows 'generationDraft_->setup' `
    'The viewport must preview the generated draft before it is applied.'
Assert-Contains $raycast 'IntersectRayTriangle(' `
    'Viewport selection must confirm exact geometry after broad-phase bounds.'
Assert-Contains $interaction 'IntersectModelCollisionShapeExact(' `
    'Workspace interaction must delegate narrow-phase picking to the raycast module.'
Assert-Contains $gizmo 'BuildPhysicsShapeDesc(' `
    'Authored collider gizmos and runtime bodies must share one shape conversion contract.'
Assert-Contains $gizmo 'shape.key.sourceShapeId == 0u' `
    'Manual collider gizmos must bypass stale runtime shape caches.'
Assert-Contains $joltShapeFactory 'static_cast<uint32_t>(userData)' `
    'Triangle mesh sub-shapes must preserve their owning Collider identity.'
Assert-NotContains $joltShapeFactory 'static_cast<uint32_t>(index / 3u)' `
    'Triangle indices must not be decoded as Collider indices by runtime filters.'

$settingsPath = Join-Path $repoRoot 'ProjectSettings\Physics\collision.json'
$settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
if ($settings.layers.Count -lt 2 -or $settings.materials.Count -lt 2) {
    throw 'Physics authoring needs named layers and reusable material presets.'
}

Write-Host 'Physics and model-collision contract checks passed.'
