# 编辑器 Play 与呈现目标交接设计

## 目标

为编辑器提供两种职责明确的运行方式，并确保日常调试时不会同时运行两套完整渲染器：

- **在新窗口中运行（Play in New Window）**：在编辑器进程内运行，通过独立游戏窗口呈现；复用编辑器已有的 D3D12 device 和常驻资源，并在 Play 期间完全暂停编辑器渲染。
- **独立运行（Standalone Game）**：继续使用子进程验证打包后的真实运行环境，但在子进程存活期间释放编辑器所持有的项目场景 GPU 常驻资源。

普通 Play 路径必须能够在游戏呈现目标上使用 DLSS、DLAA 和 DLSS 帧生成，同时不得复制城市场景的纹理与几何常驻资源。

## 方案比较

### 跨进程共享 D3D12 Heap

理论上可以把静态纹理和缓冲区建立在 shared heap 中，再由子进程通过 shared handle 打开。但这要求重构所有相关资源分配器，并建立跨进程 fence 和所有权协议。Descriptor heap、PSO、临时渲染目标、时序历史、swapchain 和 Streamline 状态仍然属于各自进程，无法真正共用。

该方案同步和生命周期风险很高，却不能彻底消除重复常驻，因此不采用。

### 在编辑器进程内建立第二套渲染器

在同一进程中再创建一个 `Dx12Core`、渲染器和场景栈虽然能够消除第二个 Windows 进程，但仍会复制 swapchain 相关资源和大量场景运行时状态。当前渲染服务也明确采用进程级全局所有权，因此该方案不采用。

### 独占式呈现目标交接

最终方案只保留一个 D3D12 device、一套渲染服务、一个文档场景资源上下文和一个活动 swapchain。开始 Play 时创建游戏窗口，暂停编辑器 UI 渲染，从已保存文档重建当前场景的干净运行时 World，并把 swapchain 交接到游戏窗口；停止 Play 时执行相反流程，恢复编辑器 World 和呈现目标。

## 运行模式

### 在新窗口中运行

这是工具栏 Play 按钮的默认行为。

1. 验证当前场景具有有效的 Asset GUID。
2. 保存场景文档和渲染质量配置。
3. 保存恢复编辑器所需的会话状态，包括当前相机、Debug Camera 和窗口呈现设置。
4. 从已保存文档重建当前场景的 `World`，保证每次 Play 都从权威序列化状态开始。
5. 把文档场景切换到 Play 状态。Play 状态必须关闭编辑器 Debug Camera 更新、Gizmo、调试辅助绘制、编辑 UI 和编辑器 viewport 呈现。
6. 按保存的质量配置创建独立 Win32 游戏窗口。
7. 等待 GPU idle，暂停 Streamline 帧生成，释放 swapchain 相关的 back buffer 和 depth 资源，为游戏 HWND 创建新 swapchain，并刷新所有 GPU context。
8. 把输入宿主窗口和活动呈现尺寸切换到游戏窗口。
9. 每帧只执行一次现有 update、render、post 和 present 流程。Play 期间不得构建编辑器 viewport 或 ImGui frame。
10. Streamline 帧生成策略和性能统计必须把该窗口视为真实游戏呈现目标。

关闭游戏窗口或点击 Stop 都进入同一套有序停止流程。关闭游戏窗口不得向整个应用发送 `WM_QUIT`。

### 独立运行

Standalone 继续通过包含 `GamePreview` runtime config 的命令行启动当前可执行文件的子进程。恢复子进程执行之前，编辑器必须：

1. 保存与同进程 Play 相同的场景和质量配置。
2. 等待编辑器 GPU idle。
3. 释放 Post、Temporal 和 Streamline 临时资源。
4. 释放项目场景持有的文档 World、模型与几何注册表、GPU Scene、材质表、项目纹理、Sky/IBL 和编辑器 viewport 资源，同时保留恢复编辑器所需的 CPU 场景文档和 Asset Database 元数据。
5. 最小化编辑器并停止父进程帧循环。

子进程退出后，编辑器按照资源所有权顺序恢复渲染服务，从已保存场景重建文档运行时 World，重置时序历史，恢复编辑器窗口并继续帧循环。

如果释放或恢复过程失败，Standalone 必须进入明确的失败状态并报告原因，不能在只完成部分停车的情况下继续启动。

## 组件边界

### `EditorPlaySession`

负责同进程 Play 的状态机：`Stopped`、`Starting`、`Running`、`Stopping` 和 `Failed`。它负责协调场景 Play 状态与呈现目标交接，但不能直接创建 D3D12 资源。

输入：

- 当前 `DocumentSceneBase`
- Project Root 与场景 GUID
- 当前渲染质量配置

输出：

- 主循环当前应绘制编辑器 UI 还是只绘制游戏输出
- 工具栏使用的状态文本和失败原因
- 提交给呈现目标控制器的开始与停止请求

### `GamePresentationWindow`

只负责第二个 Win32 窗口及其关闭、Resize 信号。该窗口采用非应用级关闭策略：销毁窗口只把自身标记为已关闭，不能调用 `PostQuitMessage`。它不能依赖 D3D12 或场景系统。

