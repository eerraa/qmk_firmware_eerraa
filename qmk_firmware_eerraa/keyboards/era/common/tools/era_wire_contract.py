#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bind official VIA enums and ERA aliases to the generated wire registry.

The generated header is input, never rewritten. --index checks staged blobs.
Firmware also expands the official tables as compile-time assertions in via.h.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
HEADER = "keyboards/era/common/system/era_wire.h"
VIA_H = "quantum/via.h"
KEYCODES_H = "quantum/keycodes.h"
SOURCE_ROOTS = ("keyboards/era/",)
EXTRA_SOURCES = (VIA_H, KEYCODES_H)
TABLES = {
    "via_command_id": "ERA_WIRE_VIA_OFFICIAL_COMMANDS",
    "via_keyboard_value_id": "ERA_WIRE_VIA_OFFICIAL_KEYBOARD_VALUES",
    "via_channel_id": "ERA_WIRE_VIA_OFFICIAL_CHANNELS",
}
# These are firmware alias namespaces, not another allocation table.
OWNED = re.compile(
    r"ERA_STATE_SYNC_(?:KEYBOARD_VALUE|ENVELOPE_VERSION|STATUS_\w+|DOMAIN_\w+)"
    r"|ERA_VIA_\w+(?:CHANNEL|VALUE_ID|VALUES_PER_SLOT)"
    r"|ERA_(?:SOCD|KKUK|MOUSEKEY|TAPPING|DEBOUNCE)_VIA_VALUE_\w+"
    r"|ERA_SPLIT_VIA_\w+_VALUE_ID"
    r"|ERA_SPLIT_LINK_LEVEL_(?:HIGH|MEDIUM|LOW)"
    r"|ERA_SOCD_MODE_(?:LAST_INPUT|NEUTRAL|FIRST_INPUT|KEY_[01]_PRIORITY)"
    r"|ERA_KKUK_MODE_REPORT_PULSE"
    r"|ERA_BACKLIGHT_EFFECT_(?:STEADY|BREATHING|PULSE_\w+)"
    r"|ERA_RGB_INDICATOR_SOURCE_(?:OFF|CAPS_LOCK|SCROLL_LOCK|NUM_LOCK)"
    r"|ERA_MATRIX_DEBOUNCE_PROFILE_(?:BALANCED|FAST|ADVANCED)"
    r"|ERA_TAP_DANCE_(?:MODE_TAG|FIELD_TERM|VIA_VALUE_ID_\w+)"
    r"|(?:TOMAK|ODESSEY)_INDICATOR_(?:OFF|CAPS_LOCK|SCROLL_LOCK|NUM_LOCK)"
    r"|RILEY_INDICATOR_(?:RGB_EFFECT|CAPS_LOCK|SCROLL_LOCK|NUM_LOCK)"
    r"|id_custom_(?:indicator_\w+|badge_only|velocikey_enable|riley_\w+)"
)


class Tree:
    def __init__(self, root=ROOT, index=False):
        self.root, self.index = Path(root), index
        self.cache = {}
        self.names = set(self.git("ls-files", "--cached").splitlines()) if index else None

    def git(self, *args):
        proc = subprocess.run(["git", *args], cwd=self.root, capture_output=True)
        if proc.returncode:
            raise ValueError(f"git {' '.join(args)} failed: {proc.stderr.decode('utf-8', errors='replace')}")
        return proc.stdout.decode("utf-8")

    def read(self, rel):
        if rel not in self.cache:
            self.cache[rel] = self.git("show", f":./{rel}") if self.index else (self.root / rel).read_text(encoding="utf-8")
        return self.cache[rel]

    def sources(self):
        if self.index:
            return sorted(p for p in self.names if p.endswith((".c", ".h"))
                          and (p.startswith(SOURCE_ROOTS) or p in EXTRA_SOURCES) and p != HEADER)
        return sorted(set(EXTRA_SOURCES) | {
            p.relative_to(self.root).as_posix()
            for prefix in SOURCE_ROOTS for p in (self.root / prefix).rglob("*")
            if p.suffix in (".c", ".h") and p.relative_to(self.root).as_posix() != HEADER
        })


def strip_c(text):
    return re.sub(r"/\*[\s\S]*?\*/|//[^\n]*", "", text)


def wire_defines(text):
    return dict(re.findall(r"^\s*#\s*define\s+(\w+)\b(?!\()[ \t]+([^\n]+)", strip_c(text), re.M))


