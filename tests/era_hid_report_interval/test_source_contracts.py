#!/usr/bin/env python3
"""Source wiring of the report interval: the width is requested, never waited
for, on every path that synthesizes a keyboard-class tap, and the transport
hooks the unit rides are attached where the contract says they are.

The executed suite proves the unit; this file pins what a host cannot run:
the forked core sites, the ChibiOS hooks and the ledger rows that name them.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8")


def body(path, name):
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", source(path), flags=re.S)
    match = re.search(r"\b" + re.escape(name) + r"\([^;{}]*\)\s*\{", text)
    if match is None:
        raise AssertionError(f"Definition not found: {path}:{name}")
    start = match.end(); depth = 1
    for end in range(start, len(text)):
        if text[end] == "{": depth += 1
        if text[end] == "}": depth -= 1
        if not depth: return text[start:end]
    raise AssertionError(f"Unbalanced function: {name}")


class ReportIntervalWiring(unittest.TestCase):
    def test_no_synthesized_tap_waits_in_the_core(self):
        action = re.sub(r"/\*.*?\*/|//[^\n]*", "", source("quantum/action.c"), flags=re.S)
        self.assertNotIn("wait_ms(TAP_HOLD_CAPS_DELAY)", action)
        self.assertNotIn("wait_ms(TAP_CODE_DELAY)", action)
        self.assertIn("tap_code_wait(code, delay)", body("quantum/action.c", "tap_code_delay"))
        self.assertIn("tap_code_wait(code, delay)", body("quantum/quantum.c", "tap_code16_delay"))
        wait = body("quantum/action.c", "tap_code_wait")
        self.assertIn("host_keyboard_delay(delay)", wait)
        self.assertIn("wait_ms(delay)", wait)
        self.assertIn("wait_ms(delay_ms)", body("tmk_core/protocol/host.c", "host_keyboard_delay"))
        self.assertNotIn("wait_ms(", body("keyboards/era/common/features/era_tapdance.c", "era_tapdance_tap_keycode"))
        self.assertNotIn("wait_ms(", body("keyboards/era/common/features/era_tapdance.c", "era_tapdance_on_reset"))

    def test_keyboard_class_reports_pass_the_hold_and_are_counted(self):
        main = "tmk_core/protocol/chibios/usb_main.c"
        keyboard = body(main, "send_keyboard")
        self.assertIn("era_hid_report_interval_hold_report(USB_ENDPOINT_IN_KEYBOARD", keyboard)
        self.assertIn("era_hid_report_interval_note_keyboard_report_posted(USB_ENDPOINT_IN_KEYBOARD)", keyboard)
        nkro = body(main, "send_nkro")
        self.assertIn("era_hid_report_interval_hold_report(USB_ENDPOINT_IN_SHARED", nkro)
        self.assertIn("era_hid_report_interval_note_keyboard_report_posted(USB_ENDPOINT_IN_SHARED)", nkro)
        self.assertIn("era_hid_report_interval_note_report_posted", body(main, "send_report"))
        for other in ("send_mouse", "send_extra"):
            self.assertNotIn("era_hid_report_interval_hold_report", body(main, other))
        events = body(main, "usb_event_cb")
        self.assertIn("era_hid_report_interval_session_edge_i(false)", events)
        self.assertIn("era_hid_report_interval_session_edge_i(event == USB_EVENT_SUSPEND)", events)
        self.assertIn("era_hid_report_interval_endpoint_completed_i(endpoint)",
                      body("tmk_core/protocol/chibios/usb_driver.c", "usb_endpoint_in_tx_complete_cb"))

    def test_the_service_runs_every_pass_and_the_unit_is_linked(self):
        task = body("keyboards/era/common/system/era_common_features.c", "era_common_features_task")
        self.assertLess(task.index("era_hid_report_interval_service()"), task.index("era_usb_session_task()"))
        rules = source("keyboards/era/common/system/era_common_qmk_rules.mk")
        self.assertIn("-DERA_HID_REPORT_INTERVAL_ENABLE", rules)
        self.assertIn("keyboards/era/common/system/era_hid_report_interval.c", rules)
        self.assertIn("keyboards/era/common/system/era_hid_report_interval_chibios.c", rules)
        glue = source("keyboards/era/common/system/era_hid_report_interval_chibios.c")
        self.assertIn("void host_keyboard_delay(uint16_t delay_ms)", glue)
        self.assertNotIn("wait_ms(", glue)

    def test_the_ledger_names_every_forked_file(self):
        ledger = source("keyboards/era/common/docs/manuals/era_qmk_fork_ledger.md")
        for path in ("`quantum/action.[ch]`", "`tmk_core/protocol/host.[ch]`", "`tmk_core/protocol/chibios/usb_main.c`"):
            self.assertIn(path, ledger)
        self.assertIn("era_hid_report_interval_endpoint_completed_i", ledger)
        self.assertIn("tap_code_wait", ledger)


if __name__ == "__main__":
    unittest.main(verbosity=2)
