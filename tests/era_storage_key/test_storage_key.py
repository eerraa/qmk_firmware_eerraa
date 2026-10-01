#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""The storage-key check must see a stored-format change and ignore a comment.

Each case plants one edit in an in-memory view of the tree and runs the real
check against the recorded fingerprint; nothing on disk changes.

  python tests/era_storage_key/test_storage_key.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "keyboards/era/common/tools"))
import era_storage_key as storage  # noqa: E402


class PlantedTree(storage.Tree):
    def __init__(self, rel, edit):
        super().__init__(index=False)
        self.rel, self.edit = rel, edit

    def read(self, rel):
        text = super().read(rel)
        if rel == self.rel and text is not None:
            planted = self.edit(text)
            assert planted != text, f"plant did not change {rel}"
            return planted
        return text


def sub(pattern, replacement):
    return lambda text: re.sub(pattern, replacement, text, count=1, flags=re.M)


LAYOUT = storage.LAYOUT
CASES = (
    ("ERA block region resized", LAYOUT,
     sub(r"^(#define ERA_EEPROM_KKUK_CONFIG_SIZE )(\d+)", lambda m: m.group(1) + str(int(m.group(2)) + 1)),
     "stored format differs"),
    ("persisted ERA type reshaped", "keyboards/era/common/features/era_tapdance.c",
     sub(r"(\}\s*era_tapdance_storage_t;)", r"    uint8_t planted;\n\1"), "stored format differs"),
    ("validity marker changed", "keyboards/era/common/features/era_backlight.c",
     sub(r"ERA_BACKLIGHT_CONFIG_VALID = 0x\w+", "ERA_BACKLIGHT_CONFIG_VALID = 0x01"), "stored format differs"),
    ("board keymap geometry changed", "keyboards/era/comm/7b75/config.h",
     lambda text: text + "\n#define DYNAMIC_KEYMAP_LAYER_COUNT 8\n", "stored format differs"),
    ("RGB effect order changed", "quantum/rgblight/rgblight_modes.h",
     lambda text: text.replace("_RGBM_MULTI_DYNAMIC(BREATHING)", "_RGBM_MULTI_DYNAMIC(BREATHING_X)", 1),
     "stored format differs"),
    ("QMK keycode renumbered", "quantum/keycodes.h",
     lambda text: text.replace("KC_NO = 0x0000,", "KC_NO = 0x0001,", 1), "stored format differs"),
    ("shared board body geometry changed", "keyboards/era/shared/n86/info.json",
     lambda text: text.replace('"layer_count": 6', '"layer_count": 8', 1), "stored format differs"),
    ("board keycode enum reordered", "keyboards/era/sirind/common/tomak_common.h",
     lambda text: text.replace("    TD0 = QK_KB_0,", "    TD0 = QK_KB_1,", 1), "stored format differs"),
    ("VIA magic key moved without a record", storage.VIA_MAGIC,
     sub(r'^(#define QMK_BUILDDATE )"[^"]+"', r'\1"2024-04-24-11:29:54"'), "`QMK_BUILDDATE` is"),
    ("VIA magic no longer reaches every target", storage.VIA_MAGIC_RULES,
     lambda text: text.replace("CONFIG_H += keyboards/era/common/storage/era_via_magic.h", "", 1),
     "no longer force-includes"),
    ("ERA reset key moved without a record", LAYOUT,
     sub(r"^(#define ERA_EEPROM_RESET_KEY 0x[0-9A-Fa-f]{7})([0-9A-Fa-f])",
         lambda m: m.group(1) + ("1" if m.group(2) == "0" else "0")), "`ERA_EEPROM_RESET_KEY` is"),
)


def main() -> int:
    baseline = storage.check(storage.Tree(index=False))
    if baseline:
        print("the live tree already fails; fix that first:\n" + "\n".join(baseline))
        return 1
    missed = []
    for name, rel, edit, expected in CASES:
        findings = storage.check(PlantedTree(rel, edit))
        caught = any(expected in line for line in findings)
        print(f"{'ok  ' if caught else 'FAIL'} {name}")
        if not caught:
            missed.append(name)
    comment_only = storage.check(PlantedTree(LAYOUT, lambda text: text.replace(
        "#pragma once", "#pragma once\n/* a planted comment is not a format change */", 1)))
    print(f"{'ok  ' if not comment_only else 'FAIL'} comment-only edit is not a format change")
    if comment_only:
        missed.append("comment-only")
    if missed:
        print(f"FAIL: missed {missed}")
        return 1
    print(f"all {len(CASES) + 1} storage-key cases passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
