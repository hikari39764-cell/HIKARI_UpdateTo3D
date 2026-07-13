# HIKARI 项目级多智能体开发队伍设计

**状态：** 已获 Engine Director 批准，可进入实施规划。

## 1. 目标

为当前自研 DirectX 12 游戏引擎建立项目级 Codex 多智能体协作框架。本阶段只交付 Agent 配置、项目级协作规则和代码库入职文档框架，不调查或修改引擎实现。

完成后，Codex 应能从受信任的项目根目录发现八个职责明确的自定义 Agent。Engine Lead 作为主要调度入口，专业 Agent 在单一主要写入负责人制度下执行调查、实现、性能分析和审核。

## 2. 非目标

- 不修改 C++、HLSL、资源、场景、Visual Studio 工程或构建流程。
- 不启动代码库入职分析，不把历史文档或既有推测写成当前事实。
- 不改变 GPU Based Batch Rendering、Cluster/Meshlet、Probe Capture/Bake、反射或 GI 路线。
- 不删除、重命名或移动现有的空 `.agents/` 目录。
- 不固定 Agent 使用的模型或推理强度。

## 3. 当前仓库状态与兼容策略

- 根目录 `.codex/` 已存在但为空，在其中新增项目配置和 Agent 文件。
- 根目录 `.agents/` 已存在但为空，原样保留。
- 根目录不存在 `AGENTS.md`，因此创建基础项目规则。
- 未发现旧 Agent TOML，不需要迁移或覆盖。
- 工作区存在大量未提交的引擎、DLSS、运行时和工程文件改动。这些内容属于现有工作，本任务只新增约定范围内的配置和文档。
- `docs/superpowers/` 已有其他设计规格，必须保留；新的入职、架构、路线图和性能目录与其并存。

## 4. 配置结构

### 4.1 项目级并发配置

创建 `.codex/config.toml`，内容只包含：

```toml
[agents]
max_threads = 4
max_depth = 1
```

`max_depth = 1` 允许根会话生成直接子 Agent，但禁止子 Agent 继续递归生成更深层 Agent。

### 4.2 模型和推理强度继承

八个 Agent 文件都省略 `model` 和 `model_reasoning_effort`。每个 Agent 直接继承 Engine Director 在当前父会话中选择的模型和推理强度，避免模型名称、账户权限或可用性变化导致项目配置失效。

### 4.3 Agent 文件

在 `.codex/agents/` 创建以下独立 TOML：

| 文件 | `name` | 默认权限 | 核心边界 |
|---|---|---|---|
| `engine-lead.toml` | `engine_lead` | `read-only` | 调查、规划、调度、汇总和维护决策；架构级修改必须等待 Engine Director 批准 |
| `codebase-cartographer.toml` | `codebase_cartographer` | `read-only` | 首次接手时建立带路径和符号证据的代码库地图；未知项明确标记 `Unknown` |
| `rendering-gpu-engineer.toml` | `rendering_gpu_engineer` | `workspace-write` | DirectX 12、渲染管线、HLSL、GPU Driven、时序和 GPU 生命周期；不得越过资源和场景边界 |
| `core-runtime-platform-engineer.toml` | `core_runtime_platform_engineer` | `workspace-write` | Engine Loop、Scene/Component 生命周期、平台和 Renderer 接口；不得修改渲染算法或 HLSL |
| `resource-editor-engineer.toml` | `resource_editor_engineer` | `workspace-write` | 导入、Cook、自定义格式、序列化、资源上传和编辑器；格式变更必须带迁移方案 |
| `performance-architect.toml` | `performance_architect` | `read-only` | 基准、CPU/GPU Frame Time、卡顿、内存和 IO；没有测量不得声称优化 |
| `codebase-steward.toml` | `codebase_steward` | `read-only` | 模块边界、命名、依赖和技术债；只提出小步、可验证、可回退的整理方案 |
| `qa-integration-reviewer.toml` | `qa_integration_reviewer` | `read-only` | 独立验证和集成审核；报告问题，由原实现负责人修复后复审 |

