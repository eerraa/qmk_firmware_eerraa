#!/usr/bin/env python3
"""Deterministic inventory proof for ERA firmware-local VIA V3 definitions."""

from __future__ import annotations

import copy
import json
import re
import unittest
from pathlib import Path
from typing import Any, Iterator


ROOT = Path(__file__).resolve().parents[2]
ERA_KEYBOARDS = ROOT / "keyboards" / "era"

VERSION_COMMAND = ["id_qmk_firmware_version", 8, 1]
VERSION_CONTROL = {
    "label": "VERSION",
    "type": "label",
    "content": VERSION_COMMAND,
}
VERSION_SUBMENU = {
    "label": "VERSION",
    "content": [VERSION_CONTROL],
}
BRICK65_BOARD = "sirind/brick65"
SPLIT_BOARDS = ("sirind/tomak", "sirind/tomak79h", "sirind/tomak79s")
# One USB VID per maker from the ERA maker block; the policy is the-via-eerraa
# docs/adr/0004-firmware-distribution.md. The pairs below are the allocation as
# decided: a number is never renumbered or reused, so a change here is a
# compatibility decision, not a fix-up. A split target lists its product number n;
# its halves report 0xA000 + n and 0xB000 + n.
MAKER_VIDS = {
    "ERA": 0x4500, "SR Industry": 0x4501, "LINWORKS": 0x4502, "NEWONE": 0x4503, "KEYNETIX": 0x4504, "CLASSIC.D": 0x4505,
}
USB_IDENTITIES = {
    "comm/era65": ("ERA", 0x0001),
    "comm/7b75": ("ERA", 0x0002),
    "comm/et_tkl": ("ERA", 0x0003),
    "comm/riley": ("ERA", 0x0004),
    "sirind/divine": ("SR Industry", 0x0001),
    "sirind/klein_hs": ("SR Industry", 0x0002),
    "sirind/klein_sd": ("SR Industry", 0x0003),
    "sirind/tomak": ("SR Industry", 0x0004),
    "shared/n8x/sirind": ("SR Industry", 0x0005),
    "shared/n86/sirind": ("SR Industry", 0x0006),
    "shared/n87/sirind": ("SR Industry", 0x0007),
    "sirind/brick65s": ("SR Industry", 0x0008),
    "sirind/tomak79s": ("SR Industry", 0x0009),
    "sirind/tomak79h": ("SR Industry", 0x000A),
    "sirind/chickpad": ("SR Industry", 0x000B),
    "shared/n8x/linworks": ("LINWORKS", 0x0001),
    "shared/n86/linworks": ("LINWORKS", 0x0002),
    "shared/n87/linworks": ("LINWORKS", 0x0003),
    "linworks/fave65s": ("LINWORKS", 0x0004),
    "newone/a1": ("NEWONE", 0x0001),
    "newone/h1": ("NEWONE", 0x0002),
    "newone/odessey60s": ("NEWONE", 0x0003),
    "newone/odessey60h": ("NEWONE", 0x0004),
    "shared/n8x/keynetix": ("KEYNETIX", 0x0001),
    "shared/n86/keynetix": ("KEYNETIX", 0x0002),
    "shared/n87/keynetix": ("KEYNETIX", 0x0003),
    "classicd/classicd_core": ("CLASSIC.D", 0x0001),
    "classicd/classicd_coreless": ("CLASSIC.D", 0x0002),
    "classicd/classicd_a1": ("CLASSIC.D", 0x0003),
    "classicd/classicd_a1_ug": ("CLASSIC.D", 0x0004),
}
# The ATmega32U4 Brick65 is not an ERA-built board and keeps its own identity.
BRICK65_IDENTITY = ("SR industry", 0x5943, 0x0005)
# Retired by the move to maker VIDs; the ERA app still recognises it on old firmware.
LEGACY_ERA_VID = 0x4552
RGB_SLEEP_ENABLE_COMMAND = ["id_qmk_rgb_sleep_enable", 9, 12]
RGB_SLEEP_TIMEOUT_COMMAND = ["id_qmk_rgb_sleep_timeout", 9, 10]
BACKLIGHT_SLEEP_ENABLE_COMMAND = ["id_qmk_backlight_sleep_enable", 9, 13]
BACKLIGHT_SLEEP_TIMEOUT_COMMAND = ["id_qmk_backlight_sleep_timeout", 9, 14]
SLEEP_PRESETS = [["1 min", 1], ["3 min", 3], ["5 min", 5], ["10 min", 10], ["30 min", 30], ["60 min", 60]]
RGB_MATRIX_EFFECT_COMMAND = ["id_qmk_rgb_matrix_effect", 3, 2]
# Lighting submenus are named for where the LEDs are; these engine and legacy names must not return.
LEGACY_LIGHTING_LABELS = {
    "INDICATOR", "RGBLight", "RGB Matrix", "RGB Badge", "Badge Lighting", "Lock Indicators", "Lock Indicator",
}
BUILTIN_LIGHTING_MENUS = {"qmk_rgb_matrix", "qmk_rgblight", "qmk_backlight", "qmk_backlight_rgblight"}
# Value 0 of a lock-indicator dropdown: the LED shows the RGB effect when it is shared, or goes dark when dedicated.
SHARED_INDICATOR_BOARDS = {
    "comm/riley", "linworks/fave65s", "shared/n86", "shared/n87", "sirind/tomak", "sirind/tomak79h", "sirind/tomak79s",
}
DEDICATED_INDICATOR_BOARDS = {"newone/odessey60h", "newone/odessey60s", "sirind/brick65s"}

