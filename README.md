# GeneralBlueprint

GeneralBlueprint combines four Unreal Editor tools in one plugin while keeping each feature in its own Unreal module:

- **BlueprintLisp** — export, validate, compare, and import Blueprint graphs as S-expression DSL.
- **AnimBP2FP** — work with Animation Blueprint graphs and AnimLang/RigLang documents.
- **MatBP2FP** — export and import Material and Material Function graphs as MatLang.
- **BlueprintAutoLayout** — arrange Blueprint, Animation Blueprint, and Material graph nodes, with integrated DSL import hooks.

UE 5.8 source staging exposes eight native Unreal MCP toolsets (36 tools) through five adapter modules, covering Blueprint, Animation/Control Rig, Material, graph layout, and property highlights. See the [UE 5.8 MCP setup and tool catalog](Docs/UEMCP.zh-CN.md). Older engine packages omit these modules; previously published ZIPs retain their original contents.

## Install

1. Download the Win64 package matching the project's exact Unreal Engine version.
2. Extract `GeneralBlueprint` into the project's `Plugins` directory.
3. Enable **GeneralBlueprint** in the Plugins window and restart the Editor.
4. Regenerate project files and build the Editor target when using a source package.

Remove the old standalone plugins before enabling GeneralBlueprint; their module names overlap. See the [English user guide](Docs/UserGuide.md), [中文指南](Docs/UserGuide.zh-CN.md), and [compatibility matrix](Docs/Compatibility.md) for workflows, dependencies, and version limits.

The release script creates one package for each supported engine version from UE 4.26 through UE 5.8. Packages are engine-version specific and are not interchangeable.

## Notes

- Animation Blueprint import and Control Rig authoring capabilities depend on the target engine version; see [the compatibility matrix](Docs/Compatibility.md).
- UE 5.8 staging adds `BlueprintLispMCP`, `AnimBP2FPMCP`, `MatBP2FPMCP`, `BlueprintAutoLayoutMCP`, and `DrivenHighlightMCP`. Its `ToolsetRegistry` and `ModelContextProtocol` dependencies are experimental engine features. Use `Scripts/Stage-Ue58Plugin.ps1` for source installation with MCP; the base descriptor keeps older-engine support.
- BlueprintLisp and the DSL importers can reject or report lossy coverage for unsupported graph constructs. Keep source assets under version control and validate imports on copies first.

## Build release packages

From a Developer PowerShell with the Unreal Engine build toolchains installed:

```powershell
.\Scripts\Build-ReleasePackages.ps1 -EngineRoots $engineRoots
```

`$engineRoots` is a hashtable keyed by engine version (for example `4.26`, `4.27`, `5.0`, ... `5.8`) whose values are Unreal Engine installation roots. The script checks each root's `Build.version`, builds Win64 packages with that engine's `RunUAT`, and writes ZIP files plus SHA-256 hashes under `Releases`.
