#!/usr/bin/env python3
"""Independent stdlib HTTP client for UE 5.8 native MCP acceptance.

Run with a staged GeneralBlueprint editor host and ModelContextProtocol.StartServer.
Evidence contains the actual JSON-RPC messages; no configured external MCP servers
or Python packages are used. --exercise-assets creates only unsaved /Game assets
in the running editor. --exercise-persistence explicitly saves its unique fixture.
Use --verify-saved-asset in a fresh editor process to verify that fixture by reads.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import sys
import urllib.error
import urllib.request
import uuid


EXPECTED_CLASSES = {
    "BlueprintLispToolset", "AnimBP2FPToolset",
    "MatBP2FPInspectToolset", "MatBP2FPEditToolset",
    "BlueprintAutoLayoutInspectToolset", "BlueprintAutoLayoutEditToolset",
    "DrivenHighlightInspectToolset", "DrivenHighlightEditToolset",
}


class AcceptanceError(RuntimeError):
    pass


class Client:
    def __init__(self, url, evidence, timeout):
        self.url, self.evidence, self.timeout = url, evidence, timeout
        self.sequence = 0
        self.session_id = None
        self.protocol = "2025-11-25"
        self.checks = 0

    def record(self, event, **data):
        self.evidence.write(json.dumps({
            "time_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "event": event, **data,
        }, ensure_ascii=False) + "\n")
        self.evidence.flush()

    def check(self, condition, message):
        self.record("assertion", passed=bool(condition), message=message)
        if not condition:
            raise AcceptanceError(message)
        self.checks += 1

    def rpc(self, method, params=None, notification=False):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        if not notification:
            self.sequence += 1
            message["id"] = self.sequence
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
            "MCP-Protocol-Version": self.protocol,
        }
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        self.record("request", message=message)
        request = urllib.request.Request(
            self.url, data=json.dumps(message).encode("utf-8"), headers=headers,
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                self.session_id = response.headers.get("Mcp-Session-Id", self.session_id)
                status = response.status
                if notification:
                    body = response.read().decode("utf-8")
                    self.record("notification_ack", status=status, body=body)
                    self.check(status in (200, 202, 204), f"{method} acknowledged")
                    return None
                if "text/event-stream" in response.headers.get("Content-Type", ""):
                    data_lines = []
                    while True:
                        line = response.readline().decode("utf-8")
                        if not line:
                            raise AcceptanceError("SSE ended without a matching JSON-RPC response")
                        if line.startswith("data:"):
                            data_lines.append(line[5:].lstrip().rstrip("\r\n"))
                        elif not line.strip() and data_lines:
                            event = json.loads("\n".join(data_lines))
                            data_lines = []
                            self.record("response", status=status, message=event)
                            if event.get("id") == message["id"]:
                                result = event
                                break
                else:
                    result = json.loads(response.read().decode("utf-8"))
                    self.record("response", status=status, message=result)
        except urllib.error.HTTPError as error:
            body = error.read().decode("utf-8", errors="replace")
            self.record("http_error", status=error.code, body=body)
            raise AcceptanceError(f"HTTP {error.code} from {method}: {body}") from error
        self.check(result.get("id") == message["id"], f"{method} response ID matches")
        self.check("error" not in result, f"{method} has no JSON-RPC error")
        return result["result"]

    def tool(self, name, arguments=None, expect_error=False):
        result = self.rpc("tools/call", {"name": name, "arguments": arguments or {}})
        self.check(bool(result.get("isError", False)) == expect_error,
                   f"{name} MCP error flag matches expectation")
        return result

    def call(self, toolsets, class_name, method, arguments=None):
        result = self.tool("call_tool", {
            "toolset_name": toolsets[class_name], "tool_name": method,
            "arguments": arguments or {},
        })
        value = decode_result(result)
        self.check(isinstance(value, dict) and "returnValue" in value,
                   f"{class_name}.{method} returns the reflected result wrapper")
        return value["returnValue"]


def decode_result(result):
    if "structuredContent" in result:
        return result["structuredContent"]
    text = "\n".join(block["text"] for block in result.get("content", [])
                     if block.get("type") == "text")
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return text


def field(value, name, default=None):
    # UE's schema and struct output standardize the first letter to lowercase
    # (for example bSuccess and dSLText). Its reflected input maps accept the
    # authored names used by these fixtures as well. Bool-prefix fallback keeps
    # this reader compatible with a converter that omits the prefix.
    candidates = {name.casefold()}
    if name.startswith("b"):
        candidates.add(name[1:].casefold())
    for key, result in value.items():
        if key.casefold() in candidates:
            return result
    return default


def reflected_contract(plugin_root):
    expected = {}
    for header in (plugin_root / "MCP" / "Source").rglob("*.h"):
        text = header.read_text(encoding="utf-8-sig")
        for match in re.finditer(
                r"class\s+\w+_API\s+U(\w+)\s*:\s*public\s+UToolsetDefinition\s*\{(.*?)\n\};",
                text, re.DOTALL):
            name, body = match.groups()
            methods = set(re.findall(r"static\s+\w+\s+(\w+)\s*\(", body))
            expected[name] = methods
    if set(expected) != EXPECTED_CLASSES:
        raise AcceptanceError(f"Unexpected reflected toolset classes: {sorted(expected)}")
    if sum(map(len, expected.values())) != 36:
        raise AcceptanceError("Expected 36 reflected methods; review the acceptance contract after schema changes")
    return expected


def run(client, plugin_root, exercise_assets, exercise_persistence=False, verify_saved_asset=None):
    exercise_assets = exercise_assets or exercise_persistence
    saved_asset_path = None
    material_dsl_sha256 = None
    expected = reflected_contract(plugin_root)
    initialized = client.rpc("initialize", {
        "protocolVersion": client.protocol,
        "capabilities": {},
        "clientInfo": {"name": "GeneralBlueprintIndependentHttpAcceptance", "version": "1.0"},
    })
    client.protocol = initialized["protocolVersion"]
    client.check(bool(client.session_id), "initialize assigns an MCP session")
    client.rpc("notifications/initialized", notification=True)
    listed = client.rpc("tools/list", {})
    names = {item["name"] for item in listed.get("tools", [])}
    client.check({"list_toolsets", "describe_toolset", "call_tool"} <= names,
                 "UE native tool-search meta-tools are advertised")
    catalog = decode_result(client.tool("list_toolsets"))
    client.check(isinstance(catalog, str), "list_toolsets returns the native textual catalog")
    catalog_names = re.findall(r"^\s*-\s+([^\s:]+)", catalog, re.MULTILINE)
    toolsets = {}
    for class_name, methods in sorted(expected.items()):
        matches = [name for name in catalog_names if name.rsplit(".", 1)[-1] == class_name]
        client.check(len(matches) == 1, f"{class_name} has one registered native toolset")
        toolsets[class_name] = matches[0]
        schema = decode_result(client.tool("describe_toolset", {"toolset_name": matches[0]}))
        client.check(isinstance(schema, dict), f"{class_name} describes valid JSON")
        tools = schema.get("tools", [])
        observed = {tool["name"].rsplit(".", 1)[-1] for tool in tools}
        client.check(observed == methods and len(tools) == len(methods),
                     f"{class_name} advertises exactly {len(methods)} reflected methods")
        for tool in tools:
            properties = tool.get("inputSchema", {}).get("properties", {})
            required = tool.get("inputSchema", {}).get("required", [])
            for save_choice in ("bSavePackage", "bCommitPersistent"):
                if save_choice in properties:
                    client.check(save_choice in required,
                                 f"{tool['name']} requires explicit {save_choice}")

    valid_bp = client.call(toolsets, "BlueprintLispToolset", "ValidateBlueprintLisp",
                           {"DSLText": "(event BeginPlay)"})
    client.check(field(valid_bp, "bSuccess") is True, "BlueprintLisp validates a real DSL fixture")
    invalid_anim = client.call(toolsets, "AnimBP2FPToolset", "ValidateAnimBundle", {
        "Sources": [{"SourceFile": "invalid.animlang", "Source": "(anim-blueprint"}],
    })
    client.check(field(invalid_anim, "bSuccess") is False and bool(field(invalid_anim, "Diagnostics")),
                 "Anim bundle validation returns source diagnostics for malformed input")
    client.check(not field(invalid_anim, "bMutationStarted") and not field(invalid_anim, "bPersisted"),
                 "Anim validation reports no persistent mutation")
    valid_anim = client.call(toolsets, "AnimBP2FPToolset", "ValidateAnimBundle", {
        "Sources": [{
            "SourceFile": "acceptance.riglang",
            "Source": '(rig-module :asset "/Game/Rigs/AcceptanceRig" '
                      ':class "/Script/ControlRig.ControlRigBlueprint" :version 1 '
                      ':content-hash "acceptance-rig-hash")\n'
                      '(define-rig-entry "ForwardsSolve" :id "entry-forwards" :event "Forwards Solve")\n',
        }],
    })
    client.check(field(valid_anim, "bSuccess") is True and len(field(valid_anim, "ModulePlan", [])) == 1,
                 "Valid RigLang bundle produces one module in its import plan")
    client.check(not field(valid_anim, "bMutationStarted") and not field(valid_anim, "bPersisted"),
                 "Successful bundle validation also reports no persistent mutation")
    valid_mat = client.call(toolsets, "MatBP2FPInspectToolset", "ValidateMatLang", {
        "DSLText": '(material "M_Valid" (expressions (constant $value :value 0.5)) '
                   '(outputs :roughness (connect $value)))',
    })
    client.check(field(valid_mat, "bSuccess") is True and bool(field(valid_mat, "CanonicalText")),
                 "MatLang validation returns canonical text")
    highlights = client.call(toolsets, "DrivenHighlightInspectToolset", "ListActiveHighlights")
    client.check(field(highlights, "bSuccess") is True and isinstance(field(highlights, "Highlights"), list),
                 "Highlight inspection dispatches and returns a state list")

    invalid_writes = [
        ("BlueprintLispToolset", "ReplaceBlueprintGraph", {
            "AssetPath": "/Game/../Forbidden", "GraphName": "EventGraph",
            "DSLText": "(event BeginPlay)", "bCompile": False, "bSavePackage": False}),
        ("AnimBP2FPToolset", "UpdateAnimBlueprint", {
            "AssetPath": "/Game/../Forbidden", "DSLText": "invalid", "bSavePackage": False}),
        ("MatBP2FPEditToolset", "UpdateMaterialFromText", {
            "MaterialPath": "/Game/../Forbidden", "DSLText": "invalid", "bSavePackage": False}),
        ("BlueprintAutoLayoutEditToolset", "LayoutGraph", {
            "AssetPath": "/Game/../Forbidden", "GraphName": "EventGraph", "bSavePackage": False}),
    ]
    # The reload mode issues only read tool calls, including during base checks.
    for class_name, method, arguments in ([] if verify_saved_asset else invalid_writes):
        result = client.call(toolsets, class_name, method, arguments)
        client.check(field(result, "bSuccess") is False,
                     f"{class_name}.{method} rejects traversal through HTTP dispatch")
        client.check(not field(result, "bSavedPackage", False) and not field(result, "bMutationStarted", False),
                     f"{class_name}.{method} reports no saved package or started mutation")

    if exercise_assets:
        name = "M_HttpAcceptance_" + uuid.uuid4().hex
        folder = "/Game/GeneralBlueprintMCPHttpValidation"
        source = '(material "' + name + '" (expressions (constant $value :value 0.5)) (outputs :roughness (connect $value)))'
        imported = client.call(toolsets, "MatBP2FPEditToolset", "ImportMaterialFromText", {
            "DSLText": source, "DestinationFolder": folder, "bSavePackage": False,
        })
        client.check(field(imported, "bSuccess") is True and not field(imported, "bSavedPackage"),
                     "HTTP material import creates an unsaved asset")
        path = folder + "/" + name
        exported = client.call(toolsets, "MatBP2FPInspectToolset", "ExportMaterialToText", {"MaterialPath": path})
        client.check(field(exported, "bSuccess") is True and "constant" in field(exported, "DSLText", ""),
                     "HTTP exports the newly imported material")
        layout = client.call(toolsets, "BlueprintAutoLayoutEditToolset", "LayoutGraph", {
            "AssetPath": path, "GraphName": "MaterialGraph", "bSavePackage": False,
        })
        client.check(field(layout, "bSuccess") is True and not field(layout, "bSavedPackage"),
                     "HTTP layout operates on the unsaved material")
        if exercise_persistence:
            saved = client.call(toolsets, "MatBP2FPEditToolset", "UpdateMaterialFromText", {
                "MaterialPath": path, "DSLText": source.replace(":value 0.5", ":value 0.25"),
                "bSavePackage": True,
            })
            client.check(field(saved, "bSuccess") is True and field(saved, "bSavedPackage") is True,
                         "HTTP material update explicitly saves the changed fixture")
            saved_asset_path = path
            saved_export = client.call(toolsets, "MatBP2FPInspectToolset", "ExportMaterialToText", {"MaterialPath": path})
            saved_text = field(saved_export, "DSLText", "")
            client.check(field(saved_export, "bSuccess") is True and bool(re.search(r":value\s+0\.25(?:\s|\))", saved_text)),
                         "Saved material exports the changed constant value")
            material_dsl_sha256 = hashlib.sha256(saved_text.encode("utf-8")).hexdigest()
            client.record("saved_validation_asset", asset_path=path, material_dsl_sha256=material_dsl_sha256,
                          note="Unique fixture saved explicitly; verify in a newly launched editor")
        else:
            client.record("unsaved_validation_asset", asset_path=path,
                          note="Created in memory; close the validation editor without saving")

    if verify_saved_asset:
        client.check(verify_saved_asset.startswith("/Game/") and ".." not in verify_saved_asset,
                     "Reload verification uses a project asset path")
        reloaded = client.call(toolsets, "MatBP2FPInspectToolset", "ExportMaterialToText", {
            "MaterialPath": verify_saved_asset,
        })
        reloaded_text = field(reloaded, "DSLText", "")
        client.check(field(reloaded, "bSuccess") is True and "constant" in reloaded_text,
                     "Editor loads and exports the saved material fixture")
        client.check(bool(re.search(r":value\s+0\.25(?:\s|\))", reloaded_text)),
                     "Reloaded material retains the explicitly saved constant value")
        graphs = client.call(toolsets, "BlueprintAutoLayoutInspectToolset", "ListLayoutGraphs", {
            "AssetPath": verify_saved_asset,
        })
        client.check(field(graphs, "bSuccess") is True and "MaterialGraph" in field(graphs, "Graphs", []),
                     "Reloaded material exposes its layout-capable graph")
        inspected = client.call(toolsets, "BlueprintAutoLayoutInspectToolset", "InspectGraphLayout", {
            "AssetPath": verify_saved_asset, "GraphName": "MaterialGraph",
        })
        client.check(field(inspected, "bSuccess") is True and bool(field(inspected, "Nodes")),
                     "Read-only layout inspection sees nodes in the reloaded material")
        client.check(not field(inspected, "bSavedPackage") and not field(inspected, "bMutationStarted"),
                     "Reload inspection reports no asset mutation or save")
        material_dsl_sha256 = hashlib.sha256(reloaded_text.encode("utf-8")).hexdigest()
        client.record("verified_saved_asset", asset_path=verify_saved_asset,
                      material_dsl_sha256=material_dsl_sha256,
                      note="Read verification; the caller must launch the fresh editor process")

    client.record("summary", status="PASS", toolsets=len(toolsets), methods=sum(map(len, expected.values())),
                  checks=client.checks, asset_exercise=exercise_assets,
                  persistence_exercise=exercise_persistence, saved_asset_path=saved_asset_path,
                  verified_asset_path=verify_saved_asset, material_dsl_sha256=material_dsl_sha256)
    print(f"PASS: {len(toolsets)} toolsets, 36 methods, {client.checks} checks; independent native MCP HTTP client")
    if saved_asset_path:
        print(f"Saved validation asset: {saved_asset_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:8000/mcp")
    parser.add_argument("--output-jsonl", type=Path, required=True)
    parser.add_argument("--plugin-root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--exercise-assets", action="store_true")
    parser.add_argument("--exercise-persistence", action="store_true",
                        help="Implies --exercise-assets; explicitly update/save the unique validation material")
    parser.add_argument("--verify-saved-asset", metavar="PATH",
                        help="Read-only verification of the --exercise-persistence fixture in a fresh editor")
    args = parser.parse_args()
    if args.verify_saved_asset and (args.exercise_assets or args.exercise_persistence):
        parser.error("--verify-saved-asset uses read-only mode and cannot combine with asset exercise flags")
    args.output_jsonl.parent.mkdir(parents=True, exist_ok=True)
    with args.output_jsonl.open("w", encoding="utf-8") as evidence:
        client = Client(args.url, evidence, args.timeout)
        try:
            run(client, args.plugin_root, args.exercise_assets, args.exercise_persistence, args.verify_saved_asset)
        except (AcceptanceError, OSError, ValueError, KeyError, TypeError) as error:
            client.record("summary", status="FAIL", checks=client.checks, error=str(error))
            print(f"FAIL: {error}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
