# 编辑器 Play 与呈现目标交接实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标：** 实现同进程独立游戏窗口 Play，并保留具有完整父进程资源停车能力的 Standalone 验证模式。

**架构：** 引擎始终只保留一个 D3D12 device、一个活动 swapchain 和一个帧生产者。同进程 Play 复用当前 `DocumentSceneBase` 的资源上下文，从保存文档重建运行时 World，并把呈现目标从编辑器 HWND 原子切换到游戏 HWND；Standalone 仍使用子进程，但必须在子进程启动前完成父进程场景 GPU 常驻停车。

**技术栈：** C++20、Win32、Direct3D 12、DXGI flip-model swapchain、NVIDIA Streamline、PowerShell 契约测试、MSBuild/Visual Studio 2022。

## 全局约束

- 不得重置、覆盖或顺带提交当前工作区中已有的 DLSS-G、质量设置和编辑器改动。
- 任意时刻只能有一个 Frame Producer 和一个活动 swapchain。
- 同进程 Play 不创建第二套 `Dx12Core`、`ModelManager` 或 `DocumentSceneBase`。
- 关闭游戏窗口不得发送应用级 `WM_QUIT`。
- 所有 swapchain 交接都发生在 GPU frame 之外，并具有编辑器目标回滚路径。
- Streamline 帧生成必须在旧 swapchain 销毁前暂停，并在新目标输入有效后恢复。
- Play 期间不建立编辑器 ImGui frame，不渲染编辑器 viewport。
- Play 运行时状态停止后不得写回保存的编辑器场景文档。

---

### 任务 1：建立 Play 生命周期契约测试

**文件：**
- 新建：`Tools/Tests/Test-EditorPlayContracts.ps1`
- 修改：`Tools/Tests/Test-StreamlineLifecycleContracts.ps1`

**接口：**
- 消费：现有源码文件。
- 产出：可独立运行的静态契约测试，覆盖窗口关闭、swapchain 重绑定、唯一帧生产者、Play 场景生命周期和游戏呈现策略。

- [ ] **步骤 1：编写失败契约**

测试必须检查以下标识和函数体关系：

```powershell
Assert-Contains $windowHeader 'enum class WindowCloseBehavior'
Assert-Contains $windowSource 'WindowCloseBehavior::QuitApplication'
Assert-Contains $coreHeader 'bool RebindPresentationTarget('
Assert-Contains $services 'inline bool BeginInProcessGamePresentation('
Assert-Contains $services 'inline bool IsGamePresentationActive()'
Assert-Contains $sceneHeader 'bool BeginRuntimePlay()'
Assert-Contains $sceneHeader 'bool EndRuntimePlay()'
Assert-Contains $main 'app.UpdatePlaySessions();'
Assert-Contains $main 'app.IsStandalonePlayRunning()'
```

- [ ] **步骤 2：运行测试并确认 RED**

运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\Tests\Test-EditorPlayContracts.ps1
```

预期：测试因 `WindowCloseBehavior` 或 `RebindPresentationTarget` 尚不存在而失败。

- [ ] **步骤 3：保留现有 Streamline 契约作为回归测试**

运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\Tests\Test-StreamlineLifecycleContracts.ps1
```

预期：`Streamline lifecycle contract checks passed.`

### 任务 2：实现第二窗口关闭语义与 swapchain 原子重绑定

**文件：**
- 修改：`HIKARI/Platform/HIKARI_Win32Window.h`
- 修改：`HIKARI/Platform/HIKARI_Win32Window.cpp`
- 修改：`HIKARI/Gfx/HIKARI_Dx12Core.h`
- 修改：`HIKARI/Gfx/HIKARI_Dx12Core.cpp`

**接口：**
- 产出：

```cpp
enum class WindowCloseBehavior { QuitApplication, SignalOnly };

bool Win32Window::Initialize(
    const wchar_t* title,
    int width,
    int height,
    bool resizable,
    WindowCloseBehavior closeBehavior = WindowCloseBehavior::QuitApplication,
    bool dispatchImGuiInput = true);

bool Dx12Core::RebindPresentationTarget(HWND hwnd, int width, int height);
HWND Dx12Core::PresentationWindow() const;
```

