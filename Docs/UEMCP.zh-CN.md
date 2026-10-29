# UE 5.8 原生 Unreal MCP 接口

GeneralBlueprint 使用 UE 自带的 `ToolsetRegistry` 和 `ModelContextProtocol`，通过 `UToolsetDefinition`、静态 `UFUNCTION(meta=(AICallable))` 暴露工具。无需另起 Python MCP 服务，也无需开启全部引擎 toolset。实现方式遵循 [Epic Unreal MCP 文档](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor)。

## 从当前源码安装

基础描述符继续兼容旧引擎；UE 5.8 专用反射代码保存在 `MCP/Source`，不会被旧版本的 UHT 扫描。将当前源码生成为一个包含 MCP 的插件，输出目录必须不存在：

```powershell
.\Scripts\Stage-Ue58Plugin.ps1 `
  -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' `
  -OutputDirectory '<Project>\Plugins\GeneralBlueprint'
```

输出插件包含五个 MCP 模块和两个引擎插件依赖。随后使用 UE 5.8 工具链构建项目 Editor target，打开项目并启用 GeneralBlueprint。该脚本核对 `Build.version`，只接受 UE 5.8.x。

发布包文件名标明实际构建版本；描述符 `EngineVersion` 保留 UE 打包器生成的兼容基线（`5.8.0`），不能据此判断实际构建补丁版本。

已有源码安装也可将 `MCP` 目录单独复制为 `<Project>/Plugins/GeneralBlueprintMCP`，保留旁边的基础 GeneralBlueprint 插件，然后显式启用 **GeneralBlueprint MCP (UE 5.8)**。两种安装方式任选一种，避免同名模块重复。发布脚本复用相同模块清单，在 UE 5.8 包中合并模块；其他版本仍排除 MCP。历史下载包不会随源码更新自动改变。

## 启动与发现

1. 确认 **Toolset Registry** 和 **Unreal MCP / ModelContextProtocol** 已启用，重启 Editor。
2. 在控制台执行 `ModelContextProtocol.StartServer 8000`。默认 HTTP 地址为 `http://127.0.0.1:8000/mcp`，也可在 Editor Preferences 中配置自动启动。
3. 客户端完成 MCP `initialize`、`notifications/initialized` 握手。
4. `tools/list` 默认返回 `list_toolsets`、`describe_toolset`、`call_tool`。先发现 toolset，再按需取 schema 和调用。

修改 C++ 后需要重新构建并让 Editor 加载新二进制；新增或修改反射声明时应重启 Editor。`ModelContextProtocol.RefreshTools` 只刷新工具注册信息，不会编译或加载修改后的 C++。请求由引擎串行派发到游戏线程。

## Toolset 清单

当前源码提供 **8 个 toolset、36 个工具**。表中名称带有实际模块命名空间，可直接传入 `toolset_name`。

| Toolset | 数量 | 工具 |
| --- | ---: | --- |
| `BlueprintLispMCP.BlueprintLispToolset` | 7 | `ExportBlueprintGraph`、`ListBlueprintGraphs`、`ListBlueprintMemberVariables`、`InspectBlueprintMemberVariable`、`ValidateBlueprintLisp`、`ReplaceBlueprintGraph`、`MergeBlueprintGraph` |
| `AnimBP2FPMCP.AnimBP2FPToolset` | 11 | `ExportAnimBlueprint`、`ExportAnimBlueprintBundle`、`ExportControlRig`、`ExportEventGraph`、`GetAnimBlueprintSyncState`、`ValidateAnimBundle`、`ValidateAnimBlueprintRoundTrip`、`ApplyAnimBundle`、`UpdateAnimBlueprint`、`ReplaceEventGraph`、`MergeEventGraph` |
| `MatBP2FPMCP.MatBP2FPInspectToolset` | 7 | `ExportMaterialToText`、`ExportMaterialFunctionToText`、`ValidateMatLang`、`ValidateMaterialRoundTrip`、`GetMappingTable`、`FindMappingByMaterial`、`MaterialPathToDSLPath` |
| `MatBP2FPMCP.MatBP2FPEditToolset` | 2 | `ImportMaterialFromText`、`UpdateMaterialFromText` |
| `BlueprintAutoLayoutMCP.BlueprintAutoLayoutInspectToolset` | 2 | `ListLayoutGraphs`、`InspectGraphLayout` |
| `BlueprintAutoLayoutMCP.BlueprintAutoLayoutEditToolset` | 2 | `LayoutGraph`、`LayoutNodes` |
| `DrivenHighlightMCP.DrivenHighlightInspectToolset` | 2 | `ListActiveHighlights`、`GetHighlightStatus` |
| `DrivenHighlightMCP.DrivenHighlightEditToolset` | 3 | `ShowAssetPropertyHighlight`、`HideHighlight`、`ClearHighlights` |

