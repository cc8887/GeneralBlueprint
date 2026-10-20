# AnimBP2FP

[简体中文](#zh-cn) · [English](#english) · [Releases](https://github.com/cc8887/AnimationBP2FP/releases/tag/ue4.27-5.8) · [BlueprintLisp dependency](https://github.com/cc8887/Blueprint2DSL/releases/tag/ue4.27-5.8) · [AnimBP2FP-N](https://github.com/cc8887/AnimationBP2FP-N)

<a id="zh-cn"></a>
## 简体中文

AnimBP2FP 是 Unreal Editor 插件，用于在 Animation Blueprint 与可读、可编辑的 S-expression DSL 之间转换。AnimGraph 使用 AnimLang 表达；EventGraph 与普通 K2 子图复用 BlueprintLisp；Control Rig 使用独立的 RigLang 模块。

### 功能

- 导出、解析、lint、比较和导入 Animation Blueprint；保留 Skeleton、变量、AnimGraph、状态机、Cached Pose、Animation Layer 及资产依赖。
- 按覆盖等级描述节点支持情况：`exact`、`reflected`、`lossy` 或 `unsupported`。严格导入会拒绝不支持的节点。
- 将 Control Rig 导出为独立 `.riglang` 模块，并通过 hash、类型和依赖顺序组成可检查的 Anim/Rig workspace bundle。
- 严格 bundle 导入先在临时资产中解析、编译和回导验证，通过后再保存持久资产。
- 提供 Editor 菜单、Commandlet 和 Python Bridge；UE 5.8 另有可选的 MCP Toolset companion plugin，源码位于 `MCP/AnimBP2FPMCP`，需单独安装和编译。

### 引擎版本与发布包

| Unreal Engine | 支持范围 |
| --- | --- |
| UE 4.27 | 提供 Win64 包；支持 Animation Blueprint 导出及 DSL parser、lint、diff；AnimBlueprint 导入从 UE 5.1 开始支持 |
| UE 5.1–5.8 | 提供各版本对应的 Win64 包；UE 5.0 不在支持范围内 |

发布包针对指定引擎版本构建，不能跨版本共用。当前发布批次已完成各目标版本的编译；该批次未运行自动化测试。UE 5.8 的 `UControlRigRuntimeAsset` 支持导出，目前不代表支持从 RigLang 完整重建该类资产。

### 依赖与安装

AnimBP2FP 依赖同一 UE 版本的 BlueprintLisp，以及引擎自带的 Control Rig 插件。Python Bridge 需要启用 Python Editor Script Plugin。UE 5.8 MCP Toolset 还需要引擎的 Toolset Registry 与 Model Context Protocol / Unreal MCP 插件。

1. 从 [Releases](https://github.com/cc8887/AnimationBP2FP/releases/tag/ue4.27-5.8) 下载与项目引擎版本对应的 AnimBP2FP ZIP。
2. 从 [Blueprint2DSL Releases](https://github.com/cc8887/Blueprint2DSL/releases/tag/ue4.27-5.8) 下载相同 UE 版本的 BlueprintLisp ZIP。
3. 将两个插件解压到项目的 `Plugins/` 目录，启用 Control Rig，然后重新生成项目文件并编译 Editor target。

也可以从源码安装：

~~~powershell
git clone https://github.com/cc8887/AnimationBP2FP.git "<Project>/Plugins/AnimBP2FP"
git clone https://github.com/cc8887/Blueprint2DSL.git "<Project>/Plugins/BlueprintLisp"
~~~

### 快速开始

导出一个 AnimBlueprint 及其引用的 Rig 模块：

~~~powershell
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimBP2FPExport -AssetPath=/Game/Characters/ABP_Hero -IncludeRigModules -stdout -nullrhi
~~~

随后可对 workspace 执行 lint、往返验证和严格导入：

~~~powershell
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimLispLint -Workspace="<Workspace>" -stdout -nullrhi
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimBP2FPRoundTrip -Bundle="<Workspace>" -stdout -nullrhi
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimBP2FPImport -Bundle="<Workspace>" -OutDir=/Game/Imported -stdout -nullrhi
~~~

导入 bundle 时，workspace 只应包含目标 AnimLang 文件及其引用的 RigLang 模块。旧版单文件 `.animlang` 导入需要显式使用 `-Legacy`，且不提供严格 bundle 的一致性保证。

### 能力边界与相关文档

- 增量 Patch 仅覆盖部分 root AnimGraph 非 Pose 输入默认值；其他结构变更会走完整重建流程。
- 对于 `lossy` 或 `unsupported` 节点，严格导入会拒绝继续，避免静默生成不可靠资产。
- UE 5.8 RuntimeAsset 路径已验证导出、解析/hash 和 Anim 输入关联；完整资产重建尚未支持。
- UE 5.8 MCP Toolset 标识为 `AnimBP2FPMCP.AnimBP2FPToolset`，仅接收 Unreal 资产路径与内存中的 DSL。
- [DSL lint 与格式化](Tools/USAGE.md) · [测试策略](Tests/TestStrategy.md) · [跨版本脚本](Tests/RunCrossVersionTests.ps1)

<a id="english"></a>
## English

AnimBP2FP is an Unreal Editor plugin for converting Animation Blueprints to and from readable, editable S-expression DSL. AnimGraphs use AnimLang, EventGraphs and regular K2 subgraphs use BlueprintLisp, and Control Rigs are represented as independent RigLang modules.

### Features

- Export, parse, lint, diff, and import Animation Blueprints while preserving skeletons, variables, AnimGraphs, state machines, cached poses, animation layers, and asset dependencies.
- Describe node coverage as `exact`, `reflected`, `lossy`, or `unsupported`. Strict import rejects unsupported nodes.
- Export Control Rigs as standalone `.riglang` modules and assemble verifiable Anim/Rig workspace bundles using hashes, types, and dependency order.
- Validate strict bundle imports in temporary assets, including compile and re-export checks, before saving persistent assets.
- Use the Editor menu, Commandlets, or Python Bridge. UE 5.8 also has an optional MCP Toolset companion plugin in `MCP/AnimBP2FPMCP`; install and build it separately.

### Unreal Engine versions and releases

| Unreal Engine | Support scope |
| --- | --- |
| UE 4.27 | Win64 package available; Animation Blueprint export plus DSL parsing, lint, and diff; AnimBlueprint import starts with UE 5.1 |
| UE 5.1–5.8 | Version-specific Win64 packages available; UE 5.0 is outside the support matrix |

Each package is built for its target engine and cannot be shared across engine versions. All targets in the current release batch compiled successfully; automation tests were not run for that batch. UE 5.8 `UControlRigRuntimeAsset` support covers export and does not imply full reconstruction from RigLang.

### Dependencies and installation

AnimBP2FP requires BlueprintLisp built for the same UE version and the engine's Control Rig plugin. Enable Python Editor Script Plugin for the Python Bridge. The UE 5.8 MCP Toolset also requires the engine's Toolset Registry and Model Context Protocol / Unreal MCP plugins.

1. Download the AnimBP2FP ZIP for your engine version from [Releases](https://github.com/cc8887/AnimationBP2FP/releases/tag/ue4.27-5.8).
2. Download the matching BlueprintLisp ZIP from [Blueprint2DSL Releases](https://github.com/cc8887/Blueprint2DSL/releases/tag/ue4.27-5.8).
3. Extract both plugins under the project `Plugins/` directory, enable Control Rig, regenerate project files, and build the Editor target.

You can also install from source:

~~~powershell
git clone https://github.com/cc8887/AnimationBP2FP.git "<Project>/Plugins/AnimBP2FP"
git clone https://github.com/cc8887/Blueprint2DSL.git "<Project>/Plugins/BlueprintLisp"
~~~

### Quick start

Export an Animation Blueprint and its referenced Rig modules:

~~~powershell
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimBP2FPExport -AssetPath=/Game/Characters/ABP_Hero -IncludeRigModules -stdout -nullrhi
~~~

Then lint, round-trip validate, or strictly import the workspace:

~~~powershell
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimLispLint -Workspace="<Workspace>" -stdout -nullrhi
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimBP2FPRoundTrip -Bundle="<Workspace>" -stdout -nullrhi
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimBP2FPImport -Bundle="<Workspace>" -OutDir=/Game/Imported -stdout -nullrhi
~~~

A bundle workspace should contain only the target AnimLang file and its referenced RigLang modules. Importing a legacy single-file `.animlang` requires `-Legacy` and does not provide the consistency guarantees of strict bundle import.

### Boundaries and documentation

- Incremental Patch covers only selected non-pose input defaults in the root AnimGraph; other structural changes use full reconstruction.
- Strict import stops on `lossy` or `unsupported` nodes rather than silently creating an unreliable asset.
- UE 5.8 RuntimeAsset validation covers export, parse/hash, and Anim input association; full asset reconstruction is not supported.
- The UE 5.8 MCP Toolset is `AnimBP2FPMCP.AnimBP2FPToolset` and accepts Unreal asset paths and in-memory DSL.
- [DSL lint and formatting](Tools/USAGE.md) · [Test strategy](Tests/TestStrategy.md) · [Cross-version script](Tests/RunCrossVersionTests.ps1)
