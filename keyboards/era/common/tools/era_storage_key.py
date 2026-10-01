#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""Refuse a stored-format change that no reset decision covers.

Fingerprints the sources that define stored bytes -- the ERA block layout and
stored types, QMK's eeconfig/VIA/dynamic-keymap addresses and persisted types,
the RGB effect order, each board's keymap geometry -- against
`era_storage_key.json`. A changed fingerprint fails until
`ERA_EEPROM_RESET_KEY` or `QMK_BUILDDATE` moves or the record is rewritten with
the reason the stored bytes stay valid (era_host_peer_storage_contract.md,
Stored-Data Compatibility).

  python keyboards/era/common/tools/era_storage_key.py             check the working tree
  python keyboards/era/common/tools/era_storage_key.py --index     check the staged index
  python keyboards/era/common/tools/era_storage_key.py --record "reason"
  python keyboards/era/common/tools/era_storage_key.py --record "reason" --index
                                                                   record what is staged
"""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
RECORD = "keyboards/era/common/tools/era_storage_key.json"
LAYOUT = "keyboards/era/common/storage/era_eeprom_layout.h"
VIA_MAGIC = "keyboards/era/common/storage/era_via_magic.h"
# Where every ERA target, the ATmega exception included, force-includes VIA_MAGIC.
VIA_MAGIC_RULES = "keyboards/era/common/system/era_build_variant_rules.mk"
IO_SOURCE = "keyboards/era/common/storage/era_eeprom_config_io.c"
KEYS = {"QMK_BUILDDATE": VIA_MAGIC, "ERA_EEPROM_RESET_KEY": LAYOUT}
STORAGE_HEADERS = (
    LAYOUT,
    "keyboards/era/common/storage/era_storage_layout.h",
    "keyboards/era/common/storage/era_storage_adoption.h",
)
QMK_DEFINES = (
    ("quantum/eeconfig.h", r"EECONFIG_\w+"),
    ("quantum/nvm/eeprom/nvm_eeprom_eeconfig_internal.h", r"EECONFIG_\w+"),
    ("quantum/nvm/eeprom/nvm_eeprom_via_internal.h", r"VIA_EEPROM_\w+"),
    ("quantum/nvm/eeprom/nvm_dynamic_keymap.c", r"DYNAMIC_KEYMAP_\w+"),
)
# The order of these lists is the stored effect number, and the keycode table
# gives the numbers a stored keymap holds.
QMK_ORDER_FILES = (
    "quantum/rgblight/rgblight_modes.h",
    "quantum/rgb_matrix/animations/rgb_matrix_effects.inc",
    "quantum/keycodes.h",
)
QMK_TYPES = (
    "keymap_config_t",
    "debug_config_t",
    "rgblight_config_t",
    "rgb_config_t",
    "backlight_config_t",
)
BOARD_DEFINES = (
    r"DYNAMIC_KEYMAP_\w+|EECONFIG_\w+|VIA_EEPROM_\w+|MATRIX_ROWS|MATRIX_COLS"
    r"|RGBLIGHT_EFFECT_\w+|RGBLIGHT_ANIMATIONS|ENABLE_RGB_MATRIX_\w+|ERA_EEPROM_\w+"
    r"|ERA_TAP_DANCE_KEYCODE_BASE"
)
# A board's own keycodes (TD0.., diagnostic keys) are numbers a stored keymap holds.
BOARD_KEYCODE_ENUM = re.compile(r"\benum\s+\w*keycodes?\s*\{[^}]*\}")
VALIDITY_DEFINES = r"\w+_(?:SIGNATURE|VALID|VERSION|MAGIC)"
# Validity constants a stored block is checked against, beyond its caller's own files.
VALIDITY_SOURCES = ("keyboards/era/common/split/era_split_sync_storage.h",)
PLAIN_TYPES = {"bool", "char", "int", "unsigned"} | {
    f"{sign}int{width}_t" for sign in ("", "u") for width in (8, 16, 32, 64)
}
ERA_CALL = re.compile(r"\bera_eeprom_(?:update|read)_config\(\s*&?\s*(\w+)")


class Tree:
    """The working tree, or the staged index with `--index`."""

    def __init__(self, index: bool):
        self.index = index
        self.cache: dict[str, str | None] = {}
        self.batch = None

    def _staged(self, rel: str) -> str | None:
        # One cat-file process for the whole run; a `git show` per file is seconds on Windows.
        if self.batch is None:
            self.batch = subprocess.Popen(["git", "cat-file", "--batch"], cwd=REPO,
                                          stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.batch.stdin.write(f":{rel}\n".encode("utf-8"))
        self.batch.stdin.flush()
        header = self.batch.stdout.readline().decode("utf-8").split()
        if len(header) != 3:
            return None
        body = self.batch.stdout.read(int(header[2]))
        self.batch.stdout.read(1)
        return body.decode("utf-8")

    def files(self, prefix: str) -> list[str]:
        if self.index:
            out = subprocess.run(["git", "ls-files", "--", prefix], cwd=REPO,
                                 capture_output=True, text=True, check=True).stdout
            return sorted(line for line in out.splitlines() if line)
        root = REPO / prefix
        return sorted(p.relative_to(REPO).as_posix() for p in root.rglob("*") if p.is_file())

    def read(self, rel: str) -> str | None:
        if rel not in self.cache:
            if self.index:
                self.cache[rel] = self._staged(rel)
            else:
                path = REPO / rel
                self.cache[rel] = path.read_text(encoding="utf-8") if path.is_file() else None
        return self.cache[rel]


def strip_c(text: str) -> str:
    """Comments and layout are not format; drop them before hashing."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text.replace("\\\n", " ")


