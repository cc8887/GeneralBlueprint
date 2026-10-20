# GeneralBlueprint 用户指南

GeneralBlueprint 将 BlueprintLisp、AnimBP2FP、MatBP2FP 和 BlueprintAutoLayout 合并到一个 Unreal 插件中，各功能仍由独立模块实现。安装 **GeneralBlueprint** 即可；旧版独立插件与它包含同名模块，不应同时启用。

## 安装与依赖

1. 下载与项目 **引擎小版本完全一致** 的 Win64 ZIP。当前发布包尚未验证 Mac 或 Linux。
2. 关闭编辑器，解压到 `<项目>/Plugins/`，确认描述文件位于 `<项目>/Plugins/GeneralBlueprint/GeneralBlueprint.uplugin`。通过 Fab Launcher 安装时也可能位于引擎的 `Engine/Plugins/GeneralBlueprint/`。
3. 在 **Edit > Plugins** 中启用 GeneralBlueprint，并按提示重启。使用源码编译时需安装该引擎版本对应的 C++ 工具链。
4. 插件会启用引擎的 Control Rig。以下 Python 示例还需启用 **Python Editor Script Plugin**。

UE 5.8 包另含三个 MCP 模块，并依赖引擎内的 **Toolset Registry** 和 **Unreal MCP / ModelContextProtocol**。它们是该包的实际依赖；Unreal MCP 属于实验性功能。其他版本的包不含 MCP 模块。安装 GeneralBlueprint 不会自动启动 MCP 服务器。

导入 DSL 前请备份项目和资产。卸载旧的 BlueprintLisp、AnimBP2FP、MatBP2FP、BlueprintAutoLayout 独立插件，避免模块重名。

## 版本边界

[兼容表](Compatibility.md)记录的是构建和打包结果，不代表所有图类型均已通过功能测试。四项主功能在各版本包中均有模块；Animation Blueprint **导入从 UE 5.1 开始**，UE 4.26、4.27、5.0 的动画工作流范围较窄。仅 UE 5.8 包包含 MCP。不同引擎版本之间不要混用二进制或 `Intermediate` 文件。

## BlueprintLisp：蓝图图转 DSL

启用 Python Editor Script Plugin 后，在 Unreal Python Console 中替换资产路径并执行：

```python
import unreal

bridge = unreal.BlueprintLispPythonBridge
asset = "/Game/Path/BP_Example.BP_Example"
graph = "EventGraph"
exported = bridge.export_graph_to_text(asset, graph, False, True)
if not exported.success:
    raise RuntimeError(exported.message)
checked = bridge.validate_dsl(exported.dsl_text)
if not checked.success:
    raise RuntimeError(checked.message)
print(exported.dsl_text)
```

要写回 DSL，可在**资产副本**上使用 `bridge.import_graph_from_text` 和 `unreal.BlueprintLispPythonImportMode.MERGE_APPEND`。完成后编译并重新导出核对。`REPLACE_GRAPH` 会替换整个目标图。节点覆盖程度取决于资产及项目插件；不支持的构造可能导致导入失败或产生有损报告。

## AnimBP2FP：动画蓝图与 AnimLang

AnimBP2FP 使用 AnimLang 表示 AnimGraph，并可将引用的 Control Rig 数据导出为 RigLang。UE4 请将下列可执行文件名改为 `UE4Editor-Cmd.exe`。

```powershell
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimBP2FPExport -AssetPath=/Game/Characters/ABP_Hero -IncludeRigModules -stdout -nullrhi
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimLispLint -Workspace="<Workspace>" -stdout -nullrhi
```

UE 5.1 及更新版本可用 `-run=AnimBP2FPRoundTrip -Bundle="<Workspace>"` 检查严格 bundle，再用 `-run=AnimBP2FPImport -Bundle="<Workspace>" -OutDir=/Game/Imported` 导入到测试目录。严格导入会拒绝 `lossy` 和 `unsupported` 节点。UE 5.8 Control Rig RuntimeAsset 目前覆盖导出和检查，尚不能完整从 RigLang 重建。`Examples/Animation/` 提供 AnimLang 示例。

## MatBP2FP：材质与 MatLang

在编辑器主菜单 **Tools > MatBP2FP > Export All Materials to DSL** 导出项目材质和 Material Function，默认输出到 `<项目>/Saved/BP2DSL/MatBP/`。也可使用命令行：

```powershell
UnrealEditor-Cmd.exe "<Project>.uproject" -run=MatBP2FPExport -all -NullRHI -Unattended -NoSplash -NoP4 -UTF8Output
UnrealEditor-Cmd.exe "<Project>.uproject" -run=MatBP2FPLint -path="<FileOrDirectory>" -NullRHI -Unattended -NoSplash -NoP4 -UTF8Output
```

需要持久化导入结果时，请在资产副本上用 `unreal.MatBP2FPPythonBridge.update_material_from_text(path, dsl_text, True)`，并检查返回值中的 `success` 和 `saved_package`。`MatBP2FPImport` Commandlet 当前只修改内存中的资产，不会保存 package。`Examples/Material/` 提供 MatLang 示例。

## BlueprintAutoLayout：自动排版

打开 Blueprint、Material 或 Animation Blueprint 图，点击工具栏 **Auto Layout**，或使用右键菜单 **Auto Layout > Auto Layout Graph**；默认快捷键为 `Ctrl+Shift+L`。只整理选中节点可使用 **Layout Selection** 或 `Ctrl+Shift+Alt+L`。支持编辑器撤销。如果编辑器无法读取选区，Layout Selection 可能整理整个图。

## 常见问题

- **模块无法加载：**核对 ZIP 对应的引擎小版本和 Win64 平台，移除同名旧插件后重启。
- **Python 类不存在：**启用 Python Editor Script Plugin 并重启；手动排版不要求 Python。
- **看不到排版命令：**重新打开资产编辑器，使用快捷键前先点击图空白区域。
- **UE 5.8 MCP 不可用：**确认 Toolset Registry 与 Unreal MCP 已启用并重启。
- **DSL 报告不支持或有损：**保留原资产，先检查报告，再缩小修改范围或换用受支持的图。
- **材质 Commandlet 导入后未保存：**使用带保存参数的 Python/Blueprint Bridge，或在编辑器会话中保存资产。

更完整的步骤和 Python 示例见 [English user guide](UserGuide.md)。Fab 商品页面发布后将提供下载和支持链接。
