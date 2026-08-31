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
        self.assertEqual(len(self.board_jsons), 23)
        self.assertEqual(len(self.board_definitions), 23)
        self.assertEqual(sum(len(paths) for paths in self.board_definitions.values()), 26)

        rp2040_boards = [board for board, metadata in self.board_metadata.items() if metadata.get("processor") == "RP2040"]
        self.assertEqual(len(rp2040_boards), 22)
        self.assertEqual(set(self.board_metadata) - set(rp2040_boards), {BRICK65_BOARD})
        self.assertEqual(sum(len(self.board_definitions[board]) for board in rp2040_boards), 25)

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

        self.assertEqual(checked, 25)

    def test_split_left_and_right_version_controls_match(self) -> None:
        for board in SPLIT_BOARDS:
            paths = self.board_definitions[board]
            self.assertEqual(len(paths), 2, board)
            submenus = [named_menu(load_json(path), "SYSTEM")["content"][0] for path in paths]
            self.assertEqual(submenus[0], VERSION_SUBMENU, paths[0].as_posix())
            self.assertEqual(submenus[0], submenus[1], board)

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
