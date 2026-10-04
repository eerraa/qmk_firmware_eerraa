"""Regression coverage for active HID format and non-split Velocikey owners."""
from pathlib import Path
import tempfile
import unittest

from feature_persistence_fixture import Fixture


class FeaturePersistence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="era-feature-persistence-")
        cls.fixture = Fixture(Path(cls.temp.name))
        cls.hid = cls.fixture.hid()
        cls.wl = {board: cls.fixture.wl(board) for board in ("odessey", "riley")}

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_failed_nkro_followups_release_actual_report(self):
        for path in range(4):
            with self.subTest(path=path):
                self.fixture.run([self.hid, str(path)], "hid-followup-" + str(path))

    def test_healthy_nkro_switch_and_equal_retry(self):
        for path in (4, 5):
            with self.subTest(path=path):
                self.fixture.run([self.hid, str(path)], "hid-healthy-" + str(path))

    def test_keymap_owners_preserve_independent_runtime_intent(self):
        self.fixture.run([self.hid, "6"], "keymap-independent-owner")

    def test_velocikey_failed_set_and_same_value_retry(self):
        for board, binary in self.wl.items():
            for path in range(5):
                with self.subTest(board=board, path=path):
                    self.fixture.run([binary, str(path)], board + "-retry-" + str(path))

    def test_report_regression_detects_global_reload(self):
        for mutant, path, failure in (("unsafe_get", "0", "keymap_config.nkro==!switched"), ("durable_compare", "1", "clear_count-old_clear==(switched?2U:0U)")):
            with self.subTest(mutant=mutant):
                binary = self.fixture.hid(mutant)
                self.fixture.run([binary, path], mutant + "-negative", expected="nonzero", failure=failure)

    def test_magic_reloads_release_active_report_before_format_change(self):
        binary = self.fixture.hid(magic=True)
        for path in (7, 8, 9, 10, 11, 12):
            with self.subTest(path=path):
                self.fixture.run([binary, str(path)], "magic-reload-" + str(path))

    def test_magic_regression_detects_deleted_pre_reload_clear(self):
        binary = self.fixture.hid("magic_old_clear", magic=True)
        for path in (7, 8, 10):
            with self.subTest(path=path):
                self.fixture.run([binary, str(path)], "magic-old-clear-negative-" + str(path), expected="nonzero", failure="!old_format_A")

    def test_velocikey_regression_detects_false_success_and_equal_skip(self):
        for board in self.wl:
            for mutant, path, failure in (("ignore_receipt", "4", "response[0]==id_unhandled"), ("equal_skip", "0", "eeprom_persistence_ok()")):
                with self.subTest(board=board, mutant=mutant):
                    binary = self.fixture.wl(board, mutant)
                    self.fixture.run([binary, path], board + "-" + mutant + "-negative", expected="nonzero", failure=failure)


if __name__ == "__main__":
    unittest.main()
