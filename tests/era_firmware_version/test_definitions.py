#!/usr/bin/env python3
"""Deterministic inventory proof for ERA firmware-local VIA V3 definitions."""

from __future__ import annotations

import json
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
        rgb = named_submenu(definition, "Lighting", "RGBLight")
        indicator = named_submenu(definition, "Lighting", "INDICATOR")

        effect = next(
            node
            for node in rgb.get("content", [])
            if isinstance(node, dict) and node.get("content") == ["id_qmk_rgblight_effect", 2, 2]
        )
        self.assertNotIn("All Off", json.dumps(effect.get("options")))
        self.assertNotIn(0, [option[1] for option in effect.get("options", [])])

        expected = [
            ("Indicator-Only", ["id_qmk_custom_riley_indicator_only", 0, 22]),
            ("IND1 Mode", ["id_qmk_custom_riley_ind1_mode", 0, 13]),
            ("IND1 Indicator Brightness", ["id_qmk_custom_riley_ind1_brightness", 0, 14]),
            ("IND1 Indicator Color", ["id_qmk_custom_riley_ind1_color", 0, 15]),
            ("IND2 Mode", ["id_qmk_custom_riley_ind2_mode", 0, 16]),
            ("IND2 Indicator Brightness", ["id_qmk_custom_riley_ind2_brightness", 0, 17]),
            ("IND2 Indicator Color", ["id_qmk_custom_riley_ind2_color", 0, 18]),
            ("IND3 Mode", ["id_qmk_custom_riley_ind3_mode", 0, 19]),
            ("IND3 Indicator Brightness", ["id_qmk_custom_riley_ind3_brightness", 0, 20]),
            ("IND3 Indicator Color", ["id_qmk_custom_riley_ind3_color", 0, 21]),
        ]
        self.assertEqual(
            [(node.get("label"), node.get("content")) for node in indicator.get("content", [])],
            expected,
        )

        for slot in (1, 2, 3):
            mode = next(node for node in indicator["content"] if node.get("label") == f"IND{slot} Mode")
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

                if board in SPLIT_BOARDS:
                    self.assertEqual(len(timeouts), 1, path.as_posix())
                    self.assertEqual(timeouts[0].get("showIf"), "{id_qmk_rgb_sleep_enable} == 1", path.as_posix())
                else:
                    self.assertEqual(timeouts, [], path.as_posix())

        self.assertEqual(actual_toggle_boards, expected_rgb_boards)

    def test_fixed_socd_and_kkuk_modes_stay_hidden_and_control_order_is_shared(self) -> None:
        fixed_mode_commands = {
            "id_qmk_socd_lr_mode",
            "id_qmk_socd_ud_mode",
            "id_qmk_kkuk_mode",
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
            indicator = named_menu(load_json(path), "INDICATOR")
            self.assertEqual(len(indicator.get("content", [])), 1, path.as_posix())
            wrapper = indicator["content"][0]
            self.assertEqual(wrapper.get("label"), "Lock Indicators", path.as_posix())
            self.assertNotIn("type", wrapper, path.as_posix())
            controls = wrapper.get("content")
            self.assertIsInstance(controls, list, path.as_posix())
            self.assertEqual([control.get("content") for control in controls], expected_commands, path.as_posix())
            self.assertTrue(all(isinstance(control.get("type"), str) for control in controls), path.as_posix())


if __name__ == "__main__":
    unittest.main()
