#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check firmware-local STOCK VIA identity, geometry and lighting exposure.

No QMK installation is needed. --index reads only staged blobs, including the
board configuration and exception evidence, never the unstaged working tree.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

REPO = Path(__file__).resolve().parents[4]
ENGINES = ("backlight", "rgblight", "rgb_matrix", "led_matrix")
MODULES = {
    frozenset(): None,
    frozenset(("backlight",)): "qmk_backlight_keycodes",
    frozenset(("rgblight",)): "qmk_rgblight_keycodes",
    frozenset(("backlight", "rgblight")): "qmk_backlight_rgblight_keycodes",
    frozenset(("rgb_matrix",)): "qmk_rgb_matrix_keycodes",
}
# Single declaration of indicator-only engines. The first three PWM rails are
# indicator supplies (era_backlight_lock.h / era_backlight_lock_process_record
# in era_backlight_lock.c), selected by each cited post_rules.mk. BRICK65S is
# different: its entire RGB Matrix, not a backlight, is two dedicated indicators.
INDICATOR_ONLY = {
    "era/sirind/klein_sd": (
        "backlight", "keyboards/era/sirind/klein_sd/post_rules.mk",
        "ERA_BACKLIGHT_LOCK_ENABLE = yes",
    ),
    "era/sirind/divine": (
        "backlight", "keyboards/era/sirind/divine/post_rules.mk",
        "ERA_BACKLIGHT_LOCK_ENABLE = yes",
    ),
    "era/sirind/klein_hs": (
        "backlight", "keyboards/era/sirind/klein_hs/post_rules.mk",
        "ERA_BACKLIGHT_LOCK_ENABLE = yes",
    ),
    "era/sirind/brick65s": (
        "rgb_matrix", "keyboards/era/sirind/brick65s/config.h",
        "#define ERA_RGB_INDICATOR_2_LED 1",
    ),
}
NUMERIC_MACROS = {
    "VENDOR_ID", "PRODUCT_ID", "MATRIX_ROWS", "MATRIX_COLS",
    "ERA_SPLIT_USB_IDENTITY_PID_LEFT", "ERA_SPLIT_USB_IDENTITY_PID_RIGHT",
}


class Tree:
    def __init__(self, root=REPO, index=False):
        self.root, self.index = Path(root), index
        self.names = set(self.git("ls-files", "--cached").splitlines()) if index else None
        self.cache = {}

    def git(self, *args):
        proc = subprocess.run(["git", *args], cwd=self.root, capture_output=True)
        if proc.returncode:
            raise ValueError(f"git {' '.join(args)} failed: {proc.stderr.decode('utf-8', errors='replace')}")
        return proc.stdout.decode("utf-8")

    def exists(self, rel):
        return rel in self.names if self.index else (self.root / rel).is_file()

    def read(self, rel):
        if rel not in self.cache:
            self.cache[rel] = self.git("show", f":{rel}") if self.index else (self.root / rel).read_text(encoding="utf-8")
        return self.cache[rel]

    def definitions(self):
        if self.index:
            return sorted(p for p in self.names if is_stock(p))
        # Shared maker definitions live beside keyboard.json, not a keymap.
        return sorted({
            p.relative_to(self.root).as_posix()
            for pattern in ("keyboards/era/**/keymaps/via/*.json", "keyboards/era/shared/**/*-VIA.json")
            for p in self.root.glob(pattern)
        })


def is_stock(rel):
    path = PurePosixPath(rel)
    return rel.startswith("keyboards/era/") and (
        path.parent.parts[-2:] == ("keymaps", "via") and path.suffix == ".json"
        or rel.startswith("keyboards/era/shared/") and path.name.endswith("-VIA.json")
    )


def integer(value):
    if type(value) is int:
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"not an integer USB id: {value!r}")


def load_json(text):
    def invalid(value):
        raise ValueError(f"invalid JSON constant {value}")
    value = json.loads(text, parse_constant=invalid)
    if not isinstance(value, dict):
        raise ValueError("JSON root must be an object")
    return value


def merge(target, source):
    for key, value in source.items():
        if isinstance(value, dict) and isinstance(target.get(key), dict):
            merge(target[key], value)
        else:
            target[key] = value


def defines(text, wanted):
    """Accept literal board declarations; refuse conditional/expressive facts."""
    text = re.sub(r"/\*[\s\S]*?\*/|//[^\n]*", "", text)
    found, depth = {}, 0
    for line in text.splitlines():
        directive = re.match(r"\s*#\s*(\w+)\b(.*)", line)
        if not directive:
            continue
        op, rest = directive.groups()
        if op in ("if", "ifdef", "ifndef"):
            depth += 1
        elif op == "endif":
            depth -= 1
        elif op in ("define", "undef"):
            parts = rest.split(None, 1)
            if parts and parts[0] in wanted:
                if depth:
                    raise ValueError(f"conditional {parts[0]} needs an explicit checker rule")
                if op == "undef":
                    found.pop(parts[0], None)
                else:
                    found[parts[0]] = parts[1].strip() if len(parts) > 1 else ""
    return found


def lighting_module(board, features):
    engines = frozenset(features)
    if engines not in MODULES:
        raise ValueError(f"{board}: unsupported lighting feature combination: {', '.join(sorted(engines))}")
    return MODULES[engines]