INDICATOR_COMMANDS = {
    "shared/n86": [
        ["id_qmk_custom_ind_enable", 0, 6],
        ["id_qmk_custom_ind_1_select", 0, 7],
        ["id_qmk_custom_ind_1_brightness", 0, 8],
        ["id_qmk_custom_ind_1_color", 0, 9],
        ["id_qmk_custom_ind_2_select", 0, 10],
        ["id_qmk_custom_ind_2_brightness", 0, 11],
        ["id_qmk_custom_ind_2_color", 0, 12],
    ],
    "shared/n87": [
        ["id_qmk_custom_ind_enable", 0, 6],
        ["id_qmk_custom_ind_1_select", 0, 7],
        ["id_qmk_custom_ind_1_brightness", 0, 8],
        ["id_qmk_custom_ind_1_color", 0, 9],
        ["id_qmk_custom_ind_2_select", 0, 10],
        ["id_qmk_custom_ind_2_brightness", 0, 11],
        ["id_qmk_custom_ind_2_color", 0, 12],
    ],
    "sirind/brick65s": [
        ["id_qmk_custom_ind_1_select", 0, 7],
        ["id_qmk_custom_ind_1_brightness", 0, 8],
        ["id_qmk_custom_ind_1_color", 0, 9],
        ["id_qmk_custom_ind_2_select", 0, 10],
        ["id_qmk_custom_ind_2_brightness", 0, 11],
        ["id_qmk_custom_ind_2_color", 0, 12],
    ],
}


def load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as source:
        value = json.load(source)
    if not isinstance(value, dict):
        raise AssertionError(f"{path}: top-level JSON value is not an object")
    return value


def deep_update(target: dict[str, Any], update: dict[str, Any]) -> dict[str, Any]:
    for key, value in update.items():
        target[key] = deep_update(target.get(key, {}), value) if isinstance(value, dict) else value
    return target


def board_metadata(keyboard_json: Path) -> dict[str, Any]:
    """What QMK builds: every parent info.json merged over the keyboard.json, as lib/python/qmk/info.py does."""
    merged: dict[str, Any] = {}
    chain = [keyboard_json]
    for parent in keyboard_json.parent.parents:
        if parent == ERA_KEYBOARDS.parent:
            break
        chain.append(parent / "info.json")
    for path in chain:
        if path.is_file():
            deep_update(merged, load_json(path))
    return merged


def stock_definitions(board_dir: Path) -> list[Path]:
    """A target's official VIA JSONs sit beside its VIA keymap, or, for a maker target of a
    shared board body (whose keymaps are the body's), beside that target's keyboard.json."""
    return sorted([*(board_dir / "keymaps" / "via").glob("*-VIA.json"), *board_dir.glob("*-VIA.json")])


def board_family(board: str) -> str:
    """Every maker target of a shared board body answers to that body: shared/n86/<maker> -> shared/n86."""
    parts = board.split("/")
    return "/".join(parts[:2]) if parts[0] == "shared" else board


