#!/usr/bin/env python3
"""Audit the supplied platform archive without modifying or linking it.

Only declarations and selected disassembly are inventoried, not decompiled C.
All output belongs in an explicit build directory outside the application and
reference repositories. ARC symbols are excluded from the public API inventory;
ARC relocations in selected functions remain evidence of the original backend.
"""

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


APP_ROOT = Path(__file__).resolve().parents[1]
HEADERS = (
    "platform/inc/data_process.h",
    "platform/inc/collect_lib.h",
    "platform/inc/transmit_lib.h",
    "platform/inc/platform_callback.h",
)
PREFIXES = ("dp_", "co_", "tr_", "pltcb_")
TARGETS = {
    "platform_callback.o": (
        "pltcb_reg_cfg_get_dev_tbl", "pltcb_cfg_get_dev_tbl",
        "pltcb_reg_para_range_def_cb", "pltcb_para_get_range_def",
    ),
    "data_process.o": (
        "dp_init", "dp_dev_add", "dp_dev_del", "dp_get_sig_value",
        "dp_set_sig_value", "dp_fast_para_get",
        "dp_fast_para_set", "dp_set_para_value_flag",
        "dp_set_para_only_dm", "dp_set_para_only_dm_no_store",
        "dp_load_def_data", "dp_para_value_reg", "dp_set_init", "dp_plat_ver_init",
    ),
    "dp_mgr.o": ("dp_mutex_lock", "dp_mutex_unlock", "dp_dm_get_value"),
    "dp_para_set.o": (
        "dp_set_para_proc", "dp_fast_get_para_proc", "dp_sig_func_done", "dp_para_set",
        "dp_dev_init_pro", "dp_init_func",
    ),
    "co_mgr.o": (
        "co_set_port_mode", "co_set_port_mode_proc", "co_get_port_mode",
        "co_init_func_reg", "co_dev_init", "co_clean_set_queue",
        "co_dp_fast_set", "co_put_passthough_queue",
    ),
    "co_cfg.o": ("co_set_scan_pkg_flag", "co_mb_create_scan_cfg"),
    "co_modbus.o": ("co_anlysis_mb_pkg", "co_anlysis_mb_pkg_proc", "co_set_value_trans_cb_reg"),
    "co_port_mgr.o": ("co_porc_scan_list",),
    "mb_slave_comm.o": ("tr_set_port_mode", "tr_get_port_mode", "tr_reg_table_func"),
    "mb_slave_work.o": (
        "tr_reg_pkt_filter_func", "tr_pkt_filter_config", "tr_mb_port_work_control",
        "tr_port_conn_status_config",
    ),
}
SOURCE_ANCHORS = {
    "platform/inc/data_process.h": (
        r"\b(DM_SIG_CFG_S|DM_SIG_DATA_S|DEV_TBL_CFG_S|DEV_TABLE_S|FUNC_PARA_S|"
        r"FUNC_PARA_ALL|DATA_SET_S|PARA_DATA_S|TIME_DATA_SET_S)\b",
    ),
    "platform/inc/collect_lib.h": (
        r"\b(CO_PORT_MODE|MODBUS_RTU_TABLE_S|CO_SCAN_PKG_ON|CO_SCAN_PKG_OFF)\b",
    ),
    "platform/inc/transmit_lib.h": (
        r"\b(MB_TABLE_CELL_S|MB_TABLE_SEG_S|TR_FUNC_CB|MB_PORT_CTRL_E)\b",
    ),
    "platform/inc/platform_callback.h": (r"\bPARA_GET_RANGE_DEF\b",),
    "platform/inc/arc_byte_order.h": (r"^#define BYTE_ORDER_",),
    "app/main.c": (
        r"\b(project_register_callback|pltcb_reg_\w+|dp_init|local_equip_init|"
        r"co_init|tr_init)\s*\(",
    ),
    "app/cfg/equip/dev_cfg.c": (r"\b(CAST_DEV_TABLE_DEF|g_arm_seg_table_t)\b",),
    "app/timer/timefunction.c": (r"\b(g_timer_callback|timing|cfg_get_timer_reg|cfg_get_timer_func_num)\b",),
    "app/port_filter/filter.c": (r"MPP_RET_(?:OK|DISCONTINUE|AES|ERR)",),
    "app/plc/tr_plc_func.c": (
        r"\btr_reg_table_func\b", r"MPP_RET_(?:OK|DISCONTINUE|CONTINUE|ERR)",
    ),
    "app/timer/FAN.C": (r"\bdp_set_sig_value\b",),
    "app/process/dsp/dsp_mgr.c": (r"\bdp_set_sig_value\b",),
}