def board_facts(tree, rel):
    path = PurePosixPath(rel)
    folder = path.parent.parent.parent if path.parent.name == "via" else path.parent
    board = folder.relative_to("keyboards").as_posix()
    folders = []
    while folder.as_posix() != "keyboards":
        folders.append(folder.as_posix())
        folder = folder.parent
    info = {}
    # qmk/info.py: find_info_json / merge_info_jsons: deepest first,
    # parent data wins conflicts.
    for folder in folders:
        for name in ("info.json", "keyboard.json"):
            candidate = f"{folder}/{name}"
            if tree.exists(candidate):
                merge(info, load_json(tree.read(candidate)))
    macros = {}
    features = {engine for engine in ENGINES if info.get("features", {}).get(engine, False)}
    config_folders = list(reversed(folders))
    if path.parent.name == "via":
        config_folders.append(path.parent.as_posix())
    for folder in config_folders:
        config = f"{folder}/config.h"
        if tree.exists(config):
            macros.update(defines(tree.read(config), NUMERIC_MACROS))
        for name in ("rules.mk", "post_rules.mk"):
            rules = f"{folder}/{name}"
            if not tree.exists(rules):
                continue
            for engine, value in re.findall(
                r"^\s*(BACKLIGHT|RGBLIGHT|RGB_MATRIX|LED_MATRIX)_ENABLE\s*[:?+]?=\s*([^\n#]+)",
                tree.read(rules), re.M,
            ):
                engine = engine.lower()
                value = value.strip()
                if value not in ("yes", "no"):
                    raise ValueError(f"{board}: non-literal {engine} in {rules}")
                features.discard(engine)
                if value == "yes":
                    features.add(engine)
    if board in INDICATOR_ONLY:
        engine, evidence, marker = INDICATOR_ONLY[board]
        if engine not in features or marker not in tree.read(evidence):
            raise ValueError(f"{board}: indicator-only evidence changed: {evidence}")
        if engine == "rgb_matrix":
            leds = info["rgb_matrix"]["layout"]
            if len(leds) != 2 or any(led.get("flags") != 8 for led in leds):
                raise ValueError(f"{board}: indicator-only RGB Matrix geometry changed")
        else:
            header = tree.read("keyboards/era/common/features/era_backlight_lock.h")
            if "indicator supply rather than an independently switchable lamp" not in header:
                raise ValueError(f"{board}: indicator supply policy evidence changed")
        features.remove(engine)
    usb = info["usb"]
    vid = integer(macros.get("VENDOR_ID", usb["vid"]))
    pid = integer(macros.get("PRODUCT_ID", usb["pid"]))
    split = info.get("split", {}).get("enabled", False)
    if split:
        side = re.search(r"-([LR])-VIA\.json$", path.name)
        if not side:
            raise ValueError(f"{board}: split definition must identify -L- or -R-")
        # era_split_usb_identity.c, era_split_usb_identity_init: ProductID is
        # selected from these board config.h macros, not guessed from JSON.
        name = "ERA_SPLIT_USB_IDENTITY_PID_" + ("LEFT" if side[1] == "L" else "RIGHT")
        pid = integer(macros[name])
    # qmk/info.py: _matrix_size doubles pin-derived rows only; explicit
    # matrix_size / MATRIX_ROWS already describes the entire split.
    if "matrix_size" in info:
        matrix = dict(info["matrix_size"])
    else:
        pins = info["matrix_pins"]
        if "direct" in pins:
            matrix = {"rows": len(pins["direct"]), "cols": len(pins["direct"][0])}
        else:
            matrix = {"rows": len(pins["rows"]), "cols": len(pins["cols"])}
        if split:
            matrix["rows"] *= 2
    for axis in ("rows", "cols"):
        if "MATRIX_" + axis.upper() in macros:
            matrix[axis] = integer(macros["MATRIX_" + axis.upper()])
    return board, features, vid, pid, matrix


def validate(board, definition, features, vid, pid, matrix):
    keycodes = definition.get("keycodes", [])
    if not isinstance(keycodes, list) or any(not isinstance(k, str) for k in keycodes):
        raise ValueError(f"{board}: keycodes must be a string array")
    if "qmk_lighting" in keycodes:
        raise ValueError(f"{board}: forbidden qmk_lighting in keycodes")
    expected = lighting_module(board, features)
    lighting = [k for k in keycodes if re.match(r"qmk_(backlight|rgblight|rgb_matrix|led_matrix).*keycodes$", k)]
    if lighting != ([expected] if expected else []):
        raise ValueError(f"{board}: lighting module {lighting!r}; expected {expected!r}")
    for field, expected_id in (("vendorId", vid), ("productId", pid)):
        if integer(definition[field]) != expected_id:
            raise ValueError(f"{board}: {field} {definition[field]!r}; expected 0x{expected_id:04X}")
    actual = definition["matrix"]
    if any(type(actual.get(axis)) is not int or actual[axis] != matrix[axis] for axis in ("rows", "cols")):
        raise ValueError(f"{board}: matrix {actual!r}; expected {matrix!r}")


def check_all(tree):
    paths, errors = tree.definitions(), []
    if not paths:
        return 0, ["no STOCK VIA definitions found; refusing an empty scan"]
    for rel in paths:
        try:
            board, features, vid, pid, matrix = board_facts(tree, rel)
            validate(board, load_json(tree.read(rel)), features, vid, pid, matrix)
        except (OSError, ValueError, KeyError, TypeError) as exc:
            errors.append(f"{rel}: {exc}")
    return len(paths), errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", action="store_true")
    args = parser.parse_args()
    try:
        count, errors = check_all(Tree(index=args.index))
    except (OSError, ValueError) as exc:
        errors = [str(exc)]
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"STOCK VIA: {count} definitions match firmware lighting, USB and matrix")
    return 0


if __name__ == "__main__":
    sys.exit(main())
