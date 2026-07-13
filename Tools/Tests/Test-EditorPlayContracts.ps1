$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Read-Source([string]$relativePath) {
    $path = Join-Path $repoRoot $relativePath
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Source file not found: $relativePath"
    }
    return [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
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

function Get-CppFunctionBody([string]$source, [string]$signature) {
    $signatureIndex = $source.IndexOf($signature, [StringComparison]::Ordinal)
    if ($signatureIndex -lt 0) {
        throw "Function signature not found: $signature"
    }
    $bodyStart = $source.IndexOf('{', $signatureIndex)
    if ($bodyStart -lt 0) {
        throw "Function body not found: $signature"
    }
    $depth = 0
    for ($index = $bodyStart; $index -lt $source.Length; ++$index) {
        if ($source[$index] -eq '{') {
            ++$depth
        }
        elseif ($source[$index] -eq '}') {
            --$depth
            if ($depth -eq 0) {
                return $source.Substring($bodyStart, $index - $bodyStart + 1)
            }
        }
    }
    throw "Function body is incomplete: $signature"
}

$windowHeader = Read-Source 'HIKARI\Platform\HIKARI_Win32Window.h'
$windowSource = Read-Source 'HIKARI\Platform\HIKARI_Win32Window.cpp'
$debugCameraHeader = Read-Source 'HIKARI\Render3D\Debug\HIKARI_DebugCameraController3D.h'
$debugCameraSource = Read-Source 'HIKARI\Render3D\Debug\HIKARI_DebugCameraController3D.cpp'
$coreHeader = Read-Source 'HIKARI\Gfx\HIKARI_Dx12Core.h'
$coreSource = Read-Source 'HIKARI\Gfx\HIKARI_Dx12Core.cpp'
$streamlineRuntimeHeader = Read-Source 'HIKARI\Render3D\Upscaling\HIKARI_StreamlineRuntime.h'
$services = Read-Source 'HIKARI\HIKARI_Services.h'
$sceneHeader = Read-Source 'HIKARI\Scene\Scenes\HIKARI_DocumentSceneBase.h'
$sceneSource = Read-Source 'HIKARI\Scene\Scenes\HIKARI_DocumentSceneBase.cpp'
$appHeader = Read-Source 'HIKARI\App\HIKARI_EngineApp.h'
$appSource = Read-Source 'HIKARI\App\HIKARI_EngineApp.cpp'
$playSessionHeader = Read-Source 'HIKARI\Editor\Play\HIKARI_EditorPlaySession.h'
$playSessionSource = Read-Source 'HIKARI\Editor\Play\HIKARI_EditorPlaySession.cpp'
$parkingSource = Read-Source 'HIKARI\Runtime\HIKARI_RuntimeRenderResourceParking.cpp'
$main = Read-Source 'main.cpp'
$controllerSource = Read-Source 'HIKARI\Editor\Controllers\HIKARI_DocumentSceneEditorController.cpp'

Assert-Contains $windowHeader 'enum class WindowCloseBehavior' `
    'Win32Window must distinguish application-close and signal-only windows.'
Assert-Contains $windowHeader 'WindowCloseBehavior closeBehavior_' `
    'Each Win32Window must own its close behavior.'
$windowProcedure = Get-CppFunctionBody $windowSource 'LRESULT Win32Window::WndProc('
Assert-Contains $windowProcedure 'WindowCloseBehavior::QuitApplication' `
    'Only the application window may post WM_QUIT.'

Assert-Contains $coreHeader 'bool RebindPresentationTarget(' `
    'Dx12Core must rebind presentation without recreating the device.'
$rebindBody = Get-CppFunctionBody $coreSource 'bool Dx12Core::RebindPresentationTarget('
Assert-Contains $rebindBody 'WaitGPU()' `
    'Presentation rebind must wait for GPU idle.'
Assert-Contains $rebindBody 'candidateSwapChain' `
    'Presentation rebind must create a candidate before releasing the active target.'
Assert-Contains $rebindBody 'previousSwapChain' `
    'Presentation rebind must retain the previous swap chain for atomic rollback.'

Assert-Contains $debugCameraHeader 'enum class CameraControlInputContext' `
    'Free camera input must distinguish editor capture from runtime-window ownership.'
$cameraUpdate = Get-CppFunctionBody $debugCameraSource 'void DebugCameraController3D::Update('
Assert-Contains $cameraUpdate 'CameraControlInputContext::EditorViewport' `
    'Only editor-view camera input may be gated by ImGui capture state.'

Assert-Contains $services 'inline bool BeginInProcessGamePresentation(' `
    'Services must expose frame-boundary game presentation handoff.'
Assert-Contains $services 'inline bool EndInProcessGamePresentation(' `
    'Services must restore editor presentation through one owner.'
Assert-Contains $services 'inline bool IsGamePresentationActive()' `
    'Frame generation policy needs explicit game-presentation state.'
Assert-Contains $services 'inline bool ShouldProduceEditorUiFrame()' `
    'Main loop and ImGui backend need one editor-frame predicate.'
Assert-Contains $services 'inline void UpdateGamePresentationPerformanceTitle()' `
    'Both Play modes need one presentation telemetry title path.'
$presentationTelemetry = Get-CppFunctionBody $services 'inline void UpdateGamePresentationPerformanceTitle()'
Assert-Contains $presentationTelemetry 'IsInProcessGamePresentationActive()' `
    'In-process Play must expose render and display FPS for DLSS-G validation.'
$endFrame = Get-CppFunctionBody $services 'inline bool EndFrame()'
Assert-Contains $endFrame 'UpdateGamePresentationPerformanceTitle();' `
    'Presentation telemetry must update after each completed presented frame.'
$resizePresentation = Get-CppFunctionBody $services 'inline bool ApplyPendingWindowResize()'
Assert-Contains $resizePresentation 'SuspendStreamlineFrameGeneration(false);' `
    'Ordinary resize should suspend DLSS-G without forcing a full resource release.'
$beginPresentation = Get-CppFunctionBody $services 'inline bool BeginInProcessGamePresentation()'
Assert-Contains $beginPresentation 'gCore.WaitForIdle()' `
    'Entering Play must flush GPU work before changing Streamline feature lifetime.'
Assert-Contains $beginPresentation 'SuspendStreamlineFrameGeneration(true);' `
    'Entering Play must release DLSS-G resources tied to the editor swap chain.'
Assert-Contains $beginPresentation 'SetStreamlineFrameGenerationFeatureLoaded(true)' `
    'Entering Play must load DLSS-G before the game swap chain is created.'
$endPresentation = Get-CppFunctionBody $services 'inline bool EndInProcessGamePresentation()'
Assert-Contains $endPresentation 'gCore.WaitForIdle()' `
    'Leaving Play must flush GPU work before changing Streamline feature lifetime.'
Assert-Contains $endPresentation 'SuspendStreamlineFrameGeneration(true);' `
    'Leaving Play must release DLSS-G resources tied to the game swap chain.'
Assert-Contains $endPresentation 'SetStreamlineFrameGenerationFeatureLoaded(false)' `
    'Leaving Play must unload DLSS-G before the editor swap chain is created.'
Assert-Contains $streamlineRuntimeHeader 'SetStreamlineFrameGenerationFeatureLoaded' `
    'Streamline runtime must own dynamic DLSS-G feature lifetime.'

Assert-Contains $sceneHeader 'bool BeginRuntimePlay();' `
    'DocumentSceneBase must own runtime World reset at Play start.'
Assert-Contains $sceneHeader 'bool EndRuntimePlay();' `
    'DocumentSceneBase must restore the saved editor World at Play stop.'
$drawDebugHelpers = Get-CppFunctionBody $sceneSource 'bool DocumentSceneBase::DrawDebugHelpers() const'
Assert-Contains $drawDebugHelpers 'runtimePlayActive_' `
    'Editor debug helpers must be suppressed during runtime Play.'
$beginRuntimePlay = Get-CppFunctionBody $sceneSource 'bool DocumentSceneBase::BeginRuntimePlay()'
Assert-NotContains $beginRuntimePlay 'RebuildRuntimeWorld()' `
    'ReloadSceneDocument already rebuilds the World; Play must not rebuild it twice.'
Assert-NotContains $beginRuntimePlay 'camera_ = Camera3D{}' `
    'Play must not replace the editor viewpoint with a hard-coded camera.'
Assert-Contains $beginRuntimePlay 'runtimePreviewCamera_ = editorDebugCameraSnapshot_' `
    'Play must initialize a separate runtime preview camera from the editor viewpoint.'
$sceneUpdate = Get-CppFunctionBody $sceneSource 'void DocumentSceneBase::Update(float dt)'
Assert-Contains $sceneUpdate 'CameraControlInputContext::RuntimeWindow' `
    'Runtime preview camera input must bypass stale editor ImGui capture state.'
Assert-Contains $sceneHeader 'bool ParkRuntimeForStandalone();' `
    'DocumentSceneBase must expose scene-owned Standalone parking.'
Assert-Contains $parkingSource 'ShutdownClusterGeometryResourceSystem()' `
    'Standalone parking must release resident cluster geometry.'

Assert-Contains $appHeader 'void UpdatePlaySessions();' `
    'EngineApp must process Play requests before BeginFrame.'
Assert-Contains $appHeader 'bool IsStandalonePlayRunning() const;' `
    'Main loop must distinguish paused-parent Standalone from in-process Play.'
Assert-Contains $playSessionHeader 'enum class EditorPlayMode' `
    'One session owner must distinguish in-process and Standalone Play.'
Assert-Contains $playSessionSource 'ConsumeInProcessGameCloseRequest()' `
    'Closing the game window must request Play stop through the session owner.'
Assert-Contains $playSessionSource 'ParkEditorForStandalone(scene)' `
    'Standalone launch must park the parent renderer before starting the child.'

Assert-Contains $main 'app.UpdatePlaySessions();' `
    'Main loop must update Play state before opening a GPU frame.'
Assert-Contains $main 'app.IsStandalonePlayRunning()' `
    'Only Standalone may pause the parent frame producer.'
Assert-Contains $main 'SERVICES::ShouldProduceEditorUiFrame()' `
    'Editor ImGui production must use the centralized presentation predicate.'

Assert-Contains $controllerSource 'Play in New Window' `
    'The default Play action must identify the in-process mode.'
Assert-Contains $controllerSource 'Standalone Game' `
    'Standalone validation must remain an explicit separate action.'

Write-Host 'Editor Play contract checks passed.'