每个 Agent 文件必须包含非空的 `name`、`description` 和 `developer_instructions`。`developer_instructions` 写入附件定义的完整职责、限制、证据标准和交接要求。实现型角色的 `workspace-write` 只是默认工具权限，不能绕过 Engine Director 审批、单一写入负责人或任务范围限制。

## 5. 项目级协作规则

根目录 `AGENTS.md` 记录以下规则：

- Engine Director 拥有最终方向和架构批准权，Engine Lead 是智能体队伍主要调度入口。
- 每项任务只能指定一个主要代码写入负责人。
- 多 Agent 可以并行调查、验证和审核，但不得同时修改相同公共文件。
- 大型架构修改、公共接口变化、资源或序列化格式变化、主要渲染路线变化必须先提交计划并等待批准。
- 保留 Debug、PIX、日志、对象命名和诊断能力。
- 文件存在不代表系统完成；当前路径未调用某文件也不代表该文件可删除。
- 推测和无法确认的结论必须明确标记，文档与真实代码冲突时以代码和实际运行路径为准并记录文档过时问题。
- 修改后执行与范围匹配的构建、测试或运行验证。
- 项目背景中的 GPU Based Batch Rendering、Cluster/Meshlet、Resource Cook、Reflection Probe/Bake、GI 和性能状态全部放入“待代码验证的背景”章节，不写成已确认事实。

## 6. 入职文档框架

创建以下十九个文件：

```text
docs/
  onboarding/
    project-overview.md
    build-and-run.md
    current-engine-status.md
    runtime-flow.md
    rendering-flow.md
    asset-flow.md
    editor-flow.md
    known-issues.md
  architecture/
    module-map.md
    dependency-map.md
    ownership.md
    architecture-decisions.md
    project-intent.md
  roadmap/
    current-milestone.md
    completed-work.md
    in-progress-work.md
    planned-work.md
  performance/
    baseline.md
    benchmark-rules.md
```

每个文件只包含：

1. 与文件名对应的标题。
2. 精确状态行 `Status: Unknown — awaiting codebase onboarding`。
3. 该文档未来应记录的内容范围。
4. 写入事实前必须收集的代码、构建、运行、性能或版本控制证据。
5. 禁止猜测或把计划状态写成完成状态的说明。

不同文件可以有不同的范围和证据清单，但不得填入未经本轮代码库调查验证的项目事实。

## 7. 验证设计

实施完成后执行以下验证：

1. 用 Python `tomllib` 解析 `.codex/config.toml` 和八个 Agent TOML。
2. 检查 `max_threads = 4`、`max_depth = 1`。
3. 检查每个 Agent 的三项必填字段非空、八个 `name` 唯一、`sandbox_mode` 符合权限矩阵。
4. 检查所有 Agent 均未设置 `model` 或 `model_reasoning_effort`。
5. 检查十九个框架文档都含精确 Unknown 状态行，且不存在 `TODO`、`TBD` 或虚构完成状态。
6. 根据 Codex 官方项目级 Agent 文件模式检查目录和字段结构。
7. 尝试通过本机 Codex 入口进行实时发现验证；若 Windows Store 执行限制仍阻止命令运行，则明确区分“静态配置有效”与“新会话实时加载尚待确认”，不得伪报成功。
8. 比较任务开始前后的 Git 文件集合，只允许设计规格、`.codex/`、`AGENTS.md` 和指定文档框架发生新增或修改。
9. 输出完整文件清单、Agent 职责与权限、限定范围的 Git diff，并明确确认未修改引擎生产代码。

## 8. 验收标准

- 交付一个项目配置、八个 Agent 配置、一个根规则文件和十九个入职框架文件，共二十九个目标文件；本设计规格作为过程文档单独存在。
- 所有 TOML 可解析且符合当前 Codex 自定义 Agent 必填字段要求。
- 所有 Agent 继承父会话的模型和推理强度。
- 只读角色和实现角色的默认权限符合本设计。
- 没有修改任何现有引擎源代码、Shader、资源格式、场景、Visual Studio 工程或构建流程。
- 不启动正式代码库分析，等待 Engine Director 后续指令。
