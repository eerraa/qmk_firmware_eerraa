#!/usr/bin/env python3
"""Compile isolated TD mutations using the official native build's flags/objects.

Run after era-sync, in the WSL-local tree. Firmware builds still use era-build.
Only ignored copies and outputs are changed; a compile failure is never a
successful runtime control. The initial and relinked baselines run every test.
"""
from __future__ import annotations
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TARGET = "era_backlight_pulse"
CONTROLS = [
    ("full_term_time", "quantum/action_tapping.c", "ExactGlobalTermSurvivesSkippedWrapWindow",
     "TIMER_DIFF_32(e.time, tapping_key.event.time)", "TIMER_DIFF_16(e.time, tapping_key.event.time)", 2),
    ("layer_override", "quantum/action_layer.c", "LayerOverrideRetiresOnlyLayerContributions",
     "    td_layer_union = 0;", "    /* mutation: retain stale layer union */", 1),
    ("tri_recapture", "quantum/action_layer.c", "TriLayerDoesNotRecaptureTDContribution",
     "layer_state_set_regular((regular_layer_state & ~mask) | (adjusted & mask));", "(void)mask; layer_state_set_regular(adjusted);", 1),
    ("physical_owner", "quantum/process_keycode/process_tap_dance.c", "SameSlotPhysicalOwnersKeepTheirExecutedOutputs",
     "record->event.key.row * MATRIX_COLS + record->event.key.col", "0", 2),
    ("release_owner", "quantum/process_keycode/process_tap_dance.c", "ReconfiguredPhysicalMappingStillReleasesOriginalDance",
     "return TD(state->index);", "return KC_NO;", 1),
    ("owned_release_action", "quantum/action.c", "ReconfiguredHeldDanceDoesNotReleaseAnotherInputsOutput",
     "if (td_owned_release)", "if (td_owned_release && 0)", 1),
    ("event_time", "quantum/process_keycode/process_tap_dance.c", "QueuedDanceUsesOriginalPressInsteadOfDispatchTime",
     "return record->event.time;", "return timer_read32();", 1),
    ("queued_epoch", "quantum/action.c", "ClearKeyboardDoesNotReplayAQueuedDance",
     "if (tap_dance_discard_retired_record(record)) return;", "if (false && tap_dance_discard_retired_record(record)) return;", 1),
    ("reinitialization", "keyboards/era/common/features/era_tapdance.c", "ReinitializationRetiresHoldAndConsumesLateRemappedRelease",
     "    tap_dance_cancel_all();", "    /* mutation: omit runtime retirement */", 1),
    ("shared_usage", "quantum/action_util.c", "KeyboardUsageIsNotReleasedWhileAnotherOwnerRemains",
     "if (!output_key_counts[key] && !(regular_keys[key >> 3] & bit)) del_key_from_report(key);", "del_key_from_report(key);", 1),
    ("layer_contributions", "quantum/action_layer.c", "DistinctSlotsSharingALayerKeepTheRemainingHold",
     "--td_layer_counts[i] == 0", "--td_layer_counts[i] <= 1", 1),
    ("modifier_contributions", "quantum/action_util.c", "PartiallyOverlappingModifiersKeepOnlyRemainingBits",
     "--counts[i] == 0", "--counts[i] <= 1", 1),
    ("mouse_owner", "quantum/action.c", "TdHidUsageFollowsLastOwner",
     "if (!action_output_mouse_update(mouse_keycode, pressed)) return;", "(void)action_output_mouse_update(mouse_keycode, pressed);", 1),
    ("usage_owner", "quantum/action_util.c", "TdHidUsageFollowsLastOwner",
     "if (regular_usage[page] == usage || output_usage_held(page, usage)) return false;", "(void)output_usage_held;", 1),
    ("extra_current", "quantum/action_util.c", "TdExtraUsageUpClearsOnlyItsOwnUsage",
     "return current == usage || !(synthetic || output_usage_held(page, current));", "return true;", 1),
    ("osl_owned_press", "quantum/action.c", "TdOnOneShotLayerStillDecidesHold",
     "&& !tap_dance_owns_press(record)", "&& true", 1),
    ("press_edge", "quantum/action.c", "EachKeystrokeOfADanceHeldUsageReachesTheHost",
     "            del_key_from_report(code);\n#else", "            del_key(code);\n#else", 1),
    ("oneshot_regular", "quantum/action_util.c", "OneShotLayerTappedByADanceIsConsumedByItsHold",
     "oneshot_layer_switch(get_oneshot_layer(), false);", "layer_off(get_oneshot_layer());", 1),
    ("queued_content", "quantum/process_keycode/process_tap_dance.c", "QueuedRecordResolvingToAnOrdinaryKeyIsNotRetired",
     "IS_QK_TAP_DANCE(keycode) && tap_dance_remap_keycode(keymap_key_to_keycode(", "0 && tap_dance_remap_keycode(keymap_key_to_keycode(", 1),
    ("double_cancel", "quantum/process_keycode/process_tap_dance.c", "DanceRetiredAcrossTwoBoundariesLeavesNoSeat",
     "        if (owner->cancelled) {\n            memset(owner, 0, sizeof(*owner));",
     "        if (owner->cancelled) {\n            if (owner->input_epoch == record->tap_dance_epoch) memset(owner, 0, sizeof(*owner));", 1),
    ("mod_replay", "quantum/process_keycode/process_tap_dance.c", "ADanceTapReplaysTheModifiersHeldAtItsPress",
     "if (!state->pressed || state->interrupted)", "if (0)", 1),
    ("hold_no_replay", "quantum/process_keycode/process_tap_dance.c", "CapturedModifierFollowsItsOriginalOwner",
     "if (!state->pressed || state->interrupted)", "if (1)", 1),
    ("stale_tombstone", "quantum/process_keycode/process_tap_dance.c", "APressSupersedesATombstoneWhoseUpWasLost",
     "        memset(owner, 0, sizeof(*owner));\n        owner = NULL;", "        (void)0;", 1),
    ("retro_position", "keyboards/era/common/features/era_tapdance.c", "RetroTapOnADanceHoldFollowsTheSettingAtAnyPosition",
     "    record.event.key     = state->key;\n", "", 1),
    ("retro_held_mods", "quantum/action.c", "RetroTapKeepsAModifierItsOwnerStillHolds",
     "retro_tap_curr_mods & ~get_mods();", "retro_tap_curr_mods;", 1),
    ("fallback_mods", "keyboards/era/common/features/era_tapdance.c", "DoubleHoldFallbackTapCarriesTheModifiersOfEitherPress",
     "                    add_weak_mods(mods);\n", "                    (void)mods;\n", 1),
    ("quantum_keycode", "keyboards/era/common/features/era_tapdance.c", "DanceActionsRunQuantumKeycodes",
     "        tap_dance_run_quantum_keycode(&record, keycode);", "        (void)keycode;", 1),
    ("nested_dance", "quantum/process_keycode/process_tap_dance.c", "ADanceNeverStartsADanceFromItsOutput",
     "    if (IS_QK_TAP_DANCE(tap_dance_remap_keycode(keycode))) return;\n", "", 1),
    ("immediate_retire", "quantum/process_keycode/process_tap_dance.c", "AClearFromADanceOutputRetiresAnotherDancesModifierAtOnce",
     "        if (state == exempt) continue;\n", "        if (exempt) continue;\n", 1),
    ("own_dance_exempt", "quantum/process_keycode/process_tap_dance.c", "AClearFromADanceOutputKeepsThatDancesOwnHold",
     "        if (state == exempt) continue;\n", "", 1),
    ("owned_newer_up", "quantum/process_keycode/process_tap_dance.c", "AClearFromADanceOutputKeepsThatDancesOwnHold",
     "(int32_t)(record->tap_dance_epoch - state->input_epoch) >= 0", "state->input_epoch == record->tap_dance_epoch", 1),
    ("boundary_epoch", "quantum/process_keycode/process_tap_dance.c", "ADanceStartedAfterAClearInTheSameDispatchOwnsItsRelease",
     "        return (int32_t)(record->tap_dance_epoch - owner->input_epoch) < 0;\n", "        return true;\n", 1),
    ("split_retro_window", "keyboards/era/common/features/era_tapdance.c", "APeerHalfPressCancelsADanceHoldsRetroTap",
     "        era_split_tap_activity_dance_hold(true, state->key, state->last_tap_time);\n", "", 1),
    ("split_window_yield", "keyboards/era/common/split/era_split_tap_activity.c", "APeerHalfPressCancelsADanceHoldsRetroTap",
     "(g_era_split_tap_activity.window_open && g_era_split_tap_activity.phase == ERA_SPLIT_TAP_ACTIVITY_PHASE_ENGINE)",
     "g_era_split_tap_activity.window_open", 1),
    ("tap_only_release", "keyboards/era/common/features/era_tapdance.c", "TapOnlySlotIsDecidedAtItsRelease",
     "return (state->count == 1 && !era_tapdance_keycode_is_valid(entry.on_double_tap)",
     "return (state->count == 1 && era_tapdance_keycode_is_valid(entry.on_hold) && !era_tapdance_keycode_is_valid(entry.on_double_tap)", 1),
]

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main() -> None:
    if os.name != "posix" or str(ROOT).startswith("/mnt/"):
        raise SystemExit("Run after era-sync in the configured WSL-local tree, not the Windows edit tree.")
    os.chdir(ROOT)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    out = ROOT / ".era-artifacts" / "td-ownership-controls" / stamp
    out.mkdir(parents=True, exist_ok=False)
    records: list[dict] = []
    def execute(name: str, args: list[str], timeout: int = 180) -> subprocess.CompletedProcess:
        result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        (out / (name + ".log")).write_bytes(result.stdout)
        records.append({"name": name, "argv": args, "exit_code": result.returncode})
        (out / "commands.json").write_text(json.dumps(records, indent=2), encoding="utf8")
        return result
    native = execute("official-native", ["make", "test:" + TARGET, "-j8"])
    if native.returncode:
        raise SystemExit(f"Official native baseline failed; see {out}")
    passed = re.search(rb"\[  PASSED  \] (\d+) tests", native.stdout)
    if not passed:
        raise SystemExit("Native command did not execute a counted test suite")
    count = int(passed.group(1))
    meta = ROOT / ".build" / "test_obj" / TARGET
    cc = shutil.which("gcc")
    if not cc or subprocess.check_output([cc, "--version"]).strip() != (meta / "compiler.txt").read_bytes().strip():
        raise SystemExit("Compiler does not match the official native build")
    cflags = shlex.split((meta / "cflags.txt").read_text())
    ldflags = shlex.split((meta / "ldflags.txt").read_text())
    objects = list(dict.fromkeys(shlex.split((meta / "obj.txt").read_text())))
    source_names = sorted({row[1] for row in CONTROLS})
    tracked = [ROOT / path for path in [*objects, *source_names]]
    before = {str(path.relative_to(ROOT)): digest(path) for path in tracked}
    (out / "original-files.json").write_text(json.dumps(before, indent=2), encoding="utf8")
    for name in ("cflags.txt", "ldflags.txt", "obj.txt", "compiler.txt"):
        shutil.copyfile(meta / name, out / name)
    results: list[dict] = []
    baseline = ("relinked-baseline", source_names[-1], None, None, None, 0)
    for name, relative, test, old, new, expected_count in [baseline, *CONTROLS]:
        original = ROOT / relative
        text = original.read_text(encoding="utf8")
        if old is not None:
            if text.count(old) != expected_count:
                raise SystemExit(f"{name}: source anchor changed, not a detected runtime fault")
            text = text.replace(old, new)
        case = out / name
        case.mkdir()
        source = case / original.name
        obj = case / "mutated.o"
        exe = case / "test.elf"
        source.write_text(text, encoding="utf8")
        compiled = execute(name + "-compile", [cc, "-c", *cflags, "-iquote", str(original.parent), str(source), "-o", str(obj)])
        if compiled.returncode:
            raise SystemExit(f"{name}: compile failed, NOT a runtime detection; {out}")
        original_obj = str(Path(".build/test_obj") / TARGET / Path(relative).with_suffix(".o"))
        if objects.count(original_obj) != 1:
            raise SystemExit(f"{name}: exact original object missing")
        link_objects = [str(obj) if x == original_obj else x for x in objects]
        linked = execute(name + "-link", [cc, *link_objects, "--output", str(exe), *ldflags])
        if linked.returncode:
            raise SystemExit(f"{name}: link failed, NOT a runtime detection; {out}")
        args = [str(exe)]
        full_test = "EraBacklightTapHoldTiming." + test if test else None
        if full_test:
            args.append("--gtest_filter=" + full_test)
        run = execute(name + "-run", args, 30)
        output = run.stdout.decode("utf8", errors="replace")
        detected = run.returncode == 1 and full_test is not None and "[  FAILED  ] " + full_test in output
        positive = run.returncode == 0 and f"[  PASSED  ] {count} tests" in output
        result = {"control": name, "test": full_test, "compile_exit": compiled.returncode,
                  "link_exit": linked.returncode, "run_exit": run.returncode,
                  "source_sha256": digest(source), "binary_sha256": digest(exe),
                  "expected_assertion_detected": detected, "baseline_passed": positive}
        results.append(result)
        (out / "results.json").write_text(json.dumps(results, indent=2), encoding="utf8")
        if not (detected if test else positive):
            raise SystemExit(f"{name}: expected {'assertion failure' if test else 'full baseline pass'} not observed; {out}")
        print("PASS", name, full_test or f"{count} real native tests", flush=True)
    after = {str(path.relative_to(ROOT)): digest(path) for path in tracked}
    if before != after:
        raise SystemExit("An original source/object changed during the control run")
    (out / "source-preserved.json").write_text(json.dumps({"unchanged": True, "files": len(before)}), encoding="utf8")
    print("Evidence:", out, flush=True)

if __name__ == "__main__":
    main()
