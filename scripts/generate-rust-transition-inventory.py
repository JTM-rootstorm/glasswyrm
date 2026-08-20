#!/usr/bin/env python3
"""Generate the deterministic WP0 Rust-transition ownership inventory."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs/maintenance/RUST_TRANSITION_WP0_INVENTORY.json"


def tracked(prefix: str) -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", prefix], cwd=ROOT, check=True,
        capture_output=True, text=True,
    )
    return [line for line in result.stdout.splitlines() if line]


def text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def sha256_path(path: str) -> str:
    return hashlib.sha256((ROOT / path).read_bytes()).hexdigest()


def tree_sha256(paths: list[str]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(path.encode())
        digest.update(b"\0")
        digest.update((ROOT / path).read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def parse_core_opcodes() -> list[tuple[str, int]]:
    source = text("crates/glasswyrm-x11/src/core.rs")
    match = re.search(r"pub enum CoreOpcode\s*\{(.*?)\n\}", source, re.S)
    if match is None:
        raise RuntimeError("CoreOpcode enum not found")
    return [(name, int(value)) for name, value in re.findall(
        r"^\s*(\w+)\s*=\s*(\d+),", match.group(1), re.M
    )]


def parse_legacy_dispatch() -> dict[str, str]:
    result: dict[str, str] = {}
    pending: list[str] = []
    for line in text("src/glasswyrmd/request_dispatcher.cpp").splitlines():
        case = re.search(r"case x11::CoreOpcode::(\w+):", line)
        if case:
            pending.append(case.group(1))
        returned = re.search(r"return\s+([A-Za-z0-9_]+)\(", line)
        if returned and pending:
            for name in pending:
                result[name] = returned.group(1)
            pending.clear()
    return result

def parse_rust_dispatch() -> set[str]:
    source = text("crates/glasswyrm-x11/src/dispatch.rs")
    return set(re.findall(r"CoreOpcode::(\w+) as u8", source))



def parse_rust_extensions() -> dict[str, dict[str, object]]:
    source = text("crates/glasswyrm-x11/src/extension.rs")
    pattern = re.compile(
        r'ExtensionDescriptor::new\(\s*b"([^"]+)",\s*ExtensionKind::(\w+),\s*'
        r'ExtensionProfiles::(\w+),\s*(\d+),\s*(\d+),\s*(\d+),\s*'
        r'(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*\)', re.S,
    )
    rows: dict[str, dict[str, object]] = {}
    for match in pattern.finditer(source):
        name, kind, capability, *numbers = match.groups()
        major, first_event, event_count, first_error, error_count, vmaj, vmin = map(int, numbers)
        if name in rows:
            raise RuntimeError(f"duplicate Rust extension descriptor: {name}")
        rows[name] = {
            "kind": kind,
            "capability": capability,
            "major_opcode": major,
            "first_event": first_event,
            "event_count": event_count,
            "first_error": first_error,
            "error_count": error_count,
            "maximum_version": [vmaj, vmin],
        }
    return rows

def parse_extensions() -> list[dict[str, object]]:
    source = text("src/glasswyrmd/extension_registry.hpp")
    rust_rows = parse_rust_extensions()
    capability_names = {
        "None": "NONE",
        "GameCompat": "GAME_COMPAT",
        "ScaleProtocol": "SCALE_PROTOCOL",
        "VrrProtocol": "VRR_PROTOCOL",
    }
    pattern = re.compile(
        r'\{"([^"]+)",\s*ExtensionKind::(\w+),\s*'
        r'ExtensionCapability::(\w+),\s*(\d+),\s*(\d+),\s*(\d+),\s*'
        r'(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}', re.S,
    )
    rows = []
    for match in pattern.finditer(source):
        name, kind, capability, *numbers = match.groups()
        major, first_event, event_count, first_error, error_count, vmaj, vmin = map(int, numbers)
        rust = rust_rows.get(name)
        expected_rust = {
            "kind": kind,
            "capability": capability_names[capability],
            "major_opcode": major,
            "first_event": first_event,
            "event_count": event_count,
            "first_error": first_error,
            "error_count": error_count,
            "maximum_version": [vmaj, vmin],
        }
        if rust != expected_rust:
            raise RuntimeError(
                f"Rust/legacy extension metadata drift for {name}: "
                f"rust={rust!r} legacy={expected_rust!r}"
            )
        rust_discovery = rust is not None
        legacy_name = {
            "BIG-REQUESTS": None,
            "MIT-SHM": "src/glasswyrmd/extensions/mit_shm.cpp",
            "XFIXES": "src/glasswyrmd/extensions/xfixes.cpp",
            "DAMAGE": "src/glasswyrmd/extensions/damage.cpp",
            "RENDER": "src/glasswyrmd/extensions/render.cpp",
            "Composite": "src/glasswyrmd/extensions/composite.cpp",
            "RANDR": "src/glasswyrmd/extensions/randr.cpp",
            "GW_SCALE": "src/glasswyrmd/extensions/gw_scale.cpp",
            "GW_VRR": "src/glasswyrmd/extensions/gw_vrr.cpp",
        }[name]
        rows.append({
            "name": name, "kind": kind, "capability": capability,
            "major_opcode": major, "first_event": first_event,
            "event_count": event_count, "first_error": first_error,
            "error_count": error_count, "maximum_version": [vmaj, vmin],
            "legacy_implementation": legacy_name or "src/glasswyrmd/extension_dispatcher.cpp",
            "rust_implementation": "crates/glasswyrm-x11/src/extension.rs" if rust_discovery else None,
            "rust_discovery_implementation": "crates/glasswyrm-x11/src/dispatch.rs" if rust_discovery else None,
            "rust_metadata_parity": rust_discovery,
            "rust_discovery": rust_discovery,
            "rust_enabled_by_default": False,
            "fixture_profiles": extension_fixture_profiles(name),
            "mixed_stack_profiles": ["legacy-server-oracle", "rust-server-process-oracle", "wp0c-rust-server-legacy-peers", "wp0c-rust-server-rust-peers"],
            "cutover_phase": "WP3/WP5D",
        })
    legacy_names = {row["name"] for row in rows}
    if set(rust_rows) != legacy_names:
        raise RuntimeError(
            f"Rust extension registry names drift from legacy: "
            f"rust={sorted(rust_rows)} legacy={sorted(legacy_names)}"
        )
    return rows




def request_rust_decoder_file(name: str) -> str | None:
    if name in {
        "CreateWindow", "DestroyWindow", "GetGeometry", "QueryTree",
        "InternAtom", "GetAtomName", "ChangeProperty", "DeleteProperty",
        "GetProperty", "ListProperties", "GetInputFocus", "QueryExtension",
        "ListExtensions",
    }:
        return "crates/glasswyrm-x11/src/decode.rs"
    if name in {
        "MapWindow", "MapSubwindows", "UnmapWindow", "UnmapSubwindows",
        "ConfigureWindow",
    }:
        return "crates/glasswyrm-x11/src/lifecycle_request.rs"
    if name == "NoOperation":
        return "crates/glasswyrm-x11/src/dispatch.rs"
    return None

def request_legacy_file(name: str) -> str:
    groups = [
        ({"CreateWindow", "DestroyWindow", "MapWindow", "MapSubwindows", "UnmapWindow", "UnmapSubwindows", "ConfigureWindow"}, "src/glasswyrmd/request_handlers/lifecycle.cpp"),
        ({"ChangeWindowAttributes", "GetWindowAttributes"}, "src/glasswyrmd/request_handlers/window.cpp"),
        ({"InternAtom", "GetAtomName"}, "src/glasswyrmd/request_handlers/atom.cpp"),
        ({"ChangeProperty", "DeleteProperty", "GetProperty", "ListProperties"}, "src/glasswyrmd/request_handlers/property.cpp"),
        ({"SetSelectionOwner", "GetSelectionOwner", "ConvertSelection"}, "src/glasswyrmd/request_handlers/selection.cpp"),
        ({"GrabPointer", "UngrabPointer", "GrabButton", "ChangeActivePointerGrab", "GrabKeyboard", "UngrabKeyboard", "AllowEvents", "SetInputFocus", "WarpPointer"}, "src/glasswyrmd/request_handlers/grab.cpp"),
        ({"OpenFont", "CloseFont", "QueryFont", "ListFonts"}, "src/glasswyrmd/request_handlers/font.cpp"),
        ({"QueryTextExtents", "PolyText8", "ImageText8"}, "src/glasswyrmd/request_handlers/text.cpp"),
        ({"CreatePixmap", "FreePixmap", "CreateGc", "ChangeGc", "FreeGc"}, "src/glasswyrmd/request_handlers/drawable_resource.cpp"),
        ({"ClearArea", "CopyArea", "PolyLine", "PolySegment", "FillPoly", "PolyFillRectangle", "PolyFillArc", "PutImage"}, "src/glasswyrmd/request_handlers/drawing.cpp"),
        ({"CreateColormap", "FreeColormap", "InstallColormap", "UninstallColormap", "ListInstalledColormaps"}, "src/glasswyrmd/request_handlers/colormap.cpp"),
        ({"AllocColor", "AllocNamedColor", "FreeColors", "QueryColors", "LookupColor"}, "src/glasswyrmd/request_handlers/color.cpp"),
        ({"CreateCursor", "CreateGlyphCursor", "FreeCursor", "RecolorCursor"}, "src/glasswyrmd/request_handlers/cursor.cpp"),
    ]
    for names, path in groups:
        if name in names:
            return path
    if name == "NoOperation":
        return "src/glasswyrmd/request_dispatcher.cpp"
    return "src/glasswyrmd/request_handlers/query.cpp"


def request_fixture_profiles(name: str) -> list[str]:
    profiles = ["x11-wire"]
    if name in {"CreateWindow", "DestroyWindow", "MapWindow", "MapSubwindows", "UnmapWindow", "UnmapSubwindows", "ConfigureWindow", "ChangeWindowAttributes", "GetWindowAttributes", "GetGeometry", "QueryTree"}:
        profiles += ["structural-events", "gwipc-contracts"]
    elif name in {"SetSelectionOwner", "GetSelectionOwner", "ConvertSelection", "SendEvent", "GrabPointer", "UngrabPointer", "GrabButton", "ChangeActivePointerGrab", "GrabKeyboard", "UngrabKeyboard", "AllowEvents", "QueryPointer", "TranslateCoordinates", "WarpPointer", "SetInputFocus", "GetInputFocus", "QueryKeymap", "GetKeyboardMapping", "ChangeKeyboardControl", "GetKeyboardControl", "Bell", "GetPointerMapping", "GetModifierMapping"}:
        profiles += ["input-events", "gwipc-contracts"]
    elif name in {"CreatePixmap", "FreePixmap", "CreateGc", "ChangeGc", "FreeGc", "ClearArea", "CopyArea", "PolyLine", "PolySegment", "FillPoly", "PolyFillRectangle", "PolyFillArc", "PutImage", "PolyText8", "ImageText8", "OpenFont", "CloseFont", "QueryFont", "QueryTextExtents", "ListFonts", "CreateCursor", "CreateGlyphCursor", "FreeCursor", "RecolorCursor"}:
        profiles += ["rendering-scenes"]
    return profiles


def request_mixed_profiles(rust_present: bool) -> list[str]:
    profiles = ["legacy-server-oracle"]
    if rust_present:
        profiles.append("rust-server-process-oracle")
    profiles += ["wp0c-rust-server-legacy-peers", "wp0c-rust-server-rust-peers"]
    return profiles


def extension_fixture_profiles(name: str) -> list[str]:
    if name in {"RANDR", "GW_SCALE"}:
        return ["x11-wire", "m13-scale-output"]
    if name == "GW_VRR":
        return ["x11-wire", "m14-vrr"]
    if name in {"MIT-SHM", "XFIXES", "DAMAGE", "RENDER", "Composite"}:
        return ["x11-wire", "rendering-scenes"]
    return ["x11-wire"]


def files_under(prefixes: list[str]) -> list[str]:
    result: set[str] = set()
    for prefix in prefixes:
        result.update(tracked(prefix))
    return sorted(path for path in result if (ROOT / path).is_file())


def frozen_profile(prefixes: list[str], documents: list[str] | None = None) -> dict[str, object]:
    files = files_under(prefixes)
    documents = documents or []
    return {
        "file_count": len(files),
        "tree_sha256": tree_sha256(files),
        "files": [{"path": path, "sha256": sha256_path(path)} for path in files],
        "documents": [{"path": path, "sha256": sha256_path(path)} for path in documents],
    }

def phase_for_source(path: str) -> str:
    p = path.lower()
    if any(word in p for word in ("event", "input", "grab", "selection", "cursor", "extension", "xfixes", "damage", "randr", "gw_scale", "vrr")):
        return "WP3/WP5D"
    if any(word in p for word in ("lifecycle", "compositor", "published_buffer", "raster", "render", "drawable", "pixel", "picture", "shm")):
        return "WP4/WP5B-WP5C"
    if any(word in p for word in ("listener", "client_connection", "server_reactor", "server_runtime", "server_signals", "main.cpp", "options.")):
        return "WP1/WP5A"
    return "WP2/WP5A"


def phase_for_test(path: str) -> str:
    p = path.lower()
    if any(word in p for word in ("input", "selection", "cursor", "grab", "extension", "randr", "damage", "xfixes", "vrr")):
        return "WP3/WP5D"
    if any(word in p for word in ("lifecycle", "render", "drawable", "pixel", "compositor", "shm")):
        return "WP4/WP5B-WP5C"
    if any(word in p for word in ("handshake", "setup", "malformed", "core_requests", "atom", "property", "window")):
        return "WP1-WP2/WP5A"
    return "WP5 acceptance"


def build_inventory() -> dict[str, object]:
    legacy_dispatch = parse_legacy_dispatch()
    rust_dispatch = parse_rust_dispatch()
    core = []
    for name, opcode in parse_core_opcodes():
        rust_present = name in rust_dispatch
        rust_decoder = request_rust_decoder_file(name)
        core.append({
            "name": name,
            "opcode": opcode,
            "legacy_handler": legacy_dispatch.get(name),
            "legacy_implementation": request_legacy_file(name),
            "rust_decoder_present": rust_decoder is not None,
            "rust_decoder_implementation": rust_decoder,
            "rust_dispatch_present": rust_present,
            "rust_implementation": "crates/glasswyrm-x11/src/dispatch.rs" if rust_present else None,
            "fixture_profiles": request_fixture_profiles(name),
            "mixed_stack_profiles": request_mixed_profiles(rust_present),
            "cutover_phase": "WP1/WP5A" if rust_present else "WP2-WP5",
        })

    legacy_sources = [p for p in tracked("src/glasswyrmd") if p.endswith((".cpp", ".hpp"))]
    server_tests = [p for p in tracked("tests") if p.endswith((".cpp", ".hpp", ".py", ".sh")) and any(
        token in p.lower() for token in (
            "glasswyrmd", "server_", "server-", "lifecycle", "selection", "input", "cursor",
            "x11", "property", "atom", "window", "drawable", "render", "randr", "damage",
            "composite", "xfixes", "shm", "font", "grab", "colormap"
        )
    )]
    fixture_paths = [p for p in tracked("tests/fixtures") if not p.endswith(".gitkeep")]
    vocabulary_docs = [
        "docs/extensions/M13_GW_SCALE.md", "docs/extensions/M13_RANDR.md",
        "docs/ipc/M13_OUTPUT_MANAGEMENT.md", "docs/extensions/M14_GW_VRR.md",
        "docs/ipc/M14_VRR_CONTRACT.md", "docs/ipc/M14_VRR_CONTRACTS.md",
        "docs/wm/M14_VRR_POLICY.md", "docs/output/M14_VRR_CAPABILITIES.md",
    ]
    frozen_profiles = {
        "x11-wire": frozen_profile(["tests/fixtures/x11-transition"]),
        "gwipc-contracts": frozen_profile(["tests/fixtures/gwipc"]),
        "structural-events": frozen_profile(["tests/fixtures/m6"]),
        "input-events": frozen_profile(["tests/fixtures/m7", "tests/fixtures/m8"]),
        "rendering-scenes": frozen_profile(["tests/fixtures/m4", "tests/fixtures/m9", "tests/fixtures/m12"]),
        "m13-scale-output": frozen_profile(["tests/fixtures/m13"], vocabulary_docs[:3]),
        "m14-vrr": frozen_profile(["tests/fixtures/m14"], vocabulary_docs[3:]),
    }
    mixed_profiles = {
        "legacy-server-oracle": {
            "server": "legacy", "wm": "legacy-or-not-applicable", "compositor": "legacy-or-not-applicable",
            "status": "available", "gate": "retained C++ oracle and Tier-4 selectors",
        },
        "rust-server-process-oracle": {
            "server": "rust", "wm": "not-applicable", "compositor": "not-applicable",
            "status": "available-for-current-core/discovery-subset", "gate": "Rust glasswyrmd process oracle tests",
        },
        "wp0c-rust-server-legacy-peers": {
            "server": "rust", "wm": "legacy", "compositor": "legacy",
            "status": "pending-WP0C", "gate": "WP0C topology 5",
        },
        "wp0c-rust-server-rust-peers": {
            "server": "rust", "wm": "rust", "compositor": "rust",
            "status": "pending-WP0C", "gate": "WP0C topology 8",
        },
    }
    adapters = [
        {
            "name": "rust-server-process-oracle-override",
            "paths": [
                "crates/glasswyrmd/tests/extension_discovery_process.rs",
                "crates/glasswyrmd/tests/property_process.rs",
                "crates/glasswyrmd/tests/window_process.rs",
            ],
            "purpose": "run the same frozen process exchange against the Rust candidate or retained legacy oracle",
            "deletion_gate": "WP7 after legacy glasswyrmd oracle deletion; retain the Rust-side process tests",
        },
        {
            "name": "legacy-probes-rust-gwm-candidate",
            "paths": ["crates/xtask/src/lib.rs", "tests/manifest/foundation/wm"],
            "purpose": "legacy acceptance probes drive the Rust gwm candidate during migration",
            "deletion_gate": "WP7 once equivalent Rust-native/package acceptance replaces the legacy probes",
        },
        {
            "name": "legacy-probes-rust-gwcomp-candidate",
            "paths": ["crates/xtask/src/lib.rs", "tests/manifest/graphics/headless"],
            "purpose": "legacy headless/render probes drive the Rust gwcomp candidate during migration",
            "deletion_gate": "WP7 once equivalent Rust-native/package acceptance replaces the legacy probes",
        },
        {
            "name": "legacy-fake-server-rust-tools",
            "paths": ["crates/xtask/src/lib.rs", "tests/tools/output_tools_test.sh"],
            "purpose": "retained fake server validates Rust gwinfo/gwout wire compatibility",
            "deletion_gate": "WP7 after all-Rust server/tool package parity",
        },
        {
            "name": "installed-gwipc-c-abi",
            "paths": ["include/glasswyrm/ipc", "src/ipc"],
            "purpose": "installed compatibility ABI, explicitly not a disposable migration bridge",
            "deletion_gate": "WP0C disposition plus WP7 external-consumer/package acceptance",
        },
    ]
    compatibility_constants = [
        {
            "name": "server-extension-resource-xid-range",
            "path": "src/glasswyrmd/resource_id.hpp",
            "values": ["0x1FFFF100", "0x1FFFF1FF"],
            "explanation": "server-owned extension resources such as RENDER picture formats; excluded from client XID allocation",
            "regression_test": "tests/unit/resource_id_test.cpp",
            "deletion_gate": "WP3/WP7 must either retain this as a named stable allocation or replace it with an equivalent server-resource allocator",
        },
        {
            "name": "linux-v1-handle-attachment-alias",
            "path": "crates/gw-types/src/lib.rs",
            "values": ["HANDLE_ATTACHMENTS == FD_PASSING == bit 0"],
            "explanation": "portable semantic name with exact Linux v1 compatibility alias",
            "deletion_gate": "retain through Linux v1 compatibility; removal requires WP0C/WP7 ABI disposition",
        },
        {
            "name": "linux-v1-shared-buffer-alias",
            "path": "crates/gw-types/src/lib.rs",
            "values": ["SHARED_BUFFER_OBJECTS == MEMFD_BUFFERS == bit 4"],
            "explanation": "portable semantic name with exact Linux v1 compatibility alias",
            "deletion_gate": "retain through Linux v1 compatibility; removal requires WP0C/WP7 ABI disposition",
        },
    ]
    linux_security_boundaries = {
        "shared_buffer_shrink_sigbus": {
            "implementation": "crates/gw-platform-linux/src/memory.rs",
            "tests": ["crates/gw-platform-linux/tests/platform.rs"],
            "mechanism": "Mapping requires F_SEAL_SHRINK|F_SEAL_GROW before mmap and validates range/size",
        },
        "endpoint_ancestry_same_euid": {
            "implementation": "crates/gw-ipc/src/endpoint.rs",
            "tests": ["crates/gw-ipc/src/endpoint.rs"],
            "mechanism": "descriptor-relative trusted-parent validation plus SO_PEERCRED effective-UID equality",
        },
        "role_capability_handshake": {
            "implementation": "crates/gw-ipc/src/handshake.rs",
            "tests": ["crates/gw-ipc/src/handshake.rs"],
            "mechanism": "explicit allowed-role mask, required capability intersection, negotiated limits",
        },
        "single_active_policy_peer": {
            "implementation": "crates/gwm/src/runtime.rs",
            "mechanism": "listener accepts only while connection Option is None",
        },
        "single_active_compositor_producer": {
            "implementation": "crates/gwcomp/src/runtime.rs",
            "mechanism": "listener accepts only while connection Option is None",
        },
        "buffer_geometry_stride_extents": {
            "implementation": ["crates/gwcomp/src/runtime.rs", "crates/gw-wire/src/compositor.rs"],
            "tests": ["crates/gwcomp/src/runtime.rs"],
            "mechanism": "checked storage_size conversion, stride alignment, bounded aggregate storage, and mapping range validation",
        },
    }
    semantic_boundaries = {
        "x11_client_byte_stream": {
            "implementation": "crates/glasswyrmd/src/session.rs",
            "meaning": "ordered client bytes and X11 framing; contains no Unix listener/credential ownership",
        },
        "gwipc_record": {
            "implementation": "crates/gw-ipc/src/record.rs",
            "meaning": "wire envelope, payload bytes, and typed semantic attachment descriptors",
        },
        "linux_fd_transport": {
            "implementation": "crates/gw-ipc/src/transport.rs",
            "meaning": "SOCK_SEQPACKET, OwnedFd, SCM_RIGHTS, and Linux descriptor validation",
        },
        "shared_buffer_object": {
            "implementation": ["crates/gw-types/src/lib.rs", "crates/gw-platform-linux/src/memory.rs"],
            "meaning": "portable shared-buffer semantics with Linux memfd/seal mapping as one backend",
        },
    }
    selector_sources = [
        "crates/xtask/src/lib.rs",
        "crates/xtask/src/campaign.rs",
        "tests/manifest/x11_tools/meson.build",
        "tests/manifest/foundation/wm/meson.build",
        "tests/manifest/graphics/headless/meson.build",
    ]
    return {
        "schema": "glasswyrm-rust-transition-wp0-inventory-v2",
        "authority_revision": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip(),
        "core_requests": core,
        "extensions": parse_extensions(),
        "mixed_stack_profiles": mixed_profiles,
        "frozen_contract_profiles": frozen_profiles,
        "temporary_adapters": adapters,
        "compatibility_constants": compatibility_constants,
        "linux_security_boundaries": linux_security_boundaries,
        "semantic_boundaries": semantic_boundaries,
        "test_selector_sources": [
            {"path": path, "sha256": sha256_path(path)} for path in selector_sources
        ],
        "legacy_server_sources": [{"path": p, "owner": phase_for_source(p), "deletion_gate": "WP7 after matching Rust cutover"} for p in legacy_sources],
        "server_tests": [{"path": p, "owner": phase_for_test(p), "retention": "retain as oracle/fixture until replacement parity"} for p in server_tests],
        "fixture_tree_sha256": tree_sha256(fixture_paths),
        "fixture_file_count": len(fixture_paths),
        "fixture_files": [{"path": path, "sha256": sha256_path(path)} for path in sorted(fixture_paths)],
        "canonical_metadata_sha256": {p: sha256_path(p) for p in (
            "crates/glasswyrm-x11/src/core.rs", "crates/glasswyrm-x11/src/dispatch.rs",
            "crates/glasswyrm-x11/src/extension.rs",
            "crates/glasswyrm-x11/src/lifecycle_request.rs",
            "src/glasswyrmd/request_dispatcher.cpp", "src/glasswyrmd/extension_registry.hpp",
            "src/glasswyrmd/resource_id.hpp", "tests/unit/resource_id_test.cpp",
        )},
    }


def validate_inventory(inventory: dict[str, object]) -> None:
    fixture_profiles = set(inventory["frozen_contract_profiles"])
    mixed_profiles = set(inventory["mixed_stack_profiles"])
    for section in ("core_requests", "extensions"):
        for item in inventory[section]:
            if not item.get("legacy_implementation") or not item.get("cutover_phase"):
                raise RuntimeError(f"incomplete {section} implementation mapping: {item.get('name')}")
            if not item.get("fixture_profiles") or not set(item["fixture_profiles"]) <= fixture_profiles:
                raise RuntimeError(f"invalid {section} fixture mapping: {item.get('name')}")
            if not item.get("mixed_stack_profiles") or not set(item["mixed_stack_profiles"]) <= mixed_profiles:
                raise RuntimeError(f"invalid {section} mixed-profile mapping: {item.get('name')}")
    if len(inventory["fixture_files"]) != inventory["fixture_file_count"]:
        raise RuntimeError("fixture file count does not match frozen per-file inventory")
    if not inventory["temporary_adapters"] or not inventory["compatibility_constants"]:
        raise RuntimeError("adapter/compatibility ledgers must not be empty")
    if set(inventory["semantic_boundaries"]) != {
        "x11_client_byte_stream", "gwipc_record", "linux_fd_transport", "shared_buffer_object"
    }:
        raise RuntimeError("semantic transport boundary inventory is incomplete")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    options = parser.parse_args()
    inventory = build_inventory()
    validate_inventory(inventory)
    rendered = json.dumps(inventory, indent=2, sort_keys=True) + "\n"
    if options.check:
        if not OUTPUT.is_file() or OUTPUT.read_text(encoding="utf-8") != rendered:
            print(f"stale Rust-transition inventory: {OUTPUT.relative_to(ROOT)}")
            return 1
        print(f"Rust-transition inventory is current: {OUTPUT.relative_to(ROOT)}")
        return 0
    OUTPUT.write_text(rendered, encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