def run(argv, *, binary=False):
    result = subprocess.run(argv, check=False, capture_output=True)
    if result.returncode:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(map(str, argv))}\n{message}")
    return result.stdout if binary else result.stdout.decode("utf-8", errors="replace")


def discover_tools(prefix):
    """Prefer PATH/Zephyr SDK settings, then bounded conventional install paths."""
    candidates = [prefix] if prefix else []
    if not prefix:
        candidates.extend(filter(None, (os.environ.get("CROSS_COMPILE"),)))
        for name in ("arm-zephyr-eabi-objdump", "arm-none-eabi-objdump"):
            found = shutil.which(name)
            if found:
                candidates.append(found.removesuffix("objdump"))
        sdk = os.environ.get("ZEPHYR_SDK_INSTALL_DIR")
        if sdk:
            candidates.extend(str(Path(sdk) / part / "bin/arm-zephyr-eabi-")
                              for part in ("arm-zephyr-eabi", "gnu/arm-zephyr-eabi"))
        for base in (Path.home() / "Tools/toolchain", Path.home(), Path("/opt")):
            for pattern in (
                "zephyr-sdk-*/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump",
                "zephyr-sdk-*/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump",
                "arm-gnu-toolchain-*/bin/arm-none-eabi-objdump",
            ):
                candidates.extend(str(path).removesuffix("objdump")
                                  for path in sorted(base.glob(pattern), reverse=True))
    for candidate in dict.fromkeys(candidates):
        paths = {name: shutil.which(candidate + name) for name in ("nm", "objdump", "ar")}
        if not all(paths.values()):
            continue
        versions = {name: run([path, "--version"]).splitlines()[0]
                    for name, path in paths.items()}
        if (all("GNU" in version for version in versions.values())
                and "elf32-littlearm" in run([paths["objdump"], "-i"])):
            return paths, versions
    raise RuntimeError("GNU Arm nm/objdump/ar not found; pass --toolchain-prefix /path/bin/arm-none-eabi-")


def strip_comments(text):
    # Preserve offsets and line numbers for source references.
    return re.sub(r"/\*.*?\*/|//[^\n]*", lambda match: re.sub(r"[^\n]", " ", match[0]),
                  text, flags=re.S)


def header_inventory(source):
    declarations = []
    sources = []
    pattern = re.compile(
        r"^[ \t]*(?:extern[ \t]+)?[\w* \t]+?\b"
        r"((?:dp|co|tr|pltcb)_[A-Za-z0-9_]+)[ \t]*\(([^;{}]*)\)[ \t]*;", re.M)
    for relative in HEADERS:
        raw = (source / relative).read_bytes()
        text = raw.decode("utf-8")
        sources.append({"path": relative, "sha256": hashlib.sha256(raw).hexdigest(), "size": len(raw)})
        for match in pattern.finditer(strip_comments(text)):
            declarations.append({
                "name": match[1], "header": relative,
                "line": text.count("\n", 0, match.start()) + 1,
                "prototype": " ".join(match[0].split()),
            })
    return declarations, sources


def parse_symbols(text):
    symbols = []
    pattern = re.compile(r"^.+\[(.+)\]: (\S+) ([A-Za-z?])(?:\s+([0-9a-fA-F]+))?(?:\s+([0-9a-fA-F]+))?\s*$")
    for line in text.splitlines():
        match = pattern.match(line)
        if not match:
            if line.strip():
                raise RuntimeError(f"Unrecognized GNU nm record: {line}")
            continue
        member, name, kind, value, size = match.groups()
        symbols.append({"member": member, "name": name, "type": kind,
                        "value": int(value, 16) if value else None,
                        "size": int(size, 16) if size else None})
    return symbols


