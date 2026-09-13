#!/usr/bin/env python3
"""Source wiring checks complementary to the executed production-boundary suite.

These assertions do not simulate Core1/PIO or prove device latency. They keep
unexercised hardware-dispatch edges attached to the behavior tested by the
fixture; the official firmware build checks their ABI and complete call graph.
"""
from pathlib import Path
import re
import json
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMMON = ROOT / "keyboards/era/common"
def source(path):
    return (COMMON / path).read_text(encoding="utf-8")
def root_source(path):
    return (ROOT / path).read_text(encoding="utf-8")
def body(path, name):
    text = source(path)
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    match = re.search(r"\b" + re.escape(name) + r"\([^;{}]*\)\s*\{", text)
    if match is None:
        raise AssertionError(f"Definition not found: {path}:{name}")
    start = match.end(); depth = 1
    for end in range(start, len(text)):
        if text[end] == "{": depth += 1
        if text[end] == "}": depth -= 1
        if not depth: return text[start:end]
    raise AssertionError(f"Unbalanced function: {name}")

class SourceContracts(unittest.TestCase):
    def test_dirty_wire_is_an_explicit_repair_input(self):
        scheduler = source("split/era_split_transport_scheduler.c")
        self.assertRegex(scheduler, r"update_mode\(\(pending_dirty_flags & ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE\) != 0\)")
        update = body("split/era_split_transport_scheduler.c", "era_split_transport_scheduler_update_mode")
        self.assertIn("wire_role_changed || repair_wire", update)
        self.assertRegex(update, r"if \(repair_wire\)\s*\{\s*dirty_flags \|= ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE;")
        self.assertIn("era_split_communication_core_launch_capped()", update)
        start = body("split/era_split_transport_scheduler.c", "era_split_transport_scheduler_start_communication_core")
        self.assertNotIn("authority_snapshot_valid = false", start)

    def test_configuration_never_performs_unchecked_pio_restart(self):
        select = body("split/era_split_transaction_backend_rp2040.c", "era_split_transaction_backend_set_speed")
        self.assertNotIn("era_split_transaction_backend_pio_init", select)
        self.assertIn("transaction_backend_initialized = false", select)
        init = body("split/era_split_transaction_backend_rp2040.c", "era_split_transaction_backend_init_role")
        self.assertIn("if (!initialized)", init)
        owner = source("split/communication_core/era_split_communication_core_owner.c")
        self.assertIn("era_split_transaction_backend_role_ready", owner)

    def test_actual_cold_transition_is_shared_not_copied(self):
        include = '#include "scheduler/era_split_transport_scheduler_link.inc"'
        self.assertIn(include, source("split/era_split_transport_scheduler.c"))
        fixture = (ROOT / "tests/era_split_link_lifecycle/link_under_test.c").read_text()
        self.assertIn('split/scheduler/era_split_transport_scheduler_link.inc"', fixture)
        apply = body("split/era_split_link.c", "era_split_link_apply")
        self.assertLess(apply.index("era_split_transport_scheduler_apply_link_level"), apply.index("era_split_link_store"))
        self.assertNotIn("era_split_link_store", body("split/era_split_link.c", "era_split_link_task"))
        self.assertNotIn("era_split_link_load", body("split/era_split_link.c", "era_split_link_active_level"))

    def test_optional_flash_yields_and_cold_capture_follows_deadline(self):
        maintenance = body("system/era_common_features.c", "era_common_features_maintenance_task")
        self.assertLess(maintenance.index("era_split_restart_agreement_timed_window"), maintenance.index("era_eeprom_driver_maintenance_task"))
        cold = body("split/era_host_peer_storage.c", "era_host_peer_storage_task")
        self.assertIn("era_split_restart_agreement_timed_window()", cold)
        peer = body("split/era_host_peer_storage.c", "era_host_peer_storage_peer_task")
        self.assertLess(peer.index("era_host_peer_storage_process_peer_result"), peer.index("era_split_restart_agreement_timed_window"))
        self.assertLess(peer.index("era_split_restart_agreement_timed_window"), peer.index("era_host_peer_storage_start_peer_episode"))
        scheduler = source("split/era_split_transport_scheduler.c")
        self.assertLess(scheduler.index("era_split_restart_agreement_task();"), scheduler.index("era_host_peer_storage_task(timer_read32())"))

    def test_link_cannot_touch_usb_or_keep_an_owner_shadow(self):
        link = source("split/era_split_link.c") + source("split/era_split_via_link.c")
        self.assertNotIn("owner_apply_armed", link)
        self.assertNotIn("usbDisconnectBus", link)
        self.assertNotIn("usbConnectBus", link)
        self.assertNotIn("schedule_reattach", link)
        for path in ["system/era_usb_session.c", "system/era_usb_session.h", "split/era_split_authority_reducer.c"]:
            self.assertNotIn("firmware_reattach", source(path))
        dispatch = body("split/era_split_keyboard.c", "era_split_restart_prepare_local")
        self.assertIn("return era_split_link_apply(param);", dispatch)

    def test_durable_no_change_cannot_bypass_sealed_recovery(self):
        replace = body("storage/era_nvm.c", "era_nvm_replace")
        self.assertIn("!nvm->tail_sealed && era_nvm_source_matches_image", replace)
        rotate = body("storage/era_nvm.c", "era_nvm_rotate")
        self.assertIn("nvm->tail_sealed = true", rotate)

    def test_reconciliation_success_is_presentation_only_and_status_colours_are_semantic(self):
        step = body("split/era_split_link.c", "era_split_link_step_due")
        note = body("split/era_split_link.c", "era_split_link_note_relation")
        self.assertIn("reconcile_search_stepped = true", step)
        self.assertIn("recovery_report_pending = true", note)
        self.assertRegex(note, r"if \(local_is_initiator\)\s*\{\s*g_era_split_link.recovery_report_pending = true;")
        self.assertIn("peer_rate_searched", note)
        # The listener's search reaches the talker on the SESSION_STATUS answer only:
        # built from the link lane at every build, consumed with the same session
        # record that produces the serviced edge, never rewritten by AUTHORITY.
        self.assertIn("era_split_link_rate_searched()", body("split/era_split_scheduler_session.c", "era_split_scheduler_session_note_local_facts"))
        self.assertIn("!response_requested && ", body("split/era_split_scheduler_session.c", "era_split_scheduler_session_build_local_status"))
        self.assertNotIn("rate_searched", body("split/era_split_scheduler_session.c", "era_split_scheduler_session_note_peer_authority"))
        self.assertIn("peer_session.rate_searched", body("split/era_split_transport_scheduler.c", "era_split_transport_scheduler_update_mode"))
        self.assertRegex(body("split/era_split_transport_scheduler.c", "era_split_transport_scheduler_update_mode"),
                         r"peer_session.known && peer_session.rate_searched,\s*next_local_wire_initiator\)")
        self.assertIn("ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_RATE_SEARCHED", body("split/era_split_wire_payload.c", "era_split_wire_validate_session_status_payload"))
        self.assertNotIn("recovery_report_pending", body("split/era_split_link.c", "era_split_link_runtime_settled"))
        self.assertNotIn("recovery_report_pending", body("split/era_split_link.c", "era_split_link_request_apply"))
        commit = body("split/era_split_link.c", "era_split_link_reconcile_success_report_commit")
        self.assertIn("era_split_restart_agreement_agreed_deadline", commit)
        self.assertNotIn("timer_read32", commit)
        self.assertNotIn("era_split_link_apply", commit)
        self.assertNotIn("era_split_link_store", commit)
        advance = body("split/era_split_link.c", "era_split_link_reconcile_success_report_advance")
        self.assertIn("timer_read32() - g_era_split_link.recovery_report_start_ms", advance)
        dispatch = body("split/era_split_keyboard.c", "era_split_restart_prepare_local")
        self.assertIn("return era_split_link_reconcile_success_report_commit();", dispatch)
        ready = body("split/era_split_keyboard.c", "era_split_restart_arm_ready")
        self.assertIn("era_host_peer_transaction_time_anchor_adopted()", ready)

        tomak = root_source("keyboards/era/sirind/common/tomak_common.c")
        self.assertRegex(tomak, r"case TOMAK_STATUS_SOURCE_LINK_RECONCILE_SUCCESS:\s*green = 255;")
        self.assertRegex(tomak, r"case TOMAK_STATUS_SOURCE_EEPROM_SYNC:\s*blue = 255;")
        self.assertRegex(tomak, r"case TOMAK_STATUS_SOURCE_LINK_FALLBACK:\s*default:\s*red = 255;")
        self.assertIn("tomak_status_policy_source != tomak_status_frame_source", tomak)
        self.assertIn("status_frame && tomak_status_policy_source == TOMAK_STATUS_SOURCE_EEPROM_SYNC", tomak)

    def test_local_apply_feedback_observes_without_owning_execution(self):
        observe = body("split/era_split_link.c", "era_split_link_apply_feedback_task")
        self.assertIn("era_split_restart_agreement_holds_intent", observe)
        self.assertNotIn("era_split_restart_agreement_last_result", observe)
        for forbidden in ("era_split_restart_agreement_request", "era_split_link_store", "era_split_link_apply("):
            self.assertNotIn(forbidden, observe)
        task = body("split/era_split_link.c", "era_split_link_task")
        self.assertLess(task.index("era_split_link_apply_feedback_task();"),
                        task.index("era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_RECOVERED"))
        apply = body("split/era_split_link.c", "era_split_link_apply")
        self.assertLess(apply.index("era_split_link_store(level)"),
                        apply.index("era_split_link_apply_feedback_set(stored ? ERA_SPLIT_LINK_APPLY_APPLIED"))
        self.assertIn("apply_confirming = stored &&", apply)
        set_feedback = body("split/era_split_link.c", "era_split_link_apply_feedback_set")
        self.assertIn("status != ERA_SPLIT_LINK_APPLY_PENDING", set_feedback)
        report = body("split/era_split_link.c", "era_split_link_apply_report_advance")
        for forbidden in ("era_split_restart_agreement_request", "era_split_link_store", "soft_reset_keyboard"):
            self.assertNotIn(forbidden, report)
        tomak = root_source("keyboards/era/sirind/common/tomak_common.c")
        self.assertIn("era_split_link_apply_report_advance(&status, &on)", tomak)
        self.assertIn("tomak_link_apply_visibility_task();", tomak)
        self.assertNotIn("TOMAK_STATUS_SOURCE_LINK_APPLY_PENDING", tomak)
        self.assertNotIn("TOMAK_STATUS_SOURCE_LINK_APPLY_UNCHANGED", tomak)
        self.assertRegex(tomak, r"case TOMAK_STATUS_SOURCE_LINK_APPLY_SUCCESS:\s*green = 255;")
        self.assertIn("tomak_link_apply_status_cached == ERA_SPLIT_LINK_APPLY_UNCHANGED", tomak)
        policy = tomak[tomak.index("void rgb_matrix_render_policy_kb("):]
        self.assertLess(policy.index("rgb_matrix_get_suspend_state()"), policy.index("tomak_link_apply_active_cached"))
        self.assertLess(policy.index("tomak_link_fallback_active_cached"), policy.index("tomak_link_apply_active_cached"))

    def test_only_non_disruptive_acts_skip_hid_quiet(self):
        table = source("split/era_split_keyboard.c")
        for act in ("LINK_SPEED", "LINK_RECOVERED"):
            row = re.search(r"\[ERA_SPLIT_RESTART_ACT_" + act + r"\]\s*=\s*\{([^}]+)\}", table).group(1)
            self.assertIn(".skips_hid_quiet = true", row)
            self.assertIn(".requires_confirmation = true", row)
            self.assertIn(".yields_to_storage = true", row)
            self.assertIn(".resets = false", row)
        clean = re.search(r"\[ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN\]\s*=\s*\{([^}]+)\}", table).group(1)
        self.assertNotIn(".skips_hid_quiet = true", clean)
        self.assertIn(".resets = true", clean)

    def test_single_listener_window_stays_on_the_existing_cold_path(self):
        step = body("split/era_split_link.c", "era_split_link_step_due")
        self.assertIn("ERA_SPLIT_LINK_SCAN_DWELL_MS", step)
        self.assertNotIn("scan_undecodable_after_guard", source("split/era_split_link.c"))
        self.assertNotIn("ERA_SPLIT_LINK_SCAN_EARLY_MS", source("split/era_split_link.h"))
        self.assertNotIn("ERA_SPLIT_LINK_SCAN_NOISE_GAP_MS", source("split/era_split_link.h"))
        self.assertRegex(source("split/era_split_link.h"), r"define ERA_SPLIT_LINK_SCAN_DWELL_MS 400\b")
        self.assertGreaterEqual(step.count("era_split_communication_core_responder_accepted_rx_count()"), 2)
        for forbidden in ("era_split_link_store", "wait_ms", "mark_dirty", "wake", "era_split_restart_agreement_request"):
            self.assertNotIn(forbidden, step)
        note = body("split/era_split_link.c", "era_split_link_note_relation")
        self.assertRegex(note, r"if \(serviced \|\| !listening\)\s*\{\s*g_era_split_link.scan_valid = false;")
        scan = body("split/era_split_transport_scheduler.c", "era_split_transport_scheduler_transport_step")
        self.assertNotIn("era_split_link_step_due", scan)
        cold = body("split/era_split_transport_scheduler.c", "era_split_transport_scheduler_housekeeping_body")
        self.assertIn("era_split_link_step_due(&link_step_level)", cold)

    def test_disconnected_sender_cadence_and_wait_primitives_remain_bounded(self):
        constants = source("split/scheduler/era_split_transport_scheduler_internal.h")
        self.assertRegex(constants, r"define ERA_SPLIT_WIRE_BOOTSTRAP_PERIOD_MS 25\b")
        self.assertRegex(constants, r"define ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER 10\b")
        self.assertRegex(constants, r"define ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS 100\b")
        self.assertIn("__WFE();", body("split/era_split_transaction_backend_rp2040.c", "era_split_transaction_backend_park_until"))
        self.assertIn("era_split_transaction_backend_park_until", body("split/era_split_transaction_backend_rp2040.c", "era_split_transaction_backend_receive_response_window_until"))
        self.assertIn("era_split_transaction_backend_park_until", body("split/era_split_transaction_backend_rp2040.c", "era_split_transaction_backend_receive_responder_until"))
        scheduler = source("split/era_split_transport_scheduler.c")
        self.assertIn("ERA_SPLIT_LINK_SCAN_DWELL_MS >=", scheduler)
        self.assertRegex(scheduler, r"2U \* \(ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS \+\s*ERA_SPLIT_PEER_RESPONSE_WINDOW_MS.*\+\s*ERA_SPLIT_AUTHORITY_POLL_PERIOD_MS\)")

    def test_all_six_split_definitions_expose_read_only_local_link_labels(self):
        expected = {
            "id_qmk_split_link_runtime": (64, "Runtime Level"),
            "id_qmk_split_link_stored": (65, "Saved Level"),
            "id_qmk_split_link_result": (66, "Last Apply (local)"),
        }
        def nodes(value):
            if isinstance(value, dict):
                yield value
                for child in value.values():
                    yield from nodes(child)
            elif isinstance(value, list):
                for child in value:
                    yield from nodes(child)
        seen = 0
        for board in ("tomak", "tomak79h", "tomak79s"):
            for path in (ROOT / "keyboards/era/sirind" / board / "keymaps/via").glob("*VIA.json"):
                seen += 1
                controls = list(nodes(json.loads(path.read_text(encoding="utf-8"))))
                for name, (value_id, label) in expected.items():
                    with self.subTest(path=path.name, control=name):
                        found = [c for c in controls if c.get("content") == [name, 9, value_id]]
                        self.assertEqual(len(found), 1)
                        self.assertEqual(found[0]["type"], "label")
                        self.assertEqual(found[0]["label"], label)
                channels = [tuple(c["content"][1:]) for c in controls
                            if isinstance(c.get("content"), list) and len(c["content"]) == 3
                            and c["content"][1] == 9 and isinstance(c["content"][2], int)]
                self.assertEqual(len(channels), len(set(channels)))
        self.assertEqual(seen, 6)
        header = source("split/era_split_via_link.h")
        for name, value in (("RUNTIME", 64), ("STORED", 65), ("RESULT", 66)):
            self.assertRegex(header, rf"#define ERA_SPLIT_VIA_LINK_{name}_VALUE_ID {value}\b")

if __name__ == "__main__":
    unittest.main(verbosity=2)
