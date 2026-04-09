# UpdateTest cleanup audit（本轮）

## 1. 当前图形主链

当前运行主链（按帧生命周期）为：

1. `Platform/HIKARI_Win32Window.*`：窗口与消息循环。
2. `Gfx/HIKARI_Dx12Core.*`：DX12 设备、交换链、命令队列、帧 begin/end。
3. `Gfx/HIKARI_GfxContext.h`：设备/命令列表/SRV Heap 等上下文桥接。
4. `Render2D/HIKARI_DxTexture.*`：贴图资源管理。
5. `Render2D/HIKARI_DxRenderer.*`：底层 Draw 提交。
6. `Render2D/HIKARI_PostSystem.*`（及 PostChain / PostEffect / PostQuadDrawer）：后处理。
7. `Render3D/HIKARI_Renderer3D.*` + `Render3D/HIKARI_Renderer3D_Debug.*`：当前 3D 调试图元层（线框/辅助绘制）。
8. `Render2D/HIKARI_Renderer.*`：2D/最终层队列绘制与 UI 叠加。

## 2. 当前仍保留的过渡依赖

工程配置中仍可见以下 Kamata/Novice 过渡依赖（本轮只记录，不激进删除）：

- `.vcxproj` 仍链接 `KamataEngine.lib`。
- `.vcxproj` 仍包含 `C:\KamataEngine\Adapter\Novice.cpp` 编译项。
- `.vcxproj` 仍保留 Kamata include path（`C:\KamataEngine\...`）与 library path。
- 工程根仍存在 `NoviceResources/` 资源目录。
- `PostBuildEvent` 仍使用 `xcopy` 从 Kamata 路径复制资源。

## 3. 当前音频后端状态

- 音频入口保持 `Audio/HIKARI_Audio.*` 抽象。
- 后端接口为 `Audio/HIKARI_IAudioBackend.h`。
- 当前默认后端仍为 `Audio/Backends/HIKARI_AudioBackend_Novice.*`。
- 图形主链可独立于音频后端实现演进；音频后端仍可在后续轮次替换。

## 4. Legacy 区列表

本轮归类为 Legacy（过渡模块）并独立到 `HIKARI/Legacy/`：

- `HIKARI_Camera2_5D.*`
- `HIKARI_RenderQueue25.*`

> 说明：本轮策略是“隔离+标识+减耦合”，不是一次性删除。

## 5. 第二轮建议方向（仅规划）

1. 基于上述清单，逐项验证 Kamata/Novice 必需项。
2. 将可替代依赖转为本地模块接口后，再进行工程配置瘦身。
3. 逐步把 Debug 3D 与正式 Model/Mesh 渲染路径分离到不同实现单元。
