# GeneralBlueprint user guide

GeneralBlueprint combines BlueprintLisp, AnimBP2FP, MatBP2FP, and BlueprintAutoLayout in a single Unreal Engine plugin. The four tools remain separate Unreal modules. Install only **GeneralBlueprint**; do not install the old standalone plugins beside it, because their module names overlap.

## Installation and requirements

1. Choose the ZIP for your **exact** Unreal Engine minor version. Current binaries are for Windows (Win64); Mac and Linux are not verified release targets.
2. Close Unreal Editor. Extract the ZIP so the descriptor is at `<Project>/Plugins/GeneralBlueprint/GeneralBlueprint.uplugin`. The Fab Launcher may instead install it under `<Engine>/Engine/Plugins/GeneralBlueprint/`.
3. Open the project and enable **GeneralBlueprint** in **Edit > Plugins**, then restart when prompted. A source build may require the C++ toolchain for that engine version.
4. The plugin enables the engine's **Control Rig** plugin. The Python examples below also require the engine's **Python Editor Script Plugin**.

The UE 5.8 package additionally contains three MCP toolset modules. This package requires the engine's **Toolset Registry** and **Unreal MCP (ModelContextProtocol)** plugins, which are enabled by its descriptor. Unreal MCP is an experimental engine feature. The other engine packages contain no MCP modules. The MCP server is configured separately in Unreal Editor; installing GeneralBlueprint alone does not start a server.

If you previously installed BlueprintLisp, AnimBP2FP, MatBP2FP, or BlueprintAutoLayout separately, disable and remove those standalone plugins before enabling GeneralBlueprint. Keep a backup of the project and its assets before importing DSL.

## What each engine package contains

The build matrix in [Compatibility.md](Compatibility.md) records compilation and packaging, not a guarantee that every graph type has passed functional testing. All packages include the four main tools. Animation Blueprint import is available starting with UE 5.1; UE 4.26, 4.27, and 5.0 have a narrower export and DSL workflow. UE 5.8 alone includes the MCP toolsets. Do not exchange binaries or `Intermediate` files between engine versions.

## Blueprint graphs: BlueprintLisp

BlueprintLisp exports Blueprint graphs to a readable S-expression DSL. It supports graph inspection, validation, and import, including stable node IDs for scoped updates. Work on a copy of the Blueprint until its compile and re-export results are satisfactory.

Enable Python Editor Script Plugin, then run this example from the Unreal Python console after replacing the asset path:

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

To write an edited DSL document back into a **copy** of the Blueprint, call `import_graph_from_text` with `unreal.BlueprintLispPythonImportMode.MERGE_APPEND`. Compile and re-export the result before replacing the original asset. `REPLACE_GRAPH` replaces the whole target graph. Node coverage depends on the graph and other plugins installed in the project; an unsupported construct can cause import to fail or report a lossy result.

## Animation Blueprints: AnimBP2FP

AnimBP2FP represents AnimGraphs with AnimLang and can export referenced Control Rig data as RigLang. Export, lint, and inspect the files before importing. Use `UE4Editor-Cmd.exe` instead of `UnrealEditor-Cmd.exe` on UE 4.x.

```powershell
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimBP2FPExport -AssetPath=/Game/Characters/ABP_Hero -IncludeRigModules -stdout -nullrhi
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimLispLint -Workspace="<Workspace>" -stdout -nullrhi
```

On UE 5.1 or later, a strict bundle can be checked and imported with the following commands. Use a destination folder containing test assets first:

```powershell
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimBP2FPRoundTrip -Bundle="<Workspace>" -stdout -nullrhi
UnrealEditor-Cmd.exe "<Project>.uproject" -run=AnimBP2FPImport -Bundle="<Workspace>" -OutDir=/Game/Imported -stdout -nullrhi
```

Strict import rejects `lossy` or `unsupported` nodes. Control Rig runtime asset support in UE 5.8 covers export and related inspection; full reconstruction from RigLang is not available. Example `.animlang` files are in `Examples/Animation/`.

## Materials: MatBP2FP

In Unreal Editor, use **Tools > MatBP2FP > Export All Materials to DSL** to export project Materials and Material Functions. By default, output is written under `<Project>/Saved/BP2DSL/MatBP/`. The same operation is available as a commandlet:

```powershell
UnrealEditor-Cmd.exe "<Project>.uproject" -run=MatBP2FPExport -all -NullRHI -Unattended -NoSplash -NoP4 -UTF8Output
UnrealEditor-Cmd.exe "<Project>.uproject" -run=MatBP2FPLint -path="<FileOrDirectory>" -NullRHI -Unattended -NoSplash -NoP4 -UTF8Output
```

Use the Python bridge for an edit that must be saved. Replace the example path and make a copy of the Material first:

```python
import unreal

path = "/Game/Materials/M_Example.M_Example"
exported = unreal.MatBP2FPPythonBridge.export_material_to_text(path)
if not exported.success:
    raise RuntimeError(exported.message)

# Edit exported.dsl_text here, keeping stable node IDs where possible.
result = unreal.MatBP2FPPythonBridge.update_material_from_text(
    path, exported.dsl_text, True
)
if not result.success or not result.saved_package:
    raise RuntimeError(result.message)
```

`MatBP2FPImport` commandlet currently creates or updates an in-memory asset but does not save its package. Use the Python or Blueprint bridge with package saving, or save the asset inside an Editor session. Example `.matlang` files are in `Examples/Material/`.

## Graph layout: BlueprintAutoLayout

Open a Blueprint, Material, or Animation Blueprint graph. Use **Auto Layout** in the graph toolbar or **Auto Layout > Auto Layout Graph** in its context menu; `Ctrl+Shift+L` is the default shortcut. To move only selected nodes, use **Layout Selection** or `Ctrl+Shift+Alt+L`. The operation supports the Editor's Undo command. If the selection cannot be read, Layout Selection may lay out the whole graph.

DSL import integrations can request layout after importing nodes. The manual layout command is also available without using any DSL workflow.

## Troubleshooting and limitations

- **Missing module or incompatible binary:** check that the ZIP, Unreal minor version, and Win64 platform match. Remove binaries from old standalone plugin installs, then restart the Editor.
- **Python class missing:** enable Python Editor Script Plugin and restart. Python is only required for the Python bridge workflows.
- **No layout command:** reopen the graph editor after enabling GeneralBlueprint. Click inside the graph before trying a keyboard shortcut.
- **UE 5.8 MCP module fails to load:** enable Toolset Registry and Unreal MCP in the engine, then restart. MCP is available only in the UE 5.8 package.
- **DSL import reports unsupported or lossy coverage:** retain the source asset, inspect the report, and use a supported graph or a narrower change. An export is not proof that every construct can be rebuilt.
- **Material commandlet import appears to succeed but nothing is saved:** use the Python/Blueprint bridge with saving, or save from an interactive Editor session.

See [Compatibility.md](Compatibility.md) for the build matrix. Support and download links are supplied on the Fab product page when the product is published.
