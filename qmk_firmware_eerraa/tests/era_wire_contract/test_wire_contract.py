#!/usr/bin/env python3
"""Positive inventory and intentional negative wire-contract fixtures."""

import importlib.util
import re
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "wire", ROOT / "keyboards/era/common/tools/era_wire_contract.py",
)
wire = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(wire)


class WireContractTests(unittest.TestCase):
    def mutation(self, rel, change):
        tree = wire.Tree()
        original = tree.read
        changed = change(original(rel))
        self.assertNotEqual(changed, original(rel), "fixture did not mutate its input")
        with patch.object(tree, "read", side_effect=lambda p: changed if p == rel else original(p)):
            errors = wire.check_all(tree)
        self.assertTrue(errors)
        return "\n".join(errors)

    def test_current_contract(self):
        self.assertEqual(wire.check_all(wire.Tree()), [])

    def test_wrong_official_value(self):
        errors = self.mutation(wire.VIA_H, lambda text: re.sub(
            r"(id_get_protocol_version\s*=\s*)0x01", r"\g<1>0x02", text))
        self.assertIn("via_command_id name/value set", errors)

    def test_missing_official_name(self):
        self.assertIn("name/value set", self.mutation(wire.VIA_H, lambda text:
            re.sub(r"^\s*id_device_indication\s*=.*\n", "", text, flags=re.M)))

    def test_extra_official_name(self):
        self.assertIn("via_channel_id name/value set", self.mutation(wire.VIA_H, lambda text:
            text.replace("enum via_channel_id {", "enum via_channel_id {\n id_extra = 99,")))

    def test_wrong_versions(self):
        for rel, alias in ((wire.VIA_H, "VIA_PROTOCOL_VERSION"), (wire.KEYCODES_H, "QMK_KEYCODES_VERSION_BCD")):
            with self.subTest(alias=alias):
                self.assertIn(alias, self.mutation(rel, lambda text: re.sub(
                    r"(#define " + alias + r") [^\n]+", r"\1 0", text)))

    def test_literal_alias(self):
        rel = "keyboards/era/common/system/era_state_sync.h"
        self.assertIn("literal redefinition", self.mutation(rel, lambda text:
            text.replace("ERA_STATE_SYNC_KEYBOARD_VALUE ERA_WIRE_STATE_SYNC_SELECTORS_CURRENT",
                         "ERA_STATE_SYNC_KEYBOARD_VALUE (0x80U)")))

    def test_implicit_enum_alias(self):
        rel = "keyboards/era/common/features/era_socd_via.c"
        self.assertIn("implicit number", self.mutation(rel, lambda text: re.sub(
            r"ERA_SOCD_VIA_VALUE_MODE\s*=\s*ERA_WIRE_SOCD_MODE", "ERA_SOCD_VIA_VALUE_MODE", text)))

    def test_generated_name_redefinition(self):
        rel = "keyboards/era/common/system/era_state_sync.h"
        self.assertIn("redefines a generated", self.mutation(rel, lambda text:
            text + "\n#define ERA_WIRE_STATE_SYNC_SELECTORS_CURRENT 0x80U\n"))

    def test_representation_is_not_a_conflict(self):
        tree = wire.Tree()
        original = tree.read
        changed = re.sub(r"(id_get_protocol_version\s*=\s*)0x01", r"\g<1>1", original(wire.VIA_H))
        with patch.object(tree, "read", side_effect=lambda p: changed if p == wire.VIA_H else original(p)):
            self.assertEqual(wire.check_all(tree), [])


if __name__ == "__main__":
    unittest.main()