def defines(text: str, pattern: str, skip=()) -> list[str]:
    wanted = re.compile(rf"(?:{pattern})")
    rows = []
    for match in re.finditer(r"^\s*#\s*define\s+(\w+)(\(?)([^\n]*)$", strip_c(text), re.M):
        name, function_like, value = match.groups()
        if function_like or name in skip or not wanted.fullmatch(name):
            continue
        rows.append(f"{name} {' '.join(value.split())}")
    return rows


def validity_constants(text: str) -> list[str]:
    """Signature/version/valid markers, as #defines or as enum constants."""
    rows = defines(text, VALIDITY_DEFINES)
    for name, value in re.findall(rf"\b({VALIDITY_DEFINES})\s*=(?!=)\s*([^,}}\n;]+)", strip_c(text)):
        rows.append(f"{name} = {' '.join(value.split())}")
    return rows


def typedefs(text: str) -> dict[str, str]:
    text, found = strip_c(text), {}
    for match in re.finditer(r"typedef\s+(?:struct|union|enum)\b[^{;]*\{", text):
        depth, index = 1, match.end()
        while depth and index < len(text):
            depth += {"{": 1, "}": -1}.get(text[index], 0)
            index += 1
        tail = re.match(r"\s*(\w+)\s*;", text[index:])
        if tail:
            found[tail.group(1)] = " ".join(text[match.start():index].split())
    return found


def persisted_types(tree: Tree, sources: list[str], findings: list[str]) -> set[str]:
    """Types of every variable ERA code hands to the ERA block, from the call sites."""
    names: set[str] = set()
    for rel in sources:
        text = strip_c(tree.read(rel) or "")
        for var in sorted(set(ERA_CALL.findall(text))):
            declared = re.findall(
                rf"(?:^|[;{{(,]|\bstatic|\bconst)\s*(?:const\s+|volatile\s+)*(\w+)\s*\*?\s*(?:const\s+)?\b{var}\b\s*(?:\[[^\]]*\])?\s*[=;,)\[]",
                text, re.M)
            kinds = {kind for kind in declared if kind not in ("return", "sizeof", "static", "const")}
            if not kinds:
                findings.append(f"{rel}: cannot find the type of `{var}`, which it stores in the ERA block")
            names |= {kind for kind in kinds if kind not in PLAIN_TYPES}
    return names


