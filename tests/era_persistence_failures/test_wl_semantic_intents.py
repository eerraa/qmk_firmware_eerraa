"""Exact WL recovery intents and SET debt; physical backing is modeled."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get('ERA_FIXTURE_ROOT', Path(__file__).resolve().parents[2]))
HERE = Path(__file__).resolve().parent


def extract_function(path, name, manifest):
    data = (ROOT/path).read_bytes()
    source = data.decode('utf-8').replace('\r\n', '\n')
    match = re.search(r'^[^\n]*\b' + name + r'\([^;\n]*\)\s*\{', source, re.M)
    if not match:
        raise AssertionError('Missing production function ' + name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    body = source[match.start():end] + '\n'
    manifest.append({'path': path, 'sha256': hashlib.sha256(data).hexdigest(), 'function': name,
                     'lines': [source.count('\n', 0, match.start())+1, source.count('\n', 0, end)+1],
                     'body_sha256': hashlib.sha256(body.encode()).hexdigest()})
    return body


def build(out, mutation=None):
    out.mkdir(parents=True, exist_ok=True)
    stubs = {
        'dynamic_keymap.h': '#pragma once\n#include <stdint.h>\n#define DYNAMIC_KEYMAP_LAYER_COUNT 6U\n#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 128U\nstatic inline uint16_t dynamic_keymap_macro_get_buffer_size(void) { return 128U; }\n',
        'matrix.h': '#pragma once\n#define MATRIX_ROWS 5U\n#define MATRIX_COLS 8U\n',
        'eeconfig.h': '#pragma once\n#define EECONFIG_KB_DATA_SIZE 0\n#define EECONFIG_USER_DATA_SIZE 0\n',
        'keycode_config.h': '#pragma once\n#include <stdint.h>\ntypedef struct { uint16_t raw; } keymap_config_t;\n',
        'via.h': '#pragma once\n#define VIA_EEPROM_LAYOUT_OPTIONS_SIZE 1U\n#define VIA_EEPROM_CUSTOM_CONFIG_SIZE 0U\n#define id_get_keyboard_value 2U\n',
        'raw_hid.h': '#pragma once\n#include <stdint.h>\nvoid raw_hid_send(uint8_t *,uint8_t);\n',
        'compiler_support.h': '#pragma once\n#define STATIC_ASSERT _Static_assert\n',
        'util.h': '#pragma once\n#define MIN(a,b) ((a)<(b)?(a):(b))\n',
    }
    for name, body in stubs.items():
        target = out/'stubs'/name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(body, encoding='utf-8')
    manifest = []
    saves = extract_function('keyboards/era/common/features/era_tapdance.c', 'era_tapdance_save_config', manifest)
    saves += extract_function('keyboards/era/common/features/era_tapping.c', 'era_tapping_save_config', manifest)
    (out/'production_saves.inc').write_text(saves, encoding='utf-8')
    state = ROOT/'keyboards/era/common/system/era_state_sync.c'
    state_data = state.read_bytes()
    manifest.append({'path': str(state), 'sha256': hashlib.sha256(state_data).hexdigest(), 'scope': 'whole production include; read-only pending-mask observation'})
    if mutation:
        replacements = {
            'public_payload': ('bool payload = changed &&', 'bool payload = changed && false &&'),
            'public_marker': ('s_macro_marker_before != era_state_sync_public_macro_marker()', 'false'),
            'gap_debt': ('debt = s_wl_pending_debt_mask;', 'debt = s_config_pending_persist_mask;'),
            'old_generation': ('s_wl_pending_debt_mask &= (uint16_t)~mask;', '(void)mask; /* deliberately retain obsolete debt authority */'),
            'duplicate': ('        era_state_sync_note_eeprom_mask(mask);', '        era_state_sync_note_eeprom_mask(mask); era_state_sync_note_eeprom_mask(mask);'),
            'inner_publish': ('s_wl_completed_change_mask |= mask;', 's_wl_completed_change_mask |= mask; era_state_sync_note_eeprom_mask(mask);'),
            'keep_erased_intent': ('void eeprom_driver_reset_write_observer(void) {\n    s_wl_pending_change_mask = 0;', 'void eeprom_driver_reset_write_observer(void) {\n    /* deliberately retain erased intent */'),
        }
        before, after = replacements[mutation]
        text = state_data.decode('utf-8').replace('\r\n', '\n')
        if text.count(before) != 1:
            raise AssertionError('Mutation must target exactly one production site: ' + mutation)
        state = out/'source/keyboards/era/common/system/era_state_sync.c'
        state.parent.mkdir(parents=True, exist_ok=True)
        state.write_text(text.replace(before, after, 1), encoding='utf-8')
        manifest.append({'mutation': mutation, 'before': before, 'after': after, 'sha256': hashlib.sha256(state.read_bytes()).hexdigest()})
    (out/'state_under_test.inc').write_text('#include "' + state.as_posix() + '"\n', encoding='utf-8')
    shutil.copyfile(HERE/'wl_semantic_intents.c', out/'wl_semantic_intents.c')
    units = ['drivers/eeprom/eeprom_driver.c', 'drivers/eeprom/eeprom_wear_leveling.c',
             'quantum/wear_leveling/wear_leveling.c', 'lib/fnv/hash_64a.c',
             'keyboards/era/common/storage/era_eeprom_config_io.c']
    for path in units + ['drivers/eeprom/eeprom_driver.h', 'keyboards/era/common/system/era_state_sync.h',
                         'keyboards/era/common/storage/era_eeprom_layout.h', 'platforms/eeprom.h']:
        manifest.append({'path': path, 'sha256': hashlib.sha256((ROOT/path).read_bytes()).hexdigest(), 'scope': 'production compile input'})
    exe = out/'wl-semantic.exe'
    command = [os.environ.get('CC', 'gcc'), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-DEEPROM_WEAR_LEVELING', '-DVIA_ENABLE', '-DERA_STATE_SYNC_TEST',
               '-DBACKING_STORE_WRITE_SIZE=2', '-DWEAR_LEVELING_BACKING_SIZE=8192', '-DWEAR_LEVELING_LOGICAL_SIZE=4096',
               '-I'+str(out/'stubs'), '-I'+str(out), *['-I'+str(ROOT/p) for p in
                   ('platforms', 'drivers/eeprom', 'quantum', 'quantum/wear_leveling', 'lib/fnv', 'keyboards/era/common/system')],
               '-I'+str(ROOT), str(out/'wl_semantic_intents.c'), *[str(ROOT/p) for p in units], '-o', str(exe)]
    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace')
    (out/'compile.json').write_text(json.dumps({'argv': command, 'exit': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}, indent=2), encoding='utf-8')
    (out/'source-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    print('compile return 0: ' + (mutation or 'production'))
    return exe


def run_case(exe, case):
    command = [str(exe), case]
    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace')
    (exe.parent/('runtime-' + case + '.json')).write_text(json.dumps({'argv': command, 'exit': result.returncode,
        'stdout': result.stdout, 'stderr': result.stderr}, indent=2), encoding='utf-8')
    return result


class WearLevelSemanticIntents(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        evidence = os.environ.get('ERA_WL_SEMANTIC_OUT')
        cls.temporary = None
        if evidence:
            cls.out = Path(evidence)
            cls.out.mkdir(parents=True, exist_ok=True)
        else:
            cls.temporary = tempfile.TemporaryDirectory()
            cls.out = Path(cls.temporary.name)
        cls.exe = build(cls.out/'production')

    @classmethod
    def tearDownClass(cls):
        if cls.temporary:
            cls.temporary.cleanup()

    def positive(self, case):
        result = run_case(self.exe, case)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS WL semantic intent ' + case, result.stdout)

    def negative(self, mutation, case, assertion):
        exe = build(self.out/('negative-' + mutation), mutation)
        result = run_case(exe, case)
        self.assertEqual(result.returncode, 91, result.stdout + result.stderr)
        self.assertIn('WL_SEMANTIC_ASSERT_' + assertion, result.stderr)

    def test_public_macro_failure_and_unrelated_marker_repair(self): self.positive('public_macro')
    def test_negative_public_macro_payload(self): self.negative('public_payload', 'public_macro', 'PUBLIC_FAILED_PAYLOAD')
    def test_negative_public_macro_marker(self): self.negative('public_marker', 'public_macro', 'PUBLIC_OPENER')
    def test_sparse_failure_keeps_unsaved_gap_owner(self): self.positive('sparse_debt')
    def test_new_set_survives_old_repair(self): self.positive('newer_set')
    def test_new_failed_save_recaptures_latest_set(self): self.positive('newer_failed_save')
    def test_same_value_retry_and_delayed_owner_save(self): self.positive('same_retry')
    def test_unrelated_firmware_change_still_publishes(self): self.positive('unrelated_config')
    def test_compound_config_completion_once(self): self.positive('compound_config')
    def test_nested_domain_union_at_outer_end(self): self.positive('nested_domains')
    def test_failed_outer_domain_union_repairs_once(self): self.positive('failed_nested_domains')
    def test_reopen_supersedes_failed_close(self): self.positive('reopened_macro')
    def test_erase_discards_intent_and_preserves_runtime_debt(self): self.positive('erased_intent')
    def test_protocol_reject_keeps_physical_pending(self): self.positive('rejected_write')
    def test_noop_receipt_and_pending_repair(self): self.positive('noop_debt')
    def test_negative_gap_debt_consumption(self): self.negative('gap_debt', 'sparse_debt', 'UNSAVED_GAP_DEBT')
    def test_negative_old_set_generation(self): self.negative('old_generation', 'newer_set', 'NEW_SET_DEBT_SURVIVES_OLD_REPAIR')
    def test_negative_duplicate_notification(self): self.negative('duplicate', 'sparse_debt', 'KEYMAP_REPAIR_ONCE')
    def test_negative_inner_publication(self): self.negative('inner_publish', 'nested_domains', 'INNER_NOT_PUBLISHED')
    def test_negative_erased_intent(self): self.negative('keep_erased_intent', 'erased_intent', 'ERASE_DISCARDS_INTENT_KEEPS_NEW_DEBT')


if __name__ == '__main__':
    unittest.main()