def walk_dicts(value: Any) -> Iterator[dict[str, Any]]:
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from walk_dicts(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk_dicts(child)


def firmware_rgb_matrix_effects(metadata: dict[str, Any]) -> list[str]:
    """RGB Matrix mode numbers in firmware order: All Off, Solid Color, then enabled animations."""
    animations = ROOT / "quantum" / "rgb_matrix" / "animations"
    order: list[str] = []
    for header in re.findall(r'#include "(\w+\.h)"', (animations / "rgb_matrix_effects.inc").read_text(encoding="utf-8")):
        order += [name.lower() for name in re.findall(r"RGB_MATRIX_EFFECT\((\w+)\)", (animations / header).read_text(encoding="utf-8"))]
    enabled = {name for name, on in metadata.get("rgb_matrix", {}).get("animations", {}).items() if on} | {"solid_color"}
    return ["all_off"] + [name for name in order if name in enabled]


def effect_key(name: str) -> str:
    key = re.sub(r"[^a-z]", "", name.lower())
    return key.replace("bandpinwheel", "pinwheel").replace("bandspiral", "spiral")


def named_menu(definition: dict[str, Any], label: str) -> dict[str, Any]:
    matches = [entry for entry in definition.get("menus", []) if isinstance(entry, dict) and entry.get("label") == label]
    if len(matches) != 1:
        raise AssertionError(f"{definition.get('name', '<unnamed>')}: expected one {label} menu, found {len(matches)}")
    return matches[0]


def named_submenu(definition: dict[str, Any], menu_label: str, submenu_label: str) -> dict[str, Any]:
    menu = named_menu(definition, menu_label)
    matches = [
        entry
        for entry in menu.get("content", [])
        if isinstance(entry, dict) and entry.get("label") == submenu_label
    ]
    if len(matches) != 1:
        raise AssertionError(
            f"{definition.get('name', '<unnamed>')}: expected one {menu_label}/{submenu_label} submenu, found {len(matches)}"
        )
    return matches[0]


def visible_td_controls(node: dict[str, Any], command: str, mode: int) -> list[dict[str, Any]]:
    """Evaluate the official slot's small showIf grammar for a returned mode byte.

    Only equality/inequality, integer constants and boolean conjunctions are
    admitted here. This checks inherited group visibility, not just the presence
    of a showIf string on whichever node a definition happens to use.
    """
    expression = node.get("showIf", "1")
    tokens = re.sub(r"\{[a-zA-Z0-9_]+\}|\d+|==|!=|&&|\|\||\s+", "", expression)
    assert not tokens, f"unsupported Tap Dance condition: {expression}"
    expression = re.sub(r"\{([a-zA-Z0-9_]+)\}",
                        lambda match: repr(mode if match[1] == command else None), expression)
    if not eval(expression.replace("&&", " and ").replace("||", " or "), {"__builtins__": {}}, {}):
        return []
    if "type" in node:
        return [node]
    return [control for child in node.get("content", []) if isinstance(child, dict)
            for control in visible_td_controls(child, command, mode)]


def validate_stock_tapdance(definition: dict[str, Any]) -> None:
    """Stock VIA edits only confirmed Legacy slots; advanced slots remain intact."""
    all_controls = [node for node in walk_dicts(definition) if "type" in node]
    for control in all_controls:
        content = control.get("content", [])
        if len(content) == 3 and content[1] == 0 and isinstance(content[2], int):
            assert not 88 <= content[2] <= 103, "advanced control must not be declared, even hidden"

    for slot in range(8):
        pane = named_submenu(definition, "TAPDANCE", f"TD{slot}")
        menu = dict(named_menu(definition, "TAPDANCE"), content=[pane])
        command = f"id_qmk_tapdance_{slot + 1}_mode"
        readers = [node for node in all_controls if node.get("content", [None])[0] == command]
        assert len(readers) == 1, "each slot needs one mode GET"
        reader = readers[0]
        assert reader.get("type") == "label" and reader.get("showIf") == "0", "mode GET must be hidden and read-only"
        assert reader["content"] == [command, 0, 80 + slot], "mode GET address must match its slot"
        expected = [[f"id_qmk_tapdance_{slot + 1}_{suffix}", 0, 32 + slot * 5 + offset]
                    for offset, suffix in enumerate(("tap", "hold", "dtap", "thold", "term"))]
        for mode in range(256):
            visible = visible_td_controls(menu, command, mode)
            editors = [node for node in visible if node["type"] != "label"]
            assert [node["content"] for node in editors] == (expected if mode == 0 else []), \
                f"mode {mode}: only Legacy may expose the five basic editors"
            if mode == 0:
                assert [node["type"] for node in editors] == ["keycode"] * 4 + ["dropdown"], "Legacy editor types"
                assert editors[-1]["options"] == [[f"{ms} ms", ms // 10] for ms in range(100, 501, 20)], "Legacy Term presets"
            labels = [node["content"] for node in visible if node["type"] == "label"]
            assert ["Visit usekb.cc"] in labels, "Custom-app guidance must always be visible"
            states = [node["content"] for node in visible if node.get("label") == "Input mode"]
            wanted = [] if mode == 0 else [[{1: "After decision", 2: "On press"}.get(mode, "Unavailable")]]
            assert states == wanted, f"mode {mode}: advanced/unknown status must be visible"


class EraFirmwareVersionDefinitions(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.board_jsons = sorted(ERA_KEYBOARDS.glob("**/keyboard.json"))
        cls.board_definitions: dict[str, list[Path]] = {}
        cls.board_metadata: dict[str, dict[str, Any]] = {}

        for keyboard_json in cls.board_jsons:
            board_dir = keyboard_json.parent
            board = board_dir.relative_to(ERA_KEYBOARDS).as_posix()
            cls.board_metadata[board] = board_metadata(keyboard_json)
            cls.board_definitions[board] = stock_definitions(board_dir)

    def test_inventory_count_and_brick65_exclusion(self) -> None:
        self.assertEqual(len(self.board_jsons), 31)
        self.assertEqual(len(self.board_definitions), 31)
        self.assertEqual(sum(len(paths) for paths in self.board_definitions.values()), 34)

        rp2040_boards = [board for board, metadata in self.board_metadata.items() if metadata.get("processor") == "RP2040"]
        self.assertEqual(len(rp2040_boards), 30)
        self.assertEqual(set(self.board_metadata) - set(rp2040_boards), {BRICK65_BOARD})
        self.assertEqual(sum(len(self.board_definitions[board]) for board in rp2040_boards), 33)

        for board, paths in self.board_definitions.items():
            split = bool(self.board_metadata[board].get("split", {}).get("enabled"))
            self.assertEqual(len(paths), 2 if split else 1, board)

    def test_official_tapdance_is_legacy_only_on_every_rp2040_definition(self) -> None:
        checked = 0
        for board, paths in self.board_definitions.items():
            if board == BRICK65_BOARD:
                continue
            for path in paths:
                with self.subTest(path=path.relative_to(ROOT).as_posix()):
                    validate_stock_tapdance(load_json(path))
                    checked += 1
        self.assertEqual(checked, 33)

    def test_official_tapdance_guard_rejects_broken_fixtures(self) -> None:
        original = load_json(self.board_definitions["sirind/tomak"][0])
        validate_stock_tapdance(original)

        def slot(definition):
            return named_submenu(definition, "TAPDANCE", "TD0")["content"]

        def mode_setter(definition):
            slot(definition)[0].update(type="dropdown", options=[["Legacy", 0], ["After decision", 1]])

        def wrong_mode_address(definition):
            slot(definition)[0]["content"][2] = 81

        def unguarded_legacy(definition):
            del slot(definition)[1]["showIf"]

        def advanced_timing(definition):
            slot(definition).append({"label": "Hold time", "type": "range", "showIf": "0",
                                     "content": ["id_qmk_tapdance_1_hold_term", 0, 88], "options": [0, 65535]})

        def missing_unknown_status(definition):
            slot(definition)[:] = [node for node in slot(definition) if node.get("content") != ["Unavailable"]]

        def missing_guidance(definition):
            slot(definition)[:] = [node for node in slot(definition) if node.get("content") != ["Visit usekb.cc"]]

        def missing_mode_get(definition):
            del slot(definition)[0]

        def hidden_parent_menu(definition):
            named_menu(definition, "TAPDANCE")["showIf"] = "0"

        def unknown_mode_editable(definition):
            slot(definition)[1]["showIf"] += " || {id_qmk_tapdance_1_mode} == 128"

        for mutate in (mode_setter, wrong_mode_address, unguarded_legacy, advanced_timing,
                       missing_unknown_status, missing_guidance, missing_mode_get,
                       hidden_parent_menu, unknown_mode_editable):
            broken = copy.deepcopy(original)
            mutate(broken)
            with self.subTest(mutation=mutate.__name__), self.assertRaises(AssertionError):
                validate_stock_tapdance(broken)

    def test_exact_version_mapping_is_first_system_submenu_on_all_rp2040_definitions(self) -> None:
        checked = 0
        for board, paths in self.board_definitions.items():
            for path in paths:
                definition = load_json(path)
                matching_controls = [node for node in walk_dicts(definition) if node.get("content") == VERSION_COMMAND]

                if board == BRICK65_BOARD:
                    self.assertEqual(matching_controls, [], path.as_posix())
                    continue

                system = named_menu(definition, "SYSTEM")
                self.assertIsInstance(system.get("content"), list, path.as_posix())
                self.assertGreater(len(system["content"]), 0, path.as_posix())
                self.assertEqual(system["content"][0], VERSION_SUBMENU, path.as_posix())
                self.assertEqual(matching_controls, [VERSION_CONTROL], path.as_posix())
                checked += 1

        self.assertEqual(checked, 33)

    def test_layout_option_inventory_and_choice_tags(self) -> None:
        # A valid VIA definition can silently lose every optional layout label.
        # Keep the physical-layout inventory independent of the JSON being checked.
        expected = {
            "classicd/classicd_a1": [2, 2, 2, 2, 2],
            "classicd/classicd_a1_ug": [2, 2, 2, 2, 2],
            "classicd/classicd_core": [2, 2, 2, 2, 2],
            "classicd/classicd_coreless": [2, 2, 2, 2, 2],
            "comm/7b75": [2, 2, 2, 2],
            "comm/era65": [2, 2, 4],
            "comm/et_tkl": [2, 2, 2, 2],
            "comm/riley": [2, 2, 2, 2, 2],
            "linworks/fave65s": [2, 2, 2, 2],
            "newone/a1": [2, 2, 2, 2, 2],
            "newone/h1": [2, 2],
            "newone/odessey60h": [2, 2, 3],
            "newone/odessey60s": [2, 2, 2, 2, 3],
            "sirind/brick65": [2, 2],
            "sirind/brick65s": [2],
            "shared/n86": [2, 2],
            "shared/n87": [2, 2],
            "shared/n8x": [2, 2, 2, 2, 2],
            "sirind/chickpad": [],
            "sirind/divine": [2, 2],
            "sirind/klein_hs": [2, 3],
            "sirind/klein_sd": [2, 3],
            "sirind/tomak": [2, 2, 2],
            "sirind/tomak79h": [2],
            "sirind/tomak79s": [2, 2, 2],
        }
        self.assertEqual(set(expected), {board_family(board) for board in self.board_definitions})
        for board, paths in self.board_definitions.items():
            counts = expected[board_family(board)]
            for path in paths:
                with self.subTest(path=path.as_posix()):
                    layout = load_json(path)["layouts"]
                    labels = layout.get("labels", [])
                    self.assertEqual([len(label) - 1 if isinstance(label, list) else 2 for label in labels], counts)
                    tags = set()
                    for row in layout["keymap"]:
                        for key in row:
                            if isinstance(key, str):
                                legends = key.split("\n")
                                if len(legends) > 3 and legends[3]:
                                    tags.add(tuple(map(int, legends[3].split(","))))
                    self.assertEqual(tags, {(group, choice) for group, count in enumerate(counts) for choice in range(count)})

    def test_7b75_original_choices_cover_all_firmware_switches(self) -> None:
        [path] = self.board_definitions["comm/7b75"]
        definition = load_json(path)
        self.assertEqual(definition["layouts"]["labels"], [
            ["Backspace", "Unified", "Split"],
            ["Enter", "ANSI", "ISO"],
            ["Left Shift", "ANSI", "ISO"],
            ["Bottom Row", "6U", "6.25U"],
        ])
        expected = {
            (0, 0): ["1,14"], (0, 1): ["1,13", "1,14"],
            (1, 0): ["2,14", "3,14"], (1, 1): ["3,14", "3,12"],
            (2, 0): ["4,0"], (2, 1): ["4,0", "4,1"],
            (3, 0): ["5,0", "5,1", "5,2", "5,6", "5,10", "5,11"],
            (3, 1): ["5,0", "5,1", "5,2", "5,6", "5,10", "5,11"],
        }
        fixed = []
        options: dict[tuple[int, int], list[str]] = {}
        for row in definition["layouts"]["keymap"]:
            for key in row:
                if not isinstance(key, str):
                    continue
                legends = key.split("\n")
                if len(legends) > 3:
                    group = tuple(map(int, legends[3].split(",")))
                    options.setdefault(group, []).append(legends[0])
                else:
                    fixed.append(legends[0])
        self.assertEqual(options, expected)
        self.assertEqual(definition["matrix"], {"rows": 6, "cols": 16})
        firmware = {
            tuple(key["matrix"])
            for key in self.board_metadata["comm/7b75"]["layouts"]["LAYOUT"]["layout"]
        }
        covered = set()
        for bits in range(16):
            choices = [(bits >> group) & 1 for group in range(4)]
            selected = fixed + [key for group, choice in enumerate(choices) for key in options[group, choice]]
            with self.subTest(choices=choices):
                self.assertEqual(len(selected), len(set(selected)))
                self.assertEqual(len(selected), 80 + choices[0] + choices[2])
                coordinates = {tuple(map(int, key.split(","))) for key in selected}
                self.assertLessEqual(coordinates, firmware)
                covered.update(coordinates)
        self.assertEqual(len(firmware), 83)
        self.assertEqual(covered, firmware)

    def test_brick65s_backspace_choices_match_both_firmware_layouts(self) -> None:
        [path] = self.board_definitions["sirind/brick65s"]
        layout = load_json(path)["layouts"]
        self.assertEqual(layout["labels"], [["Backspace", "Unified", "Split"]])
        keys = [key.split("\n") for row in layout["keymap"] for key in row if isinstance(key, str)]
        for choice, name in enumerate(("LAYOUT_ansi", "LAYOUT_ansi_split_bs")):
            selected = [key[0] for key in keys if len(key) == 1 or key[3] == f"0,{choice}"]
            coordinates = {tuple(map(int, key.split(","))) for key in selected}
            expected = {tuple(key["matrix"]) for key in self.board_metadata["sirind/brick65s"]["layouts"][name]["layout"]}
            with self.subTest(layout=name):
                self.assertEqual(len(selected), 65 + choice)
                self.assertEqual(len(selected), len(coordinates))
                self.assertEqual(coordinates, expected)

    def test_riley_layout_choices_cover_all_firmware_switches_without_duplicates(self) -> None:
        [path] = self.board_definitions["comm/riley"]
        definition = load_json(path)
        self.assertEqual(definition["layouts"]["labels"], [
            ["Backspace", "Unified", "Split"],
            ["Enter", "ANSI", "ISO"],
            ["Left Shift", "ANSI", "ISO"],
            ["Right Shift", "Unified", "Split"],
            ["Bottom Row", "7U", "Split"],
        ])
        expected = {
            (0, 0): ["1,13"], (0, 1): ["0,13", "1,13"],
            (1, 0): ["2,13", "3,13"], (1, 1): ["3,13", "2,12"],
            (2, 0): ["3,0"], (2, 1): ["3,0", "3,1"],
            (3, 0): ["3,12"], (3, 1): ["3,12", "4,13"],
            (4, 0): ["4,6"], (4, 1): ["4,4", "4,6", "4,8"],
        }
        fixed = []
        options: dict[tuple[int, int], list[str]] = {}
        for row in definition["layouts"]["keymap"]:
            for key in row:
                if not isinstance(key, str):
                    continue
                legends = key.split("\n")
                if len(legends) > 3:
                    group = tuple(map(int, legends[3].split(",")))
                    options.setdefault(group, []).append(legends[0])
                else:
                    fixed.append(legends[0])
        self.assertEqual(options, expected)
        self.assertEqual(definition["matrix"], {"rows": 5, "cols": 14})
        firmware = {
            tuple(key["matrix"])
            for key in self.board_metadata["comm/riley"]["layouts"]["LAYOUT"]["layout"]
        }
        covered = set()
        for bits in range(32):
            choices = [(bits >> group) & 1 for group in range(5)]
            selected = fixed + [key for group, choice in enumerate(choices) for key in options[group, choice]]
            with self.subTest(choices=choices):
                self.assertEqual(len(selected), len(set(selected)))
                self.assertEqual(len(selected), 58 + choices[0] + choices[2] + choices[3] + 2 * choices[4])
                coordinates = {tuple(map(int, key.split(","))) for key in selected}
                self.assertLessEqual(coordinates, firmware)
                covered.update(coordinates)
        self.assertEqual(len(firmware), 64)
        self.assertEqual(covered, firmware)

    def test_riley_rgb_effect_and_three_lock_slots_match_firmware_ids(self) -> None:
        [path] = self.board_definitions["comm/riley"]
        definition = load_json(path)
        rgb = named_submenu(definition, "Lighting", "RGB LEDs")
        indicator = named_submenu(definition, "Lighting", "Indicators")

        effect = next(
            node
            for node in rgb.get("content", [])
            if isinstance(node, dict) and node.get("content") == ["id_qmk_rgblight_effect", 2, 2]
        )
        self.assertNotIn("All Off", json.dumps(effect.get("options")))
        self.assertNotIn(0, [option[1] for option in effect.get("options", [])])

        expected = [
            ("Indicator-Only", ["id_qmk_custom_riley_indicator_only", 0, 22]),
            ("Indicator 1", ["id_qmk_custom_riley_ind1_mode", 0, 13]),
            ("Indicator 1 Brightness", ["id_qmk_custom_riley_ind1_brightness", 0, 14]),
            ("Indicator 1 Color", ["id_qmk_custom_riley_ind1_color", 0, 15]),
            ("Indicator 2", ["id_qmk_custom_riley_ind2_mode", 0, 16]),
            ("Indicator 2 Brightness", ["id_qmk_custom_riley_ind2_brightness", 0, 17]),
            ("Indicator 2 Color", ["id_qmk_custom_riley_ind2_color", 0, 18]),
            ("Indicator 3", ["id_qmk_custom_riley_ind3_mode", 0, 19]),
            ("Indicator 3 Brightness", ["id_qmk_custom_riley_ind3_brightness", 0, 20]),
            ("Indicator 3 Color", ["id_qmk_custom_riley_ind3_color", 0, 21]),
        ]
        self.assertEqual(
            [(node.get("label"), node.get("content")) for node in indicator.get("content", [])],
            expected,
        )

        for slot in (1, 2, 3):
            mode = next(node for node in indicator["content"] if node.get("label") == f"Indicator {slot}")
            self.assertEqual(
                mode.get("options"),
                [["RGB Effect", 0], ["Caps Lock", 1], ["Scroll Lock", 2], ["Num Lock", 3]],
            )

    def test_split_left_and_right_version_controls_match(self) -> None:
        for board in SPLIT_BOARDS:
            paths = self.board_definitions[board]
            self.assertEqual(len(paths), 2, board)
            submenus = [named_menu(load_json(path), "SYSTEM")["content"][0] for path in paths]
            self.assertEqual(submenus[0], VERSION_SUBMENU, paths[0].as_posix())
            self.assertEqual(submenus[0], submenus[1], board)

    def test_every_rgb_board_enables_qmk_sleep_and_exposes_the_master_toggle(self) -> None:
        actual_toggle_boards: set[str] = set()
        expected_rgb_boards: set[str] = set()
        for board, paths in self.board_definitions.items():
            metadata = self.board_metadata[board]
            features = metadata.get("features", {})
            has_rgb = bool(features.get("rgb_matrix") or features.get("rgblight"))
            if has_rgb:
                expected_rgb_boards.add(board)
                if features.get("rgb_matrix"):
                    self.assertIs(metadata.get("rgb_matrix", {}).get("sleep"), True, f"{board}: RGB Matrix sleep must be enabled")
                if features.get("rgblight"):
                    self.assertIs(metadata.get("rgblight", {}).get("sleep"), True, f"{board}: RGBLight sleep must be enabled")
            for path in paths:
                definition = load_json(path)
                commands = [node.get("content") for node in walk_dicts(definition)]
                toggles = [node for node in walk_dicts(definition) if node.get("content") == RGB_SLEEP_ENABLE_COMMAND]
                timeouts = [node for node in walk_dicts(definition) if node.get("content") == RGB_SLEEP_TIMEOUT_COMMAND]
                if has_rgb:
                    actual_toggle_boards.add(board)
                    self.assertEqual(
                        toggles,
                        [{"label": "RGB Sleep", "type": "toggle", "content": RGB_SLEEP_ENABLE_COMMAND}],
                        path.as_posix(),
                    )
                    sleep = named_submenu(definition, "SYSTEM", "SLEEP")
                    self.assertGreaterEqual(len(sleep.get("content", [])), 1, path.as_posix())
                    self.assertEqual(sleep["content"][0], toggles[0], path.as_posix())
                else:
                    self.assertEqual(toggles, [], f"{path.as_posix()}: RGB SLEEP exposed without RGB hardware feature")
                    self.assertEqual(timeouts, [], f"{path.as_posix()}: RGB SLEEP timeout exposed without RGB hardware feature")

                if has_rgb and board != BRICK65_BOARD:
                    self.assertEqual(
                        timeouts,
                        [
                            {
                                "label": "RGB Sleep Timeout",
                                "type": "dropdown",
                                "showIf": "{id_qmk_rgb_sleep_enable} == 1",
                                "options": SLEEP_PRESETS,
                                "content": RGB_SLEEP_TIMEOUT_COMMAND,
                            }
                        ],
                        path.as_posix(),
                    )
                    self.assertEqual(sleep["content"][1], timeouts[0], path.as_posix())
                else:
                    self.assertEqual(timeouts, [], path.as_posix())

        self.assertEqual(actual_toggle_boards, expected_rgb_boards)

    def test_every_backlight_board_exposes_backlight_sleep(self) -> None:
        for board, paths in self.board_definitions.items():
            has_backlight = bool(self.board_metadata[board].get("features", {}).get("backlight")) and board != BRICK65_BOARD
            for path in paths:
                definition = load_json(path)
                toggles = [node for node in walk_dicts(definition) if node.get("content") == BACKLIGHT_SLEEP_ENABLE_COMMAND]
                timeouts = [node for node in walk_dicts(definition) if node.get("content") == BACKLIGHT_SLEEP_TIMEOUT_COMMAND]
                if not has_backlight:
                    self.assertEqual(toggles + timeouts, [], path.as_posix())
                    continue
                toggle = {"label": "Backlight Sleep", "type": "toggle", "content": BACKLIGHT_SLEEP_ENABLE_COMMAND}
                timeout = {
                    "label": "Backlight Sleep Timeout",
                    "type": "dropdown",
                    "showIf": "{id_qmk_backlight_sleep_enable} == 1",
                    "options": SLEEP_PRESETS,
                    "content": BACKLIGHT_SLEEP_TIMEOUT_COMMAND,
                }
                self.assertEqual(toggles, [toggle], path.as_posix())
                self.assertEqual(timeouts, [timeout], path.as_posix())
                sleep = named_submenu(definition, "SYSTEM", "SLEEP")
                self.assertEqual(sleep["content"][-2:], [toggle, timeout], path.as_posix())

    def test_kkuk_mode_stays_hidden_socd_modes_are_offered_and_control_order_is_shared(self) -> None:
        fixed_mode_commands = {"id_qmk_kkuk_mode"}
        socd_modes = {
            "lr": (10, ["Last Input", "Neutral", "First Input", "Left Priority", "Right Priority"]),
            "ud": (11, ["Last Input", "Neutral", "First Input", "Up Priority", "Down Priority"]),
        }
        for board, paths in self.board_definitions.items():
            if board == BRICK65_BOARD:
                continue
            for path in paths:
                definition = load_json(path)
                commands = {
                    node["content"][0]
                    for node in walk_dicts(definition)
                    if isinstance(node.get("content"), list)
                    and node["content"]
                    and isinstance(node["content"][0], str)
                }
                self.assertTrue(fixed_mode_commands.isdisjoint(commands), path.as_posix())

                socd = named_submenu(definition, "FEATURE", "SOCD")
                self.assertEqual(
                    [control.get("label") for control in socd.get("content", [])],
                    [
                        "Left/Right Enable",
                        "Left/Right Mode",
                        "Left Key",
                        "Right Key",
                        "Up/Down Enable",
                        "Up/Down Mode",
                        "Up Key",
                        "Down Key",
                    ],
                    path.as_posix(),
                )
                for pair, (channel, labels) in socd_modes.items():
                    [mode] = [c for c in socd["content"] if c.get("content", [None])[0] == f"id_qmk_socd_{pair}_mode"]
                    self.assertEqual(mode.get("content"), [f"id_qmk_socd_{pair}_mode", channel, 4], path.as_posix())
                    self.assertEqual(mode.get("type"), "dropdown", path.as_posix())
                    self.assertEqual(mode.get("showIf"), f"{{id_qmk_socd_{pair}_enable}} == 1", path.as_posix())
                    self.assertEqual(mode.get("options"), [[label, value] for value, label in enumerate(labels, 1)], path.as_posix())

                kkuk = named_submenu(definition, "FEATURE", "KKUK")
                self.assertEqual(
                    [control.get("label") for control in kkuk.get("content", [])],
                    ["Enable", "First Delay Time", "Repeat Time"],
                    path.as_posix(),
                )

                tapping = named_submenu(definition, "FEATURE", "TAPPING")
                self.assertEqual(
                    [control.get("label") for control in tapping.get("content", [])],
                    [
                        "Global Tapping Term",
                        "Permissive Hold",
                        "Hold on Other Key Press",
                        "Retro Tapping",
                    ],
                    path.as_posix(),
                )

    def test_indicator_controls_have_the_required_v3_wrapper(self) -> None:
        checked = set()
        for board, paths in self.board_definitions.items():
            expected_commands = INDICATOR_COMMANDS.get(board_family(board))
            if expected_commands is None:
                continue
            checked.add(board_family(board))
            [path] = paths
            wrapper = named_submenu(load_json(path), "Lighting", "Indicators")
            self.assertNotIn("type", wrapper, path.as_posix())
            controls = wrapper.get("content")
            self.assertIsInstance(controls, list, path.as_posix())
            self.assertEqual([control.get("content") for control in controls], expected_commands, path.as_posix())
            self.assertTrue(all(isinstance(control.get("type"), str) for control in controls), path.as_posix())
        self.assertEqual(checked, set(INDICATOR_COMMANDS))

    def test_lighting_menus_use_the_shared_names(self) -> None:
        indicator_label = re.compile(r"Indicator(?: \d)?")
        for board, paths in self.board_definitions.items():
            if board == BRICK65_BOARD:
                continue
            for path in paths:
                definition = load_json(path)
                builtin = {entry for entry in definition.get("menus", []) if isinstance(entry, str)}
                self.assertEqual(builtin & BUILTIN_LIGHTING_MENUS, set(), path.as_posix())
                labels = {node.get("label") for node in walk_dicts(definition)}
                self.assertEqual(labels & LEGACY_LIGHTING_LABELS, set(), path.as_posix())
                selectors = [
                    node
                    for node in walk_dicts(definition)
                    if node.get("type") == "dropdown" and indicator_label.fullmatch(str(node.get("label")))
                ]
                if board_family(board) in SHARED_INDICATOR_BOARDS:
                    zero = "RGB Effect"
                elif board_family(board) in DEDICATED_INDICATOR_BOARDS:
                    zero = "Off"
                else:
                    self.assertEqual(selectors, [], path.as_posix())
                    continue
                self.assertGreater(len(selectors), 0, path.as_posix())
                for selector in selectors:
                    self.assertEqual(
                        selector.get("options"),
                        [[zero, 0], ["Caps Lock", 1], ["Scroll Lock", 2], ["Num Lock", 3]],
                        path.as_posix(),
                    )

    def test_rgb_matrix_effect_lists_follow_the_firmware_effect_order(self) -> None:
        checked = 0
        for board, paths in self.board_definitions.items():
            metadata = self.board_metadata[board]
            if board == BRICK65_BOARD or not metadata.get("features", {}).get("rgb_matrix"):
                continue
            expected = [effect_key(name) for name in firmware_rgb_matrix_effects(metadata)]
            for path in paths:
                effects = [node for node in walk_dicts(load_json(path)) if node.get("content") == RGB_MATRIX_EFFECT_COMMAND]
                if board == "sirind/brick65s":
                    # Its RGB Matrix drives only the two lock indicators; there is no effect to choose.
                    self.assertEqual(effects, [], path.as_posix())
                    continue
                self.assertEqual(len(effects), 1, path.as_posix())
                options = effects[0].get("options", [])
                self.assertEqual([option[1] for option in options], list(range(len(options))), path.as_posix())
                self.assertEqual([effect_key(option[0]) for option in options], expected, path.as_posix())
                checked += 1
        self.assertEqual(checked, 15)

    def test_shared_board_bodies_leave_only_the_identity_to_each_maker_target(self) -> None:
        # QMK merges a parent info.json over the keyboard.json below it and the parent
        # wins a conflicting key, so the two must not share one.
        bodies = sorted(path.parent for path in (ERA_KEYBOARDS / "shared").glob("*/info.json"))
        self.assertGreater(len(bodies), 0)
        for body in bodies:
            self.assertFalse((body / "keyboard.json").exists(), body.as_posix())
            self.assertTrue({"manufacturer", "usb"}.isdisjoint(load_json(body / "info.json")), body.as_posix())
            makers = sorted(path.parent for path in body.glob("*/keyboard.json"))
            self.assertGreater(len(makers), 0, body.as_posix())
            for maker in makers:
                self.assertEqual(set(load_json(maker / "keyboard.json")), {"manufacturer", "usb"}, maker.as_posix())
                entries = sorted(maker.iterdir())
                self.assertTrue(all(entry.is_file() for entry in entries), maker.as_posix())
                self.assertEqual(
                    [entry.name for entry in entries if entry.name != "keyboard.json"],
                    [path.name for path in stock_definitions(maker)],
                    maker.as_posix(),
                )

    def test_no_build_file_reaches_more_than_one_board(self) -> None:
        # QMK applies every parent folder's info.json, config.h, rules.mk, post_rules.mk and
        # <folder>.c/.h to each board below it. Only a shared board body may hold them;
        # anything at a maker folder would also reach that maker's other boards, brick65 included.
        for keyboard_json in self.board_jsons:
            for folder in keyboard_json.parent.parents:
                if folder == ERA_KEYBOARDS.parent:
                    break
                if folder.parent == ERA_KEYBOARDS / "shared":
                    continue
                inherited = {
                    "info.json", "keyboard.json", "config.h", "post_config.h", "rules.mk", "post_rules.mk",
                    f"{folder.name}.c", f"{folder.name}.h",
                }
                with self.subTest(folder=folder.relative_to(ROOT).as_posix()):
                    self.assertEqual({name for name in inherited if (folder / name).exists()}, set())

    def split_half_pids(self, board: str) -> tuple[int, int]:
        config = (ERA_KEYBOARDS / board / "config.h").read_text(encoding="utf-8")
        return tuple(
            int(re.search(rf"^#define ERA_SPLIT_USB_IDENTITY_PID_{side}\s+(0x[0-9A-Fa-f]+)\s*$", config, re.M).group(1), 16)
            for side in ("LEFT", "RIGHT")
        )

    def device_identities(self, board: str) -> list[tuple[int, int]]:
        """What the image enumerates as: one pair, or the left then the right half of a split."""
        usb = self.board_metadata[board]["usb"]
        vid = int(usb["vid"], 16)
        if board in SPLIT_BOARDS:
            return [(vid, pid) for pid in self.split_half_pids(board)]
        return [(vid, int(usb["pid"], 16))]

    def test_every_target_reports_its_makers_allocated_usb_identity(self) -> None:
        self.assertEqual(set(USB_IDENTITIES), set(self.board_metadata) - {BRICK65_BOARD})
        split = {board for board, metadata in self.board_metadata.items() if metadata.get("split", {}).get("enabled")}
        self.assertEqual(split, set(SPLIT_BOARDS))
        for board, (maker, number) in USB_IDENTITIES.items():
            metadata = self.board_metadata[board]
            with self.subTest(board=board):
                self.assertEqual(metadata.get("manufacturer"), maker)
                self.assertEqual(int(metadata["usb"]["vid"], 16), MAKER_VIDS[maker])
                self.assertEqual(int(metadata["usb"]["pid"], 16), number)
                if board in SPLIT_BOARDS:
                    self.assertEqual(self.split_half_pids(board), (0xA000 + number, 0xB000 + number))
        brick65 = self.board_metadata[BRICK65_BOARD]
        self.assertEqual(
            (brick65["manufacturer"], int(brick65["usb"]["vid"], 16), int(brick65["usb"]["pid"], 16)), BRICK65_IDENTITY
        )

    def test_no_usb_identity_repeats_or_returns_to_the_legacy_vid(self) -> None:
        seen: dict[tuple[int, int], str] = {}
        for board in self.board_metadata:
            usb = self.board_metadata[board]["usb"]
            # A split board never enumerates its product number, but the number is spent all the same.
            product = (int(usb["vid"], 16), int(usb["pid"], 16))
            for identity in sorted(set(self.device_identities(board)) | {product}):
                with self.subTest(board=board, identity=f"{identity[0]:04X}:{identity[1]:04X}"):
                    self.assertNotIn(identity, seen)
                    self.assertNotEqual(identity[0], LEGACY_ERA_VID)
                seen[identity] = board

    def test_every_official_definition_carries_its_targets_usb_identity(self) -> None:
        # Stock VIA matches a draft definition to the connected device by this pair.
        for board, paths in self.board_definitions.items():
            identities = self.device_identities(board)
            if board in SPLIT_BOARDS:
                self.assertEqual([path.name.rsplit("-", 2)[-2] for path in paths], ["L", "R"], board)
            self.assertEqual(len(paths), len(identities), board)
            for path, (vid, pid) in zip(paths, identities):
                definition = load_json(path)
                with self.subTest(path=path.relative_to(ROOT).as_posix()):
                    self.assertEqual((int(definition["vendorId"], 16), int(definition["productId"], 16)), (vid, pid))

    def test_maker_copies_of_a_shared_board_differ_only_in_usb_identity(self) -> None:
        families: dict[str, list[str]] = {}
        for board in self.board_definitions:
            if board_family(board) != board:
                families.setdefault(board_family(board), []).append(board)
        self.assertEqual(sorted(families), ["shared/n86", "shared/n87", "shared/n8x"])
        for family, boards in families.items():
            self.assertEqual(sorted(board.rsplit("/", 1)[1] for board in boards), ["keynetix", "linworks", "sirind"], family)
            names = {tuple(path.name for path in self.board_definitions[board]) for board in boards}
            self.assertEqual(len(names), 1, family)
            bodies = []
            for board in boards:
                [path] = self.board_definitions[board]
                definition = load_json(path)
                del definition["vendorId"], definition["productId"]
                bodies.append(definition)
            with self.subTest(family=family):
                self.assertTrue(all(body == bodies[0] for body in bodies))

    def test_every_rp2040_board_exposes_six_dynamic_layers(self) -> None:
        boards = 0
        for path in sorted(ERA_KEYBOARDS.rglob("keyboard.json")):
            info = board_metadata(path)
            if info.get("processor") != "RP2040":
                continue
            boards += 1
            self.assertEqual(info.get("dynamic_keymap", {}).get("layer_count"), 6, path.as_posix())
        self.assertGreater(boards, 0)

    def test_shipped_keymaps_bind_no_diagnostic_keycode(self) -> None:
        # A capture key does nothing in a standard image, so a default keymap that
        # binds one ships a dead key. Captures bind it temporarily in VIA instead.
        diagnostic = re.compile(r"\bWIRE_(?:DIAG|DIAG_2|QWIN)\b")
        for keymap in sorted(ERA_KEYBOARDS.glob("**/keymaps/*/keymap.c")):
            text = keymap.read_text(encoding="utf-8")
            text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
            text = re.sub(r"//[^\n]*", "", text)
            with self.subTest(keymap=keymap.relative_to(ROOT).as_posix()):
                self.assertIsNone(diagnostic.search(text))


if __name__ == "__main__":
    unittest.main()
