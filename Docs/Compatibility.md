# Unreal Engine compatibility

GeneralBlueprint release packages target Win64 and are built separately for every listed engine version. Every listed package has completed Unreal Automation Tool packaging; the manifest records its package path and SHA-256.

| Unreal Engine | BlueprintLisp | AnimBP2FP | MatBP2FP | BlueprintAutoLayout | MCP toolsets |
| --- | --- | --- | --- | --- | --- |
| 4.26 | Packaged | Compiled | Compiled | Compiled | Not included |
| 4.27 | Packaged | Compiled | Compiled | Compiled | Not included |
| 5.0 | Packaged | Compiled | Compiled | Compiled | Not included |
| 5.1 | Packaged | Compiled | Compiled | Compiled | Not included |
| 5.2 | Packaged | Compiled | Compiled | Compiled | Not included |
| 5.3 | Packaged | Compiled | Compiled | Compiled | Not included |
| 5.4 | Packaged | Compiled | Compiled | Compiled | Not included |
| 5.5 | Packaged | Compiled | Compiled | Compiled | Not included |
| 5.6 | Packaged | Compiled | Compiled | Compiled | Not included |
| 5.7 | Packaged | Compiled | Compiled | Compiled | Not included |
| 5.8 | Packaged | Compiled | Compiled | Compiled | Included; requires `ToolsetRegistry` and `ModelContextProtocol` |

Packaging compiles Editor Development and Game Development/Shipping targets. This confirms build compatibility, not complete behavior parity across engine versions. In particular, Animation Blueprint import starts at UE 5.1 in the source plugin, while UE 4.26–5.0 provide a narrower export/DSL workflow. Each release should be assessed against its separate Editor automation and asset smoke reports. Selected test passes do not establish whole-library graph round-trip coverage or production acceptance, especially where optional Animation Blueprint fixtures were skipped.

Only UE 5.8 packages contain native MCP modules. The original published ZIP contains BlueprintLisp, AnimBP2FP, and MatBP2FP adapters. Current source staging additionally includes BlueprintAutoLayoutMCP and DrivenHighlightMCP; it exposes eight toolsets and requires `ToolsetRegistry` and `ModelContextProtocol`. See [current MCP setup and boundaries](UEMCP.zh-CN.md). Existing release ZIPs are not rebuilt by a source update. These are experimental engine features. All release binaries target Win64; other platforms have not been validated for this combined plugin.