def selected_disassembly(text, names):
    """Keep whole requested function sections and their relocation records."""
    sections = re.split(r"(?=^Disassembly of section )", text, flags=re.M)
    found = []
    selected = []
    for section in sections:
        labels = re.findall(r"^[0-9a-fA-F]+ <([^>]+)>:", section, re.M)
        matches = sorted(set(labels) & set(names))
        if matches:
            selected.append(section.rstrip())
            found.extend(matches)
    return "\n\n".join(selected) + "\n", sorted(set(found))


def references(text, member):
    """Relocation targets, not unlinked BL display labels, identify callees."""
    result = []
    function = None
    origin = 0
    section = None
    for line in text.splitlines():
        match = re.match(r"Disassembly of section (.+):", line)
        if match:
            section = match[1]
        match = re.match(r"([0-9a-fA-F]+) <([^>]+)>:", line)
        if match:
            origin, function = int(match[1], 16), match[2]
        match = re.match(r"\s*([0-9a-fA-F]+):\s+(R_ARM_\w+)\s+(.+)", line)
        if match and function:
            address, relocation, target = match.groups()
            result.append({"member": member, "section": section, "function": function,
                           "offset": f"0x{int(address, 16) - origin:x}",
                           "relocation": relocation, "target": target.strip()})
    return result


def source_references(source):
    result = []
    for relative, patterns in SOURCE_ANCHORS.items():
        path = source / relative
        if not path.is_file():
            result.append({"path": relative, "missing": True})
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if any(re.search(pattern, line) for pattern in patterns):
                result.append({"path": relative, "line": number, "text": line.strip()})
    return result


def write_output(output, relative, text):
    path = output / relative
    if not path.resolve().is_relative_to(output):
        raise ValueError(f"Output symlink escapes build directory: {path}")
    path.write_text(text, encoding="utf-8")