蓝图和动画保留已有 toolset ID；各函数通过 `AIAccessMode` 标明读写。材质、布局和高亮按读写拆分 toolset。`AIAccessMode` 是描述元数据，当前引擎不将它作为权限检查；如需限制可调用范围，应使用引擎 Toolset Registry 的名称过滤设置。

## 调用示例

所有参数名、必填字段和返回类型以 `describe_toolset` 的实时 schema 为准。原生参数使用反射名称，例如 `AssetPath`、`GraphName`、`bSavePackage`。

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/call",
  "params": {
    "name": "describe_toolset",
    "arguments": {"toolset_name": "BlueprintAutoLayoutMCP.BlueprintAutoLayoutEditToolset"}
  }
}
```

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "call_tool",
    "arguments": {
      "toolset_name": "BlueprintAutoLayoutMCP.BlueprintAutoLayoutEditToolset",
      "tool_name": "LayoutGraph",
      "arguments": {
        "AssetPath": "/Game/Blueprints/BP_Example.BP_Example",
        "GraphName": "EventGraph",
        "bSavePackage": false
      }
    }
  }
}
```

先调用 `ListLayoutGraphs` 取得图名，Blueprint/AnimBlueprint 的嵌套图使用返回的限定名；材质主图名为 `MaterialGraph`。`InspectGraphLayout` 返回节点 ID 和坐标，可将 ID 原样传给 `LayoutNodes`；该列表独立于编辑器当前选区。空列表或未知 ID 会失败，不会自动改成整图布局。Blueprint 布局仅返回 K2 schema 及其派生图、原生动画状态机图，保留 AnimGraph、动画状态/过渡图，排除需要模型同步的 RigVM 图。没有受支持图时会明确失败。材质只接受 Material 主图，不接受 Material Instance 或 Material Function。

## 写入与能力边界

- 资产参数限制为项目 `/Game` 包路径或对象路径；拒绝文件系统路径、路径穿越和子对象。DSL 通过文本传入，Bundle 的 `SourceFile` 是逻辑文件名。
- 资产写入必须明确传入 `bSavePackage`，Bundle 则明确传入 `bCommitPersistent`；`false` 表示当前内存工作流，不能据此认定已保存。调用方应同时检查 `bSuccess`、错误/诊断和保存结果。PIE/SIE 中拒绝修改资产。
- 蓝图 Merge 为追加操作；底层增量 Update 尚未实现，没有暴露成工具。解析失败的验证不等于完整编译、运行或所有导入失败均可回滚。
- 动画 Bundle 验证报告模块、引用及导入计划；文本 round trip 是导出/解析/序列化检查。RigLang 导出不代表 UE 5.8 可完整重建 Control Rig。
- 材质函数支持导出；当前导入后端只支持 Material。MatLang lint 不编译着色器，也不证明渲染结果。
- 布局保留使用 `[BAL:pin]` 注释标记固定的节点，固定未选节点，并禁用新增 reroute。只读图会拒绝布局。材质只同步节点/根节点坐标和注释框尺寸；只读检查在临时副本上构造图，避免把打开图的副作用写入原资产。结果是图坐标证据，节点宽高是保存的图字段，不是 Slate 实测边界。
- 高亮只改变 Editor UI 请求状态。属性匹配使用 `DetailRowItem.<显示名称>`，不限定某一个 Details 面板。`bActive` 表示已登记，`bWidgetBound` 表示当前绑定了控件，两者均不等于截图验收。节点高亮后端尚未实现控件定位，因此没有暴露独立 ShowNode 工具。`ClearHighlights` 会清除所有来源的高亮。

## 验证

`BlueprintLisp.MCP`、`AnimBP2FP.MCP`、`MatBP2FP.MCP`、`BlueprintAutoLayout.MCP` 和 `DrivenHighlight.MCP` 自动化覆盖注册、schema、输入约束及代表性内存资产操作。可使用独立 HTTP 客户端验证真实 MCP 协议：

```powershell
python .\Scripts\Test-Ue58Mcp.py --url http://127.0.0.1:8000/mcp --output-jsonl '<Evidence>\mcp.jsonl'
```

脚本逐条保存实际请求和响应，检查目录、各 toolset schema 和调用结果。测试脚本成功与构建成功分别记录；它们不替代交互 UI、渲染像素或完整资产库验收。