def board_facts(data: dict) -> dict:
    pins = data.get("matrix_pins", {})
    split_pins = data.get("split", {}).get("matrix_pins", {}).get("right", {})

    def geometry(block):
        return {key: len(value) if isinstance(value, list) else value for key, value in block.items()}

    animations = lambda section: sorted(k for k, v in data.get(section, {}).get("animations", {}).items() if v)
    return {
        "matrix_pins": geometry(pins),
        "split_right_pins": geometry(split_pins),
        "matrix_size": data.get("matrix_size"),
        "split": bool(data.get("split", {}).get("enabled")),
        "dynamic_keymap": data.get("dynamic_keymap"),
        "rgblight_animations": animations("rgblight"),
        "rgb_matrix_animations": animations("rgb_matrix"),
        "encoders": len(data.get("encoder", {}).get("rotary", [])),
        "encoder_map": bool(data.get("features", {}).get("encoder_map")),
        "backlight_levels": data.get("backlight", {}).get("levels"),
    }


def fingerprint(tree: Tree) -> tuple[dict[str, str], dict[str, str], list[str]]:
    findings: list[str] = []
    groups: dict[str, list[str]] = {}

    def need(rel):
        text = tree.read(rel)
        if text is None:
            findings.append(f"{rel}: missing")
            return ""
        return text

    keys = {}
    for name, rel in KEYS.items():
        match = re.search(rf'^#define\s+{name}\s+(\S+)', need(rel), re.M)
        if match is None:
            findings.append(f"{rel}: `{name}` is not defined")
        keys[name] = match.group(1).strip('"') if match else None
    if f"CONFIG_H += {VIA_MAGIC}" not in need(VIA_MAGIC_RULES):
        findings.append(f"{VIA_MAGIC_RULES}: no longer force-includes {VIA_MAGIC}, so the "
                        "VIA magic would follow the build clock again")

    for rel in STORAGE_HEADERS:
        text = need(rel)
        groups[f"define {rel}"] = defines(text, r"\w+", skip=KEYS)
        groups[f"types {rel}"] = sorted(typedefs(text).values())
    for rel, pattern in QMK_DEFINES:
        groups[f"define {rel}"] = defines(need(rel), pattern)
    for rel in QMK_ORDER_FILES:
        groups[f"order {rel}"] = [" ".join(strip_c(need(rel)).split())]
    for rel in tree.files("keyboards/era"):
        if rel.endswith((".h", ".c")):
            enums = BOARD_KEYCODE_ENUM.findall(strip_c(tree.read(rel) or ""))
            if enums:
                groups[f"keycodes {rel}"] = [" ".join(body.split()) for body in enums]

    quantum_types: dict[str, list[str]] = {}
    for rel in tree.files("quantum"):
        if rel.endswith(".h"):
            for name, body in typedefs(tree.read(rel) or "").items():
                quantum_types.setdefault(name, []).append(body)
    for name in QMK_TYPES:
        bodies = quantum_types.get(name, [])
        if len(bodies) != 1:
            findings.append(f"QMK persisted type `{name}` has {len(bodies)} definitions")
        groups[f"type {name}"] = bodies

    era_files = [rel for rel in tree.files("keyboards/era") if rel.endswith((".c", ".h"))]
    era_types: dict[str, list[str]] = {}
    for rel in era_files:
        for name, body in typedefs(tree.read(rel) or "").items():
            era_types.setdefault(name, []).append(body)
    # The accessor's own definition is not a caller.
    callers = [rel for rel in era_files if rel.endswith(".c") and rel != IO_SOURCE
               and ERA_CALL.search(tree.read(rel) or "")]
    pending, seen = sorted(persisted_types(tree, callers, findings)), set()
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        bodies = era_types.get(name, [])
        if len(bodies) != 1:
            findings.append(f"ERA persisted type `{name}` has {len(bodies)} definitions")
            continue
        groups[f"type {name}"] = bodies
        pending += [inner for inner in re.findall(r"\b(\w+_t)\b", bodies[0])
                    if inner != name and inner in era_types]
    for rel in callers + list(VALIDITY_SOURCES):
        rows = []
        for source in (rel, rel[:-2] + ".h") if rel.endswith(".c") else (rel,):
            text = tree.read(source)
            if text is not None:
                rows += validity_constants(text)
        groups[f"validity {rel}"] = rows

    for rel in tree.files("keyboards/era"):
        if rel.endswith("/keyboard.json"):
            groups[f"board {rel}"] = [json.dumps(board_facts(json.loads(need(rel))), sort_keys=True)]
        elif rel.endswith("/config.h") and not rel.startswith("keyboards/era/common/"):
            groups[f"board {rel}"] = sorted(defines(need(rel), BOARD_DEFINES))
        elif rel.endswith("rules.mk"):
            rows = re.findall(r"^\s*(ENCODER_MAP_ENABLE\s*[:?]?=\s*\S+)", need(rel), re.M)
            if rows:
                groups[f"board {rel}"] = rows

    hashed = {name: hashlib.sha256("\n".join(rows).encode("utf-8")).hexdigest()[:16]
              for name, rows in groups.items()}
    return keys, dict(sorted(hashed.items())), findings