def number(expression, defines):
    expression = expression.strip()
    seen = set()
    while expression in defines:
        if expression in seen:
            raise ValueError(f"cyclic macro: {expression}")
        seen.add(expression)
        expression = defines[expression].strip()
    expression = re.sub(r"(?i)(0x[0-9a-f]+|\d+)[ul]+\b", r"\1", expression)
    if not re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", expression):
        raise ValueError(f"unsupported integer expression: {expression}")
    return int(expression, 16 if expression.lower().startswith("0x") else 10)


def enum_members(body):
    for member in body.split(","):
        member = member.strip()
        if member:
            match = re.fullmatch(r"(\w+)(?:\s*=\s*(.+))?", member, re.S)
            if not match:
                raise ValueError(f"unparseable enum member: {member!r}")
            yield match[1], match[2]


def enum_values(text, name, defines):
    match = re.search(r"\benum\s+" + re.escape(name) + r"\s*\{([^}]+)\}", strip_c(text), re.S)
    if not match:
        raise ValueError(f"missing enum {name}")
    values, previous = {}, -1
    for member, expression in enum_members(match[1]):
        if member in values:
            raise ValueError(f"duplicate enum member: {member}")
        previous = number(expression, {**defines, **{k: str(v) for k, v in values.items()}}) if expression else previous + 1
        values[member] = previous
    return values


def official_table(text, name, defines):
    joined = strip_c(text).replace("\\\n", " ")
    match = re.search(r"^#define\s+" + name + r"\(X\)\s+([^\n]+)", joined, re.M)
    if not match:
        raise ValueError(f"missing generated table {name}")
    rows = re.findall(r"\bX\((\w+),\s*([^)]+)\)", match[1])
    if not rows or len({n for n, _ in rows}) != len(rows):
        raise ValueError(f"empty or duplicate generated table {name}")
    return {n: number(v, defines) for n, v in rows}


def alias_findings(text, generated_names):
    text = strip_c(text)
    declarations = list(wire_defines(text).items())
    for body in re.findall(r"\benum(?:\s+\w+)?\s*\{([^}]+)\}", text, re.S):
        # Some unrelated QMK enums contain preprocessor includes. Only examine
        # declared members of the owned namespaces, without interpreting them.
        for member in body.split(","):
            match = re.fullmatch(r"\s*(\w+)(?:\s*=\s*(.+?))?\s*", member, re.S)
            if match:
                declarations.append((match[1], match[2]))
    errors = []
    for name, expression in declarations:
        if name in generated_names:
            errors.append(f"{name}: redefines a generated registry name")
        elif OWNED.fullmatch(name):
            if expression is None:
                errors.append(f"{name}: implicit number; use the generated wire macro")
            else:
                without_numbers = re.sub(r"\b(?:0[xX][0-9a-fA-F]+|\d+)[uUlL]*\b", "", expression)
                if not re.search(r"\b[A-Za-z_]\w*\b", without_numbers):
                    errors.append(f"{name}: literal redefinition; use the generated wire macro")
    return errors


def check_all(tree):
    errors = []
    try:
        header = tree.read(HEADER)
        generated = wire_defines(header)
        via, keycodes = tree.read(VIA_H), tree.read(KEYCODES_H)
        defines = {**generated, **wire_defines(via), **wire_defines(keycodes)}
        for enum, table in TABLES.items():
            actual = enum_values(via, enum, defines)
            expected = official_table(header, table, generated)
            if actual != expected:
                errors.append(f"{VIA_H}: {enum} name/value set differs: actual={actual!r}, registry={expected!r}")
        for alias, canonical in (
            ("VIA_PROTOCOL_VERSION", "ERA_WIRE_VIA_PROTOCOL_VERSION"),
            ("QMK_KEYCODES_VERSION_BCD", "ERA_WIRE_VIA_KEYCODES_VERSION"),
        ):
            if alias not in defines or number(alias, defines) != number(canonical, generated):
                errors.append(f"{alias} differs from {canonical}")
        sources = tree.sources()
        if not sources:
            errors.append("no ERA sources found; refusing an empty scan")
        for rel in sources:
            errors.extend(f"{rel}: {finding}" for finding in alias_findings(tree.read(rel), set(generated)))
    except (OSError, ValueError) as exc:
        errors.append(str(exc))
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", action="store_true")
    args = parser.parse_args()
    try:
        errors = check_all(Tree(index=args.index))
    except (OSError, ValueError) as exc:
        errors = [str(exc)]
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("ERA wire contract: official name/value sets, versions and ERA aliases match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