def analyze(source, output, prefix):
    source, output = source.resolve(), output.resolve()
    if output.is_relative_to(source) or output.is_relative_to(APP_ROOT):
        raise ValueError("--output must be a build directory outside both source/application repositories")
    archive = source / "platform/libs/plateform.lib"
    raw_archive = archive.read_bytes()
    if not raw_archive.startswith(b"!<arch>\n"):
        raise ValueError(f"Expected a regular ar archive: {archive}")
    tools, versions = discover_tools(prefix)
    members_text = run([tools["ar"], "t", str(archive)])
    members = members_text.splitlines()
    if len(members) != len(set(members)):
        raise ValueError("Duplicate archive member names: ar p cannot identify members unambiguously")
    symbols_text = run([tools["nm"], "-A", "--format=posix", str(archive)])
    symbols = parse_symbols(symbols_text)
    declarations, header_sources = header_inventory(source)
    exports = [item for item in symbols if item["type"] in ("T", "W")
               and item["name"].startswith(PREFIXES)]
    export_names = {item["name"] for item in exports}
    for declaration in declarations:
        declaration["exported"] = declaration["name"] in export_names
    public_names = {item["name"] for item in declarations}
    for item in exports:
        item["declared_in_scoped_headers"] = item["name"] in public_names
    output.mkdir(parents=True, exist_ok=True)
    evidence_dir = output / "evidence"
    evidence_dir.mkdir(exist_ok=True)
    evidence = []
    all_references = []
    with tempfile.TemporaryDirectory(prefix="reference-objects-") as temporary:
        for member, functions in TARGETS.items():
            if member not in members:
                evidence.append({"member": member, "missing_member": True})
                continue
            # Never extract archive-controlled paths; use only our target basename.
            obj = Path(temporary) / member
            contents = run([tools["ar"], "p", str(archive), member], binary=True)
            obj.write_bytes(contents)
            disassembly = run([tools["objdump"], "-dr", str(obj)])
            selected, found = selected_disassembly(disassembly, functions)
            sections = run([tools["objdump"], "-h", str(obj)])
            sections = sections.replace(str(obj), member)
            debug_sections = sorted(set(re.findall(r"\s(\.debug_\S+)\s", sections)))
            filename = f"evidence/{member}.txt"
            write_output(output, filename, selected)
            write_output(output, f"evidence/{member}.sections.txt", sections)
            all_references.extend(references(selected, member))
            evidence.append({"member": member, "sha256": hashlib.sha256(contents).hexdigest(),
                             "size": len(contents), "file": filename, "functions": found,
                             "missing_functions": sorted(set(functions) - set(found)),
                             "debug_sections": debug_sections,
                             "has_source_dwarf": ".debug_info" in debug_sections or ".debug_line" in debug_sections})
    counts = {
        "members": len(members), "symbol_records": len(symbols),
        "defined_global_functions": sum(item["type"] in ("T", "W") for item in symbols),
        "undefined_references": sum(item["type"] in ("U", "w", "v") for item in symbols),
        "scoped_exported_functions": len(exports),
        "scoped_header_declarations": len(declarations),
        "scoped_public_names": len(public_names),
        "scoped_header_declarations_exported": sum(item["exported"] for item in declarations),
        "exports_by_prefix": dict(Counter(item["name"].split("_", 1)[0] for item in exports)),
    }
    report = {
        "schema_version": 1,
        "source": str(source),
        "archive": {"path": str(archive), "size": len(raw_archive),
                    "sha256": hashlib.sha256(raw_archive).hexdigest()},
        "tools": {name: {"path": path, "version": versions[name]} for name, path in tools.items()},
        "scope": {"prefixes": list(PREFIXES), "headers": list(HEADERS),
                  "excluded": ["ARC API implementation/wrappers", "other protocol subsystems", "full decompilation"],
                  "notes": ["Counts cover archive symbol records, not unique callable public APIs.",
                            "Header inventory is lexical, not a C preprocessor/type checker.",
                            "ARC relocation targets remain visible as original implementation evidence.",
                            "Offsets are relative to the named object function, not final firmware addresses."]},
        "counts": counts, "header_sources": header_sources,
        "public_api": declarations, "exported_functions": exports,
        "selected_evidence": evidence, "relocations": all_references,
        "source_references": source_references(source),
    }
    write_output(output, "audit.json", json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    write_output(output, "members.txt", members_text)
    write_output(output, "symbols.txt", symbols_text)
    lines = ["Reference platform audit", f"Archive: {archive}",
             f"SHA256: {report['archive']['sha256']}", f"Bytes: {len(raw_archive)}",
             f"Counts: {json.dumps(counts, sort_keys=True)}", "",
             "Scope: dp_/co_/tr_/pltcb_ in the four listed public headers; ARC wrappers excluded.",
             "Evidence is selected disassembly with relocations, not reconstructed source.", "",
             "Public declarations (source line; archive export present):"]
    for item in declarations:
        lines.append(f"{item['header']}:{item['line']} [{item['exported']}] {item['prototype']}")
    lines.extend(("", "Selected object evidence:"))
    for item in evidence:
        lines.append(json.dumps(item, ensure_ascii=False, sort_keys=True))
    lines.extend(("", "Call/jump relocations (ignore unresolved BL display labels):"))
    for item in all_references:
        if "CALL" in item["relocation"] or "JUMP" in item["relocation"]:
            lines.append(f"{item['member']}:{item['function']}+{item['offset']} {item['relocation']} {item['target']}")
    write_output(output, "audit.txt", "\n".join(lines) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="Reference CODE directory (read only)")
    parser.add_argument("--output", required=True, type=Path, help="Dedicated build output directory")
    parser.add_argument("--toolchain-prefix", help="GNU Arm prefix, e.g. /opt/toolchain/bin/arm-none-eabi-")
    args = parser.parse_args()
    try:
        report = analyze(args.source, args.output, args.toolchain_prefix)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"error: {error}\n")
    print(f"Wrote {args.output.resolve() / 'audit.json'}")
    print(json.dumps(report["counts"], ensure_ascii=False, sort_keys=True))


if __name__ == "__main__":
    main()
