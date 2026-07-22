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

$definition = Read-Source `
    'HIKARI\Animation\StateMachine\HIKARI_AnimationStateMachine.h'
$instance = Read-Source `
    'HIKARI\Animation\StateMachine\HIKARI_AnimationStateMachineInstance.h'
$runtimeService = Read-Source `
    'HIKARI\Animation\StateMachine\HIKARI_AnimationStateMachineRuntimeService.h'
$system = Read-Source `
    'HIKARI\Animation\StateMachine\HIKARI_AnimationStateMachineSystem.cpp'
$component = Read-Source `
    'HIKARI\Scene\Components\HIKARI_AnimationStateMachineComponent.cpp'
$feature = Read-Source `
    'HIKARI\Scene\Features\HIKARI_AnimationRuntimeFeature.cpp'
$workspace = Read-Source `
    'HIKARI\Editor\Workspaces\HIKARI_AnimationStateMachineDetailsPanel.cpp'
$motionEditor = Read-Source `
    'HIKARI\Editor\Workspaces\HIKARI_AnimationStateMachineMotionEditor.cpp'
$runtimeDebug = Read-Source `
    'HIKARI\Editor\Workspaces\HIKARI_AnimationStateMachineRuntimeDebug.cpp'
$motionEvaluator = Read-Source `
    'HIKARI\Animation\StateMachine\HIKARI_AnimationStateMotionEvaluator.cpp'
$graphWorkspace = Read-Source `
    'HIKARI\Editor\Workspaces\HIKARI_AnimationStateMachineGraphPanel.cpp'
$graphMenu = Read-Source `
    'HIKARI\Editor\Workspaces\HIKARI_AnimationStateMachineGraphMenu.cpp'
$documentWorkflow = Read-Source `
    'HIKARI\Editor\Workspaces\HIKARI_AnimationStateMachineDocumentWorkflow.cpp'
$workspaceWindows = Read-Source `
    'HIKARI\Editor\Workspaces\HIKARI_AnimationStateMachineWorkspaceWindows.cpp'
$assetBrowser = Read-Source `
    'HIKARI\Editor\HIKARI_AssetBrowserPanel.cpp'
$sceneEditor = Read-Source `
    'HIKARI\Editor\Controllers\HIKARI_DocumentSceneEditorController.cpp'
$renderingFeature = Read-Source `
    'HIKARI\Scene\Features\HIKARI_RenderingRuntimeFeature.cpp'
$project = Read-Source 'HIKARI_UpdateTo3D.vcxproj'

Assert-Contains $definition 'AnimationParameterSource::Manual' `
    'State machine parameters need a game C++ controlled source.'
Assert-Contains $definition 'AnimationStateMotion motion' `
    'States must own an extensible motion definition.'
Assert-Contains $definition 'AnimationBlendTree1DMotion' `
    'The built-in state motion set must include 1D blend trees.'
Assert-Contains $motionEvaluator 'secondaryWeight' `
    'Blend tree evaluation must resolve a real two-clip motion weight.'
Assert-Contains $instance 'FireTrigger(std::string_view name)' `
    'Game C++ needs a named trigger seam.'
Assert-Contains $runtimeService 'SetFloat(' `
    'The runtime service must expose generic parameter writes.'
Assert-Contains $runtimeService 'bool Start(' `
    'Play On Start false needs an explicit C++ playback seam.'
Assert-Contains $system 'CharacterMotionAnimationBridge' `
    'Character facts must enter through an optional bridge.'
Assert-Contains $component 'AssetType::AnimationStateMachine' `
    'The component must use an animation state machine asset picker.'
Assert-Contains $feature '"AnimationStateMachineSystem",' `
    'Animation feature must register state selection as its own system.'
Assert-Contains $feature '                        140' `
    'State selection must run before pose evaluation.'
Assert-Contains $project 'HIKARI_AnimationStateMachineMotionEditor.cpp' `
    'Motion authoring must stay in a dedicated editor boundary.'
Assert-Contains $motionEditor 'BeginCombo("Motion Type"' `
    'States must expose their motion type in the editor.'
Assert-Contains $motionEditor 'BeginTable("BlendSamples"' `
    '1D blend samples need a structured authoring surface.'
Assert-Contains $runtimeDebug 'Live Runtime' `
    'The state machine workspace must expose live runtime diagnostics.'
Assert-Contains $graphWorkspace 'io.MouseWheel' `
    'The state graph must support pointer-centered wheel zoom.'
Assert-Contains $graphWorkspace 'AnimationStateGraphContext' `
    'The state graph must provide contextual graph editing.'
Assert-Contains $graphWorkspace 'ImGuiWindowFlags_MenuBar' `
    'Document actions belong in menus instead of a floating button pile.'
Assert-Contains $graphMenu 'DrawUnsavedDocumentDialog(' `
    'Document actions must share the unsaved-changes workflow.'
Assert-Contains $documentWorkflow `
    'ImGui::BeginPopupModal(' `
    'Unsaved state machine changes need an explicit modal decision.'
Assert-Contains $documentWorkflow 'Save and Continue' `
    'The unsaved workflow must support saving before continuing.'
Assert-Contains $documentWorkflow 'Discard and Continue' `
    'The unsaved workflow must support deliberately discarding changes.'
Assert-Contains $assetBrowser `
    'ConsumeActivatedAnimationStateMachineGuid' `
    'State machine assets must expose a resource-browser activation seam.'
Assert-Contains $sceneEditor `
    'EditorWorkspaceId::AnimationStateMachine' `
    'Resource-browser activation must route to the state machine workspace.'
Assert-NotContains $workspaceWindows 'AnimationSMToolbar' `
    'The legacy floating state machine toolbar must stay removed.'
Assert-NotContains $renderingFeature 'AnimationStateMachine' `
    'Rendering must not own gameplay animation state selection.'

foreach ($projectEntry in @(
    'HIKARI_AnimationStateMachine.cpp',
    'HIKARI_AnimationStateMachineInstance.cpp',
    'HIKARI_AnimationStateMachineSystem.cpp',
    'HIKARI_AnimationStateMotionEvaluator.cpp',
    'HIKARI_AnimationStateMachineComponent.cpp',
    'HIKARI_AnimationStateMachineMotionEditor.cpp',
    'HIKARI_AnimationStateMachineRuntimeDebug.cpp',
    'HIKARI_AnimationStateMachineGraphMenu.cpp',
    'HIKARI_AnimationStateMachineDocumentWorkflow.cpp',
    'HIKARI_AnimationStateMachineWorkspaceController.cpp')) {
    Assert-Contains $project $projectEntry `
        "Visual Studio project is missing $projectEntry"
}

$legacyV1Asset = Join-Path $repoRoot `
    'Assets\AnimationStateMachines\New_Animation_State_Machine.hanimsm'
if (Test-Path -LiteralPath $legacyV1Asset) {
    throw 'The obsolete V1 animation state machine asset must stay removed.'
}

Write-Host 'Animation state machine boundary contract checks passed.'
