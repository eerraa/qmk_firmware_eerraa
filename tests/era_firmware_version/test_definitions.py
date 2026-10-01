#!/usr/bin/env python3
"""Deterministic inventory proof for ERA firmware-local VIA V3 definitions."""

from __future__ import annotations

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
    "comm/riley", "linx3/fave65s", "linx3/n86", "linx3/n87", "sirind/tomak", "sirind/tomak79h", "sirind/tomak79s",
}
DEDICATED_INDICATOR_BOARDS = {"newone/odessey60h", "newone/odessey60s", "sirind/brick65s"}

INDICATOR_COMMANDS = {
    "linx3/n86": [
        ["id_qmk_custom_ind_enable", 0, 6],
        ["id_qmk_custom_ind_1_select", 0, 7],
        ["id_qmk_custom_ind_1_brightness", 0, 8],
        ["id_qmk_custom_ind_1_color", 0, 9],
        ["id_qmk_custom_ind_2_select", 0, 10],
        ["id_qmk_custom_ind_2_brightness", 0, 11],
        ["id_qmk_custom_ind_2_color", 0, 12],
    ],
    "linx3/n87": [
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


class EraFirmwareVersionDefinitions(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.board_jsons = sorted(ERA_KEYBOARDS.glob("**/keyboard.json"))
        cls.board_definitions: dict[str, list[Path]] = {}
        cls.board_metadata: dict[str, dict[str, Any]] = {}

        for keyboard_json in cls.board_jsons:
            board_dir = keyboard_json.parent
            board = board_dir.relative_to(ERA_KEYBOARDS).as_posix()
            cls.board_metadata[board] = load_json(keyboard_json)
            cls.board_definitions[board] = sorted((board_dir / "keymaps" / "via").glob("*-VIA.json"))

    def test_inventory_count_and_brick65_exclusion(self) -> None:
        self.assertEqual(len(self.board_jsons), 25)
        self.assertEqual(len(self.board_definitions), 25)
        self.assertEqual(sum(len(paths) for paths in self.board_definitions.values()), 28)

        rp2040_boards = [board for board, metadata in self.board_metadata.items() if metadata.get("processor") == "RP2040"]
        self.assertEqual(len(rp2040_boards), 24)
        self.assertEqual(set(self.board_metadata) - set(rp2040_boards), {BRICK65_BOARD})
        self.assertEqual(sum(len(self.board_definitions[board]) for board in rp2040_boards), 27)

        for board, paths in self.board_definitions.items():
            split = bool(self.board_metadata[board].get("split", {}).get("enabled"))
            self.assertEqual(len(paths), 2 if split else 1, board)

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

        self.assertEqual(checked, 27)

    def test_layout_option_inventory_and_choice_tags(self) -> None:
        # A valid VIA definition can silently lose every optional layout label.
        # Keep the physical-layout inventory independent of the JSON being checked.
        expected = {
            "comm/7b75": [2, 2, 2, 2],
            "comm/classicd_a1": [2, 2, 2, 2, 2],
            "comm/classicd_a1_ug": [2, 2, 2, 2, 2],
            "comm/classicd_core": [2, 2, 2, 2, 2],
            "comm/classicd_coreless": [2, 2, 2, 2, 2],
            "comm/et_tkl": [2, 2, 2, 2],
            "comm/riley": [2, 2, 2, 2, 2],
            "divine": [2, 2],
            "era65": [2, 2, 4],
            "linx3/fave65s": [2, 2, 2, 2],
            "linx3/n86": [2, 2],
            "linx3/n87": [2, 2],
            "linx3/n8x": [2, 2, 2, 2, 2],
            "newone/a1": [2, 2, 2, 2, 2],
            "newone/h1": [2, 2],
            "newone/odessey60h": [2, 2, 3],
            "newone/odessey60s": [2, 2, 2, 2, 3],
            "sirind/brick65": [2, 2],
            "sirind/brick65s": [2],
            "sirind/chickpad": [],
            "sirind/klein_hs": [2, 3],
            "sirind/klein_sd": [2, 3],
            "sirind/tomak": [2, 2, 2],
            "sirind/tomak79h": [2],
            "sirind/tomak79s": [2, 2, 2],
        }
        self.assertEqual(set(expected), set(self.board_definitions))
        for board, counts in expected.items():
            for path in self.board_definitions[board]:
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
        for board, expected_commands in INDICATOR_COMMANDS.items():
            [path] = self.board_definitions[board]
            wrapper = named_submenu(load_json(path), "Lighting", "Indicators")
            self.assertNotIn("type", wrapper, path.as_posix())
            controls = wrapper.get("content")
            self.assertIsInstance(controls, list, path.as_posix())
            self.assertEqual([control.get("content") for control in controls], expected_commands, path.as_posix())
            self.assertTrue(all(isinstance(control.get("type"), str) for control in controls), path.as_posix())

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
                if board in SHARED_INDICATOR_BOARDS:
                    zero = "RGB Effect"
                elif board in DEDICATED_INDICATOR_BOARDS:
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
        self.assertEqual(checked, 11)

    def test_every_rp2040_board_exposes_six_dynamic_layers(self) -> None:
        boards = 0
        for path in sorted(ERA_KEYBOARDS.rglob("keyboard.json")):
            info = load_json(path)
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
