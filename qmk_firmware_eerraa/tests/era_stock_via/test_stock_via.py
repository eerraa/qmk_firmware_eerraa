#!/usr/bin/env python3
"""Positive inventory and negative source/definition fixtures for STOCK VIA."""

import importlib.util
import json
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "stock_via", ROOT / "keyboards/era/common/tools/era_stock_via.py",
)
stock = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stock)
BOARD = "era/sirind/tomak"
DEFINITION = "keyboards/era/sirind/tomak/keymaps/via/TOMAK-TKL-L-VIA.json"
CONFIG = "keyboards/era/sirind/tomak/keyboard.json"


class StockViaTests(unittest.TestCase):
    def mutation(self, rel, change):
        tree = stock.Tree()
        original = tree.read
        changed = change(original(rel))
        with patch.object(tree, "read", side_effect=lambda p: changed if p == rel else original(p)):
            _, errors = stock.check_all(tree)
        return "\n".join(errors)

    def definition_change(self, change):
        def edit(text):
            value = json.loads(text)
            change(value)
            return json.dumps(value)
        return self.mutation(DEFINITION, edit)

    def test_current_stock(self):
        count, errors = stock.check_all(stock.Tree())
        self.assertGreater(count, 0)
        self.assertEqual(errors, [])

    def test_forbidden_qmk_lighting(self):
        self.assertIn("forbidden qmk_lighting", self.definition_change(
            lambda d: d["keycodes"].append("qmk_lighting")))

    def test_wrong_lighting_module(self):
        self.assertIn("lighting module", self.definition_change(
            lambda d: d.update(keycodes=["qmk_rgblight_keycodes"])))

    def test_wrong_product_id(self):
        self.assertIn("productId", self.definition_change(
            lambda d: d.update(productId="0x0004")))

    def test_wrong_vendor_id(self):
        self.assertIn("vendorId", self.definition_change(
            lambda d: d.update(vendorId="0x0000")))

    def test_wrong_matrix_size(self):
        for axis in ("rows", "cols"):
            with self.subTest(axis=axis):
                self.assertIn("matrix", self.definition_change(
                    lambda d: d["matrix"].update({axis: d["matrix"][axis] + 1})))

    def test_unknown_feature_combination_names_board(self):
        def edit(text):
            value = json.loads(text)
            value["features"]["backlight"] = True
            return json.dumps(value)
        errors = self.mutation(CONFIG, edit)
        self.assertIn(BOARD + ": unsupported lighting feature combination", errors)

    def test_malformed_json(self):
        self.assertIn(DEFINITION, self.mutation(DEFINITION, lambda _: "{"))

    def test_required_module_mapping(self):
        for features, expected in (
            ((), None), (("backlight",), "qmk_backlight_keycodes"),
            (("rgblight",), "qmk_rgblight_keycodes"),
            (("backlight", "rgblight"), "qmk_backlight_rgblight_keycodes"),
            (("rgb_matrix",), "qmk_rgb_matrix_keycodes"),
        ):
            self.assertEqual(stock.lighting_module("fixture", features), expected)

    def test_split_pid_and_full_matrix_from_sources(self):
        tree = stock.Tree()
        left = stock.board_facts(tree, DEFINITION)
        right = stock.board_facts(tree, DEFINITION.replace("-L-", "-R-"))
        # Board allocation is 0x0004; firmware's config.h gives A004/B004.
        self.assertEqual((left[3], right[3]), (0xA004, 0xB004))
        self.assertEqual(left[4], {"rows": 12, "cols": 11})

    def test_split_config_change_is_not_hidden(self):
        rel = "keyboards/era/sirind/tomak/config.h"
        errors = self.mutation(rel, lambda text: text.replace(
            "ERA_SPLIT_USB_IDENTITY_PID_LEFT 0xA004",
            "ERA_SPLIT_USB_IDENTITY_PID_LEFT 0xA005"))
        self.assertIn("productId", errors)
        self.assertIn("0xA005", errors)

    def test_indicator_evidence_change_refused(self):
        rel = "keyboards/era/sirind/divine/post_rules.mk"
        self.assertIn("indicator-only evidence changed", self.mutation(
            rel, lambda text: text.replace(
                "ERA_BACKLIGHT_LOCK_ENABLE = yes", "ERA_BACKLIGHT_LOCK_ENABLE = no")))


if __name__ == "__main__":
    unittest.main()