- [ ] **步骤 1：实现窗口角色**

`SignalOnly` 窗口在 `WM_CLOSE/WM_DESTROY` 时只更新自身 `running_`，不得调用 `PostQuitMessage`；`QuitApplication` 保持现有主窗口语义。游戏窗口关闭后仍由主窗口 `PumpMessages()` 继续应用消息循环。

- [ ] **步骤 2：提取 swapchain 创建与释放**

把 `Dx12Core::Initialize` 中的 swapchain 创建逻辑提取为私有 `CreateSwapChainForPresentationTarget`。重绑定只释放 back buffer、depth buffer 和旧 swapchain，保留 device、queue、descriptor heap、command allocator 与 fence。

- [ ] **步骤 3：实现原子重绑定与回滚**

`RebindPresentationTarget` 必须在 frame 关闭状态等待 GPU idle，忘记旧 presentation resource state，创建新目标并刷新 frame index。创建失败时必须尝试恢复旧 HWND 和尺寸。

- [ ] **步骤 4：运行契约测试与构建**

运行 `Test-EditorPlayContracts.ps1`，预期窗口和 Core 契约转绿，Services/Scene 契约继续保持 RED；随后运行 `EditorRelease|x64` 构建。

### 任务 3：建立同进程游戏呈现控制器

**文件：**

- 新建：`HIKARI/Runtime/HIKARI_GamePresentationController.h`
- 新建：`HIKARI/Runtime/HIKARI_GamePresentationController.cpp`
- 修改：`HIKARI/HIKARI_Services.h`
- 修改：`HIKARI_UpdateTo3D.vcxproj`
- 修改：`HIKARI_UpdateTo3D.vcxproj.filters`

**接口：**

```cpp
bool BeginInProcessGamePresentation();
bool EndInProcessGamePresentation();
bool IsInProcessGamePresentationActive();
bool ConsumeInProcessGameCloseRequest();
bool IsGamePresentationActive();
bool ShouldProduceEditorUiFrame();
```

- [ ] 创建只拥有第二窗口与 presentation 状态的控制器，不让它拥有场景。
- [ ] 开始交接时创建游戏窗口、暂停 FG、重绑定 swapchain、刷新 GPU context、切换输入 HWND，并在成功后隐藏编辑器窗口。
- [ ] 停止交接时先把 swapchain 与输入恢复到编辑器 HWND，再销毁游戏窗口并恢复编辑器。
- [ ] 游戏窗口 Resize 只在帧边界进入现有 pending resize 流程。
- [ ] `BeginFrame` 和 `EndFrame` 在游戏呈现状态下不得建立、渲染或更新 ImGui platform window。
- [ ] 把 FG 的 `standaloneGame` 语义替换为 `gamePresentationActive`。
- [ ] 运行两个契约测试并执行 `EditorRelease|x64` 构建。

### 任务 4：建立场景运行时 Play 状态

**文件：**

- 修改：`HIKARI/Scene/Scenes/HIKARI_DocumentSceneBase.h`
- 修改：`HIKARI/Scene/Scenes/HIKARI_DocumentSceneBase.cpp`

**接口：**

```cpp
bool BeginRuntimePlay();
bool EndRuntimePlay();
bool IsRuntimePlayActive() const;
```

- [ ] Play 开始前保存 Camera 和 Debug Camera 状态。
- [ ] 从已保存 `SceneDocument` 重建 World，清除编辑选择与运行时残留。
- [ ] Play 期间关闭 Debug Camera、Gizmo、Debug Helper 和编辑器性能覆盖逻辑。
- [ ] 停止 Play 时重新加载保存的 SceneDocument、重建编辑器 World，并恢复编辑器相机。
- [ ] 任意失败路径都恢复 `runtimePlayActive_ = false`，并提供可诊断返回值。
- [ ] 运行契约测试和构建。

### 任务 5：建立统一编辑器 Play 会话状态机

**文件：**

