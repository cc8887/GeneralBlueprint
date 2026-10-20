# BlueprintLisp

[简体中文](#zh-cn) · [English](#english) · [Releases](https://github.com/cc8887/Blueprint2DSL/releases/tag/ue4.27-5.8) · [Used by AnimBP2FP](https://github.com/cc8887/AnimationBP2FP)

BlueprintLisp is an Editor-only Unreal Engine plugin that represents Blueprint graphs as readable S-expression DSL. Export graphs for review and AI-assisted editing, validate changes, then import them back into Blueprint assets.

<a id="zh-cn"></a>
## 简体中文

BlueprintLisp 当前由插件描述符标记为 Beta 与 Experimental；重要资产请先在副本上验证导入、编译和重新导出。

### 功能

- 双向转换 `EventGraph`、`FunctionGraph` 和 `MacroGraph`，并提供稳定节点 ID、节点位置和可读文本输出。
- 支持常见事件、函数、变量、分支、Switch、数组、结构体、Cast、纯表达式及 Timeline。
- 通过 Python Bridge 提供 DSL 验证、图列表、成员变量查询和节点 Pin 签名存根；同时公开 C++ AST、Parser、Pretty Printer 与 Converter API。
- `MergeAppend` 可按稳定 ID 复用兼容节点并更新指定入口覆盖的子图，适合范围明确的文本修改。
- UE 5.8 提供独立的 MCP Toolset companion plugin，用于在编辑器内读取、验证和写入图。源码位于 **MCP/BlueprintLispMCP**；核心 Release ZIP 不包含 companion，需单独复制到项目并构建。

Timeline DSL 保留模板、轨道、控制操作、输出 Pin 与异步回调之间的关系；它不会把 Timeline 压成线性函数调用。

### 引擎版本与发布包

| Unreal Engine | 状态 |
| --- | --- |
| UE 4.27 | 提供 Win64 包；代码兼容目标 |
| UE 5.1–5.8 | 提供各版本对应的 Win64 包；代码兼容目标 |
| UE 5.5 | 本仓库明确验证的版本 |
| UE 5.0 | 不在支持范围内 |

当前发布批次的目标版本均已编译；自动化测试未随该批次运行。UE 5.5 以外的版本应在实际项目、节点和插件组合中验证。插件仅包含 Editor 模块；Release ZIP 仅提供 Win64 二进制。插件描述符列出 Mac 和 Linux 平台；当前 Release 没有这些平台的预编译包，使用时请在对应环境从源码构建并验证。

### 安装

1. 从 [Releases](https://github.com/cc8887/Blueprint2DSL/releases/tag/ue4.27-5.8) 下载与项目引擎版本匹配的 `BlueprintLisp` ZIP。
2. 解压到项目的 `Plugins/BlueprintLisp/`，确认 `BlueprintLisp.uplugin` 位于插件目录根部。
3. 关闭 Editor，重新生成项目文件并编译 Editor target。若使用 Python Bridge，再启用引擎的 Python Editor Script Plugin。

从源码安装：

~~~powershell
git clone https://github.com/cc8887/Blueprint2DSL.git "<Project>/Plugins/BlueprintLisp"
~~~

### 快速开始

在 Unreal Python Console 或 Editor Python 脚本中导出、检查并写回一个图：

~~~python
import unreal

bridge = unreal.BlueprintLispPythonBridge
blueprint = "/Game/Path/BP_Example.BP_Example"
graph = "EventGraph"

exported = bridge.export_graph_to_text(blueprint, graph, False, True)
if not exported.success:
    raise RuntimeError(exported.message)

dsl_text = exported.dsl_text
checked = bridge.validate_dsl(dsl_text)
if not checked.success:
    raise RuntimeError(checked.message)

result = bridge.import_graph_from_text(
    blueprint,
    graph,
    dsl_text,
    unreal.BlueprintLispPythonImportMode.MERGE_APPEND,
    True,
    True,
)
if not result.success:
    raise RuntimeError(result.message)
~~~

完整覆盖图时使用 `REPLACE_GRAPH`；局部变更时从启用稳定 ID 的导出结果开始，并在资产副本上验证编译和重新导出。

### 能力边界与 MCP

- 节点类型覆盖因 Unreal 节点和项目插件而异；导入前请在目标资产副本上验证结果。
- `MergeAppend` 只更新 DSL 所描述的入口可达子图。兼容 ID 会被复用，不兼容节点会重建，覆盖范围内未被新 DSL 使用的旧节点可能删除。
- `UpdateSemantic`、`UpdateGraphFromText` 和 `UpdateGraphFromFile` 当前未实现。
- UE 5.8 companion 位于 `MCP/BlueprintLispMCP`，Toolset 为 `BlueprintLispMCP.BlueprintLispToolset`。工具只处理 Unreal 资产路径和内存中的 DSL，不提供任意本地文件访问。

### 开发与文档

- [回归测试说明](Tests/Regression/README.md)
- [BlueprintLisp regression runner](Tests/Regression/run_blueprintlisp_regression_suite.py)
- AnimBP2FP 用户可从 [AnimationBP2FP Releases](https://github.com/cc8887/AnimationBP2FP/releases/tag/ue4.27-5.8) 获取对应引擎版本的配套插件。

<a id="english"></a>
## English

The plugin descriptor marks BlueprintLisp as Beta and Experimental. Validate imports, compilation, and re-exports on asset copies before applying changes to important assets.

### Features

- Convert `EventGraph`, `FunctionGraph`, and `MacroGraph` in both directions, with stable node IDs, optional positions, and readable text output.
- Support common events, functions, variables, branches, switches, arrays, structs, casts, pure expressions, and Timelines.
- Use the Python Bridge for DSL validation, graph listing, member-variable inspection, and node pin-signature stubs. C++ AST, Parser, Pretty Printer, and Converter APIs are also available.
- `MergeAppend` reuses compatible nodes by stable ID and updates the subgraph covered by the selected entry point, making it suitable for scoped text edits.
- UE 5.8 has a separate MCP Toolset companion plugin for reading, validating, and writing graphs in the Editor. Its source is in **MCP/BlueprintLispMCP**; the core Release ZIP does not include it, so copy it to the project and build it separately.

Timeline DSL preserves the relationships between its template, tracks, control operations, output pins, and asynchronous callbacks instead of flattening them into linear function calls.

### Unreal Engine versions and releases

| Unreal Engine | Status |
| --- | --- |
| UE 4.27 | Win64 package available; code compatibility target |
| UE 5.1–5.8 | Version-specific Win64 packages available; code compatibility targets |
| UE 5.5 | Version explicitly validated by this repository |
| UE 5.0 | Outside the support matrix |

All target versions in the current release batch compiled; automation tests were not run for that batch. Validate versions other than UE 5.5 against your project's actual node and plugin combination. The plugin contains only an Editor module; Release ZIPs provide Win64 binaries. The plugin descriptor lists Mac and Linux platforms. The current Releases do not include binaries for those platforms; build from source with the target engine toolchain and validate in your project.

### Installation

1. Download the `BlueprintLisp` ZIP for your engine version from [Releases](https://github.com/cc8887/Blueprint2DSL/releases/tag/ue4.27-5.8).
2. Extract it to `Plugins/BlueprintLisp/` and make sure `BlueprintLisp.uplugin` is at the plugin root.
3. Close the Editor, regenerate project files, and build the Editor target. Enable the engine's Python Editor Script Plugin when using the Python Bridge.

Install from source:

~~~powershell
git clone https://github.com/cc8887/Blueprint2DSL.git "<Project>/Plugins/BlueprintLisp"
~~~

### Quick start

Export, validate, and import a graph from the Unreal Python Console or an Editor Python script:

~~~python
import unreal

bridge = unreal.BlueprintLispPythonBridge
blueprint = "/Game/Path/BP_Example.BP_Example"
graph = "EventGraph"

exported = bridge.export_graph_to_text(blueprint, graph, False, True)
if not exported.success:
    raise RuntimeError(exported.message)

dsl_text = exported.dsl_text
checked = bridge.validate_dsl(dsl_text)
if not checked.success:
    raise RuntimeError(checked.message)

result = bridge.import_graph_from_text(
    blueprint,
    graph,
    dsl_text,
    unreal.BlueprintLispPythonImportMode.MERGE_APPEND,
    True,
    True,
)
if not result.success:
    raise RuntimeError(result.message)
~~~

Use `REPLACE_GRAPH` for a full graph replacement. For scoped edits, start from an export with stable IDs and verify compilation and re-export on an asset copy.

### Boundaries and MCP

- Node coverage depends on Unreal node types and project plugins. Validate imports on a copy of the target asset.
- `MergeAppend` updates only the entry-reachable subgraph described by the DSL. Compatible IDs are reused, incompatible nodes are rebuilt, and old nodes in that covered region that the new DSL does not consume may be removed.
- `UpdateSemantic`, `UpdateGraphFromText`, and `UpdateGraphFromFile` are not implemented.
- The UE 5.8 companion is in `MCP/BlueprintLispMCP` and registers `BlueprintLispMCP.BlueprintLispToolset`. Tools accept Unreal asset paths and in-memory DSL; they do not provide arbitrary local-file access.

### Development and documentation

- [Regression test guide](Tests/Regression/README.md)
- [BlueprintLisp regression runner](Tests/Regression/run_blueprintlisp_regression_suite.py)
- AnimBP2FP users can get the matching plugin version from [AnimationBP2FP Releases](https://github.com/cc8887/AnimationBP2FP/releases/tag/ue4.27-5.8)