### `PresentationTargetController`

负责单一 `Dx12Core` 的活动 HWND 交接。它保存编辑器呈现目标，把 Core 重新绑定到游戏目标，刷新依赖 Context，并在失败时执行原子回滚。输入宿主窗口切换和 Streamline swapchain 检查也由该控制器协调。

### `Dx12Core`

新增呈现目标重绑定操作。该操作必须保留 adapter、device、command queue、descriptor heap、command allocator、fence 和所有非 swapchain 资源，只允许重建：

- DXGI swapchain 和 frame-latency handle
- swapchain back buffer 与 RTV
- 呈现深度缓冲及其 descriptor
- 与活动呈现目标绑定的尺寸和 frame index

### `DocumentSceneBase`

负责当前 World 进入和退出运行时 Play 状态。它保存序列化 `SceneDocument` 和编辑器相机状态，在 Play 开始时重建干净 World，在运行期间关闭编辑器专属行为，并在停止时重新加载和重建编辑器 World。它不能拥有窗口或呈现资源。

### `StandaloneGamePreviewSession`

继续负责子进程创建、Job Object 生命周期和 runtime config 写入。资源停车委托给独立的渲染常驻协调器，避免进程管理代码跨越渲染资源所有权边界。

## 主循环契约

任意时刻只能有一个 Frame Producer：

- 编辑器状态：更新并渲染编辑器 World，合成编辑器 viewport，绘制并呈现 ImGui。
- 同进程 Play 状态：更新并渲染游戏 World，直接呈现到游戏窗口；不建立编辑器 ImGui frame，不合成编辑器 viewport。
- Standalone 状态：父编辑器不生产帧，子进程独占游戏 update、render 和 presentation。

主循环不得在同一帧同时更新或渲染编辑器路径与游戏路径。

## Streamline 与帧生成

Streamline 始终挂接在现有 D3D12 device 上。呈现目标交接必须先暂停帧生成，再销毁旧 swapchain；新 swapchain 建立后重新执行检查，更新 Streamline Graphics Context，重置 Temporal 资源，并且只在新的游戏输入资源有效后恢复策略评估。

帧生成当前使用的 `standaloneGame` 判断应替换为明确的 `gamePresentationActive` 判断。它在导出游戏、Standalone 和同进程 Play 下为 `true`，在编辑器 viewport 下为 `false`。

呈现目标、窗口尺寸、渲染尺寸或 Play 状态发生变化时，必须让过渡帧的 Temporal History 和帧生成输入失效。

## 错误处理与回滚

- Swapchain 交接开始前失败时，编辑器保持原状。
- 释放编辑器呈现目标后失败时，必须先尝试重建编辑器 swapchain，再报告失败。
- 游戏窗口 Resize 只入队，在帧边界统一处理。
- 关闭游戏窗口只请求停止 Play，不得退出编辑器进程。
- Device Removed 继续使用现有致命设备丢失流程，不尝试呈现回滚。
- 只有父进程资源停车完整成功后才能启动 Standalone 子进程；子进程创建失败时立即执行恢复。
- 恢复失败时进入失败状态并保持渲染暂停，不得继续使用部分初始化的资源。

## 用户界面

现有工具栏 Play 图标用于开始或停止“在新窗口中运行”。相邻菜单提供明确的 **Standalone Game** 验证入口。状态文本分别显示 `Play Running`、`Standalone Running` 和失败状态，两种运行模式互斥。

游戏窗口不绘制任何编辑器面板。现有游戏 UI Composition 仍位于帧生成之前，继续满足当前 HUD-less Color 和 UI Color/Alpha 契约。

## 验证方案

自动化契约测试覆盖：

- Play 与 Standalone 互斥及其状态转换。
- 关闭第二窗口不会触发应用退出。
- 呈现目标重绑定会改变 swapchain/HWND，但保持 D3D12 device identity 不变。
- 同进程 Play 期间不会生成编辑器 UI frame。
- Game Presentation Policy 会为同进程 Play 启用帧生成。
- Play 开始时运行时状态被重置，Play 停止时编辑器状态被重新构建。
- Standalone 在完整停车成功前不能启动子进程，并且所有退出路径都会执行恢复。

运行时验证覆盖：

- 编辑器到 Play 再回到编辑器的 720p、1080p、1440p 和 4K 往返流程。
- Windowed、Borderless、最大化、Resize、Alt+F4 和工具栏 Stop。
- TAA、DLAA、DLSS 以及各支持倍率的 DLSS 帧生成。
- 通过 Task Manager 和 PIX 确认普通 Play 只有一个进程和一份场景常驻资源。
- 确认 Standalone 子进程开始渲染之前，父进程 GPU 常驻已经下降。
- Play 期间发生的场景状态变化在停止后不会写回编辑器文档；未来只有显式运行时保存功能可以改变这一点。

## 非目标

- 跨进程共享 D3D12 Heap。
- 同时渲染编辑器窗口和游戏窗口。
- 把运行时 World 变化自动写回编辑器文档。
- 同时运行多个游戏窗口。
- 替换现有打包和导出运行时路径。