- 新建：`HIKARI/Editor/Play/HIKARI_EditorPlaySession.h`
- 新建：`HIKARI/Editor/Play/HIKARI_EditorPlaySession.cpp`
- 修改：`HIKARI/App/HIKARI_EngineApp.h`
- 修改：`HIKARI/App/HIKARI_EngineApp.cpp`
- 修改：`HIKARI/Editor/Controllers/HIKARI_DocumentSceneEditorController.h`
- 修改：`HIKARI/Editor/Controllers/HIKARI_DocumentSceneEditorController.cpp`
- 修改：`main.cpp`
- 修改：`HIKARI_UpdateTo3D.vcxproj`
- 修改：`HIKARI_UpdateTo3D.vcxproj.filters`

**接口：**

```cpp
enum class EditorPlayMode { None, InProcess, Standalone };
enum class EditorPlayState { Stopped, Starting, Running, Stopping, Failed };

void RequestInProcessStart();
void RequestStandaloneStart();
void RequestStop();
void Update(DocumentSceneBase& scene);
```

- [ ] UI 只提交请求，不在打开的 GPU frame 内切换 swapchain。
- [ ] `EngineApp::UpdatePlaySession` 在 `BeginFrame` 前处理请求与窗口关闭信号。
- [ ] 默认 Play 图标启动同进程模式，相邻菜单显式启动 Standalone。
- [ ] 两种模式互斥，失败信息显示在现有 Game View toolbar。
- [ ] 主循环在同进程 Play 下继续生产游戏帧，在 Standalone 下等待子进程且不生产父进程帧。
- [ ] `CloseProgram` 在同进程 Play 下请求 Stop，而不是退出编辑器。
- [ ] 运行契约测试和构建。

### 任务 6：扩展 Standalone GPU 常驻停车

**文件：**

- 修改：`HIKARI/Runtime/HIKARI_RuntimeRenderResourceParking.h`
- 修改：`HIKARI/Runtime/HIKARI_RuntimeRenderResourceParking.cpp`
- 修改：`HIKARI/Editor/Play/HIKARI_EditorPlaySession.cpp`
- 修改：`HIKARI/Scene/Scenes/HIKARI_DocumentSceneBase.h`
- 修改：`HIKARI/Scene/Scenes/HIKARI_DocumentSceneBase.cpp`

**接口：**

```cpp
bool ParkEditorForStandalone(DocumentSceneBase& scene, GFX::Dx12Core& core);
bool RestoreEditorAfterStandalone(DocumentSceneBase& scene, const GFX::Context& context);
```

- [ ] 停车前 detach scheduler、清空 World 并使 GPU Scene/Render Submission 失效。
- [ ] 卸载 `ModelManager` 中已加载模型及其材质纹理引用。
- [ ] 释放 Cluster Geometry、GPU Material、Sky/IBL、项目纹理和临时渲染资源，同时保留恢复所需文档元数据。
- [ ] 子进程只在完整停车成功后恢复执行。
- [ ] 子进程创建失败、正常退出、异常退出和手动 Stop 都执行同一个恢复流程。
- [ ] 恢复顺序与初始化所有权相反，并在最后重建文档 World 与 Temporal History。
- [ ] 运行契约测试和构建。

### 任务 7：运行时验证与收口

**文件：**

- 修改：`Tools/Tests/Test-EditorPlayContracts.ps1`
- 修改：`Tools/Tests/Test-StreamlineLifecycleContracts.ps1`

- [ ] 运行所有 PowerShell 契约测试。
- [ ] 运行 `EditorRelease|x64` 与 `Release|x64` 构建。
- [ ] 启动 EditorRelease，验证编辑器空闲、普通 Play、关闭游戏窗口、恢复编辑器的完整往返。
- [ ] 检查日志中只有一次 device 初始化，且 handoff 前后 swapchain HWND 不同。
- [ ] 检查普通 Play 只有一个 HIKARI 进程。
- [ ] 验证 Standalone 仍创建一个子进程，并记录父进程停车与恢复日志。
- [ ] 运行 `git diff --check`，确认无空白错误且未改动无关文件。