def check(tree: Tree) -> list[str]:
    keys, groups, findings = fingerprint(tree)
    text = tree.read(RECORD)
    if text is None:
        return findings + [f"{RECORD}: missing -- create it with --record \"reason\""]
    try:
        record = json.loads(text)
    except ValueError as error:
        return findings + [f"{RECORD}: unreadable ({error})"]
    for name in KEYS:
        if record.get("keys", {}).get(name) != keys[name]:
            findings.append(
                f"`{name}` is {keys[name]} but {RECORD} records {record.get('keys', {}).get(name)} -- "
                "if that reset is intended, record it with --record \"reason\"")
    recorded = record.get("groups", {})
    changed = sorted(name for name in groups.keys() | recorded.keys()
                     if groups.get(name) != recorded.get(name))
    if changed:
        shown = ", ".join(changed[:4]) + (" ..." if len(changed) > 4 else "")
        findings.append(
            f"stored format differs from {RECORD} ({shown}). If bytes an older image stored "
            "would now mean something else, move `ERA_EEPROM_RESET_KEY` (everything) or, for VIA's "
            "region only, `QMK_BUILDDATE`, and accept that reset; if they stay valid, keep "
            "the keys and record the reason with --record \"reason\" (add --index when the working "
            "tree holds changes this commit does not stage) "
            "(era_host_peer_storage_contract.md, Stored-Data Compatibility)")
    return findings


def main() -> int:
    args = sys.argv[1:]
    if args[:1] == ["--record"] and len(args) in (2, 3) and args[1].strip() and args[2:] in ([], ["--index"]):
        # --index records exactly what is staged, which is what the commit hook checks.
        tree = Tree(index=args[2:] == ["--index"])
        keys, groups, findings = fingerprint(tree)
        if findings:
            print("\n".join(findings))
            return 1
        record = {"keys": keys, "reason": args[1].strip(), "groups": groups}
        (REPO / RECORD).write_bytes((json.dumps(record, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
        print(f"recorded {RECORD}: {len(groups)} groups, keys {keys}")
        return 0
    if args not in ([], ["--index"]):
        print(__doc__)
        return 2
    findings = check(Tree(index=args == ["--index"]))
    if findings:
        print("\n".join(f"storage: {line}" for line in findings))
        return 1
    print("storage: format matches the recorded reset keys")
    return 0


if __name__ == "__main__":
    sys.exit(main())
