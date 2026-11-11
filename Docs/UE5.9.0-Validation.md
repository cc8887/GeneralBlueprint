# GeneralBlueprint 1.2.0 beta 1: UE 5.9.0 Win64 validation

Validated on 2026-11-11 using the installed Unreal Engine 5.9.0, CL 60351404, branch `++UE5+Release-5.9`.
Source is identified by release tag `v1.2.0-beta.1`.

## Build and package

- Unreal Automation Tool `BuildPlugin -TargetPlatforms=Win64 -Rocket` completed successfully.
- Editor Development, Game Development, and Game Shipping compilation all passed.
- Toolchain: MSVC 14.44.35222 and Windows SDK 10.0.26100.0.
- The package contains 14 modules, including all five native MCP adapters (8 toolsets, 36 methods).
- Package structure, engine version, publisher fields, source rules, Editor DLLs, and SHA-256 passed the release checker.
- All 265 packaged source files and 7 maintained guide files match the release checkout byte for byte.

UE 5.9 compatibility uses current RigVM function-identifier APIs and Control Rig hierarchy storage for parent labels, combined control settings, and offset transforms. The synthetic export regression verifies the Float control type, parent labels, and current/initial/minimum/maximum control values; old cached fields returned default values on UE 5.9.

## Extracted-archive automation

The final ZIP was extracted into a new host project and loaded by the UE 5.9 Editor with NullRHI. No plugin rebuild was requested in that project.

- 117 test records, zero failures: 115 executed checks passed and 2 fixture-dependent checks reported a skip.
- Unreal report totals: 113 succeeded and 4 succeeded with warnings. The skip records are included in those totals.
- Selected suites: BlueprintLisp, RigLang exporter, and all five MCP adapter suites. Substring matching also selected related BlueprintLisp integration checks.
- Editor exited with code 0; terminated after report completion: False.

Unavailable fixture checks:

- `AnimBP2FP.MCP.RealRigExport`
- `AnimBP2FP.RigLang.Exporter.RealAsset`

## Native MCP HTTP and persistence

- Independent stdlib HTTP client: 153 save-workflow checks passed.
- A newly launched Editor process: 122 read/reload checks passed.
- Both processes discovered all 8 toolsets and 36 reflected methods.
- The material import, layout, explicit update/save, fresh-process export, and read-only layout inspection passed.
- Saved and reloaded canonical material DSL SHA-256 matched: `35e3cf791ea86f648200f59a7de2ccd3145ef30d2a2536091699f7964411e806`.
- Only the two child Editors created for this validation were terminated after their synchronous operations completed.

## Scope

These results establish this Win64 package's build compatibility and the selected fixture/HTTP workflows. NullRHI does not validate Slate appearance or rendered pixels. The missing GASP animation and foot Control Rig assets were not exercised; full Control Rig reconstruction is outside this validation. Other engine versions and platforms were not rebuilt for this release.

The release includes the plugin ZIP, `SHA256SUMS.txt`, this report, and a compact validation evidence ZIP containing build output, the Automation JSON report, native MCP transcripts, and source/package comparison results. The package checksum is published separately in `SHA256SUMS.txt`.
