#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Recompile isolated wake mutations against the real native QMK objects.

Run after the two native wake targets, under the host build lock. Product
sources and native objects are read only. Compile/link failures are not runtime
detections. --root permits running this script from the Windows edit tree while
using the Linux local-filesystem tree synchronized by era_build.py, with its
sources, flags and objects.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess

BASE = 'era_wake_input'
SWAP = 'era_wake_input_swap'
CONTROLS = [
    ('wake-dispatch', BASE, 'quantum/keyboard.c', 'HeldDanceKeyReleasesAfterRealWake',
     'action_exec_wakeup_release(MAKE_KEYEVENT(row, col, false));', '(void)0;', 1, 0),
    ('queued-match', BASE, 'quantum/action.c', 'QueuedDanceDownAndWakeUpKeepOriginalShortTapTime',
     'pending_wakeup_press = action_tapping_has_pending_press(event);', 'pending_wakeup_press = false;', 1, 0),
    ('wake-boundary', BASE, 'platforms/chibios/suspend.c', 'QueuedDanceSettlesOrdinaryBeforeCapturedPhysicalUp',
     '    action_prepare_wakeup();', '    /* mutation: omit pending input boundary */', 1, 0),
    ('actual-dispatch', BASE, 'quantum/quantum.c', 'QueuedSpaceCadetStillOwnsUpWhenItsModuleReturnsFalse',
     '    action_wakeup_record_dispatch(record, keycode);', '    /* mutation: omit actual dispatch lifetime */', 1, 0),
    ('final-owner-check', BASE, 'quantum/action.c', 'RetiredInvalidQueuedDanceDoesNotLeakIntoRemappedOrdinaryOutput',
     'if (record->tap_dance_wakeup_record && !record->event.pressed &&\n        tap_dance_owned_keycode(record) == KC_NO && !wakeup_action) return;',
     'if (false && record->tap_dance_wakeup_record && !record->event.pressed &&\n        tap_dance_owned_keycode(record) == KC_NO && !wakeup_action) return;', 1, 0),
    ('repeated-wake', BASE, 'quantum/action.c', 'SecondWakeClearsPriorOrdinaryDispatchQualification',
     '    memset(wakeup_action_down, 0, sizeof(wakeup_action_down));', '    /* mutation: retain prior wake qualification */', 1, 0),
    ('accepted-raw-position', SWAP, 'quantum/keyboard.c', 'FreshUnswappedWakePositionCannotReleaseOtherSwappedOwner',
     'else if (key_action_open && !key_pressed && (matrix_action_down[row] & col_mask))',
     'else if (key_action_open && !key_pressed)', 1, 0),
    ('raw-release-position', SWAP, 'quantum/action.c', 'RawWakeQualificationSurvivesAnotherLogicalPositionRelease',
     'return record->tap_dance_raw_key;', 'return record->event.key;', 1, 0),
    ('synthetic-raw-before-term', SWAP, 'quantum/action_tapping.c', 'SyntheticTapReleaseKeepsOtherRawWakePositionBeforeTerm',
     'keyrecord_t release_record = tapping_key;',
     'keyrecord_t release_record = tapping_key; release_record.tap_dance_raw_key = (keypos_t){0};', 2, 0),
    ('synthetic-raw-after-term', SWAP, 'quantum/action_tapping.c', 'SyntheticTapReleaseKeepsOtherRawWakePositionAfterTerm',
     'keyrecord_t release_record = tapping_key;',
     'keyrecord_t release_record = tapping_key; release_record.tap_dance_raw_key = (keypos_t){0};', 2, 1),
    ('ordinary-layer-capture', BASE, 'quantum/action.c', 'HeldMomentaryLayerReleasesAfterWake',
     'if (event.pressed) wakeup_record_layer_on(record, action.layer_tap.val, 0, false);',
     'if (event.pressed) (void)0;', 1, 0),
    ('original-layer-identity', BASE, 'quantum/action.c', 'RetainedLayerWakeReleaseUsesOriginalActionAfterRemap',
     'else layer_off(layer - 1);',
     'else layer_off(layer_switch_get_action(record->event.key).layer_tap.val);', 1, 0),
    ('explicit-layer-replacement', BASE, 'quantum/action_layer.c', 'ExplicitLayerReplacementRetiresOldWakeRelease',
     '    action_wakeup_reset_layers();', '    /* isolated omission: retain stale layer identity */', 1, 0),
    ('before-wake-lm-mods', BASE, 'quantum/action.c', 'HeldLayerModsWakeReleasePreservesFreshModifier',
     '            wakeup_layer_down[row][col] &= 0xff;',
     '            wakeup_layer_down[row][col] &= 0xffff;', 1, 0),
    ('after-wake-lm-mods', BASE, 'quantum/action.c', 'QueuedLayerModsReleaseNewModifiersAfterWake',
     '    if (action >> 8) unregister_mods(action >> 8);',
     '    /* isolated omission: keep post-wake LM modifiers */', 1, 0),
    ('replacement-lm-mods', BASE, 'quantum/action.c', 'LayerReplacementKeepsQueuedLayerModsModifierRelease',
     '                wakeup_layer_down[row][col] &= 0xff00;',
     '                wakeup_layer_down[row][col] = 0;', 1, 0),
    ('tap-toggle-inverse', BASE, 'quantum/action.c', 'HeldTapToggleRestoresPreviouslyEnabledLayerAfterWake',
     'if (action & WAKEUP_LAYER_INVERT) layer_invert(layer - 1);',
     'if (action & WAKEUP_LAYER_INVERT) layer_off(layer - 1);', 1, 0),
    ('disabled-oneshot-layer', BASE, 'quantum/action.c', 'RuntimeDisabledOneShotLayerReleasesAsMomentaryLayer',
     'if (!keymap_config.oneshot_enable) {\n                        if (event.pressed) {\n                            layer_on(action.layer_tap.val);\n#        ifdef ERA_TAP_DANCE_WAKE_CAPTURE\n                            wakeup_record_layer_on(record, action.layer_tap.val, 0, false);',
     'if (!keymap_config.oneshot_enable) {\n                        if (event.pressed) {\n                            layer_on(action.layer_tap.val);\n#        ifdef ERA_TAP_DANCE_WAKE_CAPTURE\n                            /* isolated omission: no momentary OSL identity */', 1, 0),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    root = parser.parse_args().root.resolve()
    if os.name != 'posix' or str(root).startswith('/mnt/'):
        raise SystemExit('Use the Linux local-filesystem root synchronized by era_build.py and its native build objects.')
    out = root / '.era-artifacts/improvement-V261004R2/usb/wake-controls' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    out.mkdir(parents=True, exist_ok=False)
    records = []

    def run(label, command, timeout=90):
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=timeout)
        (out / (label + '.log')).write_text(result.stdout + result.stderr)
        records.append(dict(name=label, argv=command, exit=result.returncode))
        (out / 'commands.json').write_text(json.dumps(records, indent=2))
        return result

    metadata = {}
    native_counts = {}
    tracked = set(root / item[2] for item in CONTROLS)
    tracked.update(root / name for name in ['quantum/action.h', 'quantum/action_tapping.h'])
    for target in (BASE, SWAP):
        meta = root / '.build/test_obj' / target
        objects = list(dict.fromkeys(shlex.split((meta / 'obj.txt').read_text())))
        flags = shlex.split((meta / 'cflags.txt').read_text())
        links = shlex.split((meta / 'ldflags.txt').read_text())
        compiler = subprocess.check_output(['gcc', '--version']).strip()
        assert compiler == (meta / 'compiler.txt').read_bytes().strip(), 'Native compiler differs'
        metadata[target] = (objects, flags, links)
        tracked.update(root / value for value in objects)
        native = run(target + '-native', [str(root / '.build/test' / (target + '.elf'))], 30)
        count = re.search(r'\[  PASSED  \] (\d+) tests', native.stdout)
        assert native.returncode == 0 and count, 'Native baseline did not pass'
        native_counts[target] = int(count.group(1))

    def fingerprint():
        return {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(tracked)}
    before = fingerprint()
    (out / 'original-files.json').write_text(json.dumps(before, indent=2))
    baselines = [('relinked-' + target, target, 'quantum/action.c', None, None, None, 0, 0) for target in (BASE, SWAP)]
    for label, target, relative, case, old, new, count, occurrence in [*baselines, *CONTROLS]:
        original = root / relative
        text = original.read_text()
        if old is not None:
            positions = [match.start() for match in re.finditer(re.escape(old), text)]
            assert len(positions) == count, label + ': source anchor changed'
            index = positions[occurrence]
            text = text[:index] + new + text[index + len(old):]
        copied, obj, exe = out / (label + '.c'), out / (label + '.o'), out / (label + '.elf')
        copied.write_text(text)
        objects, flags, links = metadata[target]
        selected = str(Path('.build/test_obj') / target / Path(relative).with_suffix('.o'))
        assert objects.count(selected) == 1, label + ': original object missing'
        # Preserve the native include order. Adding ChibiOS as an -iquote
        # directory would replace the fixture's TEST gpio.h in nested headers.
        compiled = run(label + '-compile', ['gcc', *flags, '-c', str(copied), '-o', str(obj)])
        assert compiled.returncode == 0, label + ': compile failure is NOT runtime detection'
        linked = run(label + '-link', ['gcc', *(str(obj) if value == selected else value for value in objects), '-o', str(exe), *links])
        assert linked.returncode == 0, label + ': link failure is NOT runtime detection'
        full_case = 'EraWakeInput.' + case if case else None
        result = run(label + '-run', [str(exe), *(['--gtest_filter=' + full_case] if full_case else [])], 30)
        if full_case:
            assert result.returncode == 1 and '[ RUN      ] ' + full_case in result.stdout and '[  FAILED  ] ' + full_case in result.stdout, label + ': named runtime assertion did not fail'
        else:
            assert result.returncode == 0 and '[  PASSED  ] ' + str(native_counts[target]) + ' tests' in result.stdout, label + ': relinked production failed'
        print('PASS', label, 'compiled runtime negative' if full_case else 'production relink', flush=True)
    assert fingerprint() == before, 'Product sources or native objects changed'
    (out / 'source-preserved.json').write_text(json.dumps(dict(unchanged=True, file_count=len(before))))
    print('Evidence:', out, flush=True)


if __name__ == '__main__':
    main()
