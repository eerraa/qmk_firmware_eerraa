"""Audit regressions at production caller/backend boundaries; hardware is modeled."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def execute(command, failure=None, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace', **kwargs)
    if failure is None:
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
    elif result.returncode == 0 or failure not in result.stdout + result.stderr:
        raise AssertionError('Missing runtime counterexample: ' + result.stdout + result.stderr)
    return result


def nor_fixture(out, root=ROOT, mutation=None):
    scope = dict(ROOT=root, OUT=out, Path=Path, hashlib=hashlib, json=json, re=re)
    exec((HERE/'nor_setup.py').read_text(encoding='utf-8'), scope)
    extract, write = scope['extract'], scope['write']
    owners = {
        'quantum/eeconfig.c': 'eeconfig_require_reset eeconfig_is_init_required eeconfig_init_quantum eeconfig_init eeconfig_enable eeconfig_is_enabled',
        'quantum/via.c': 'via_eeprom_is_valid via_eeprom_set_valid via_init eeconfig_init_via via_get_layout_options via_set_layout_options',
        'quantum/nvm/eeprom/nvm_eeconfig.c': 'nvm_eeconfig_is_enabled nvm_eeconfig_is_disabled nvm_eeconfig_enable nvm_eeconfig_disable',
        'quantum/nvm/eeprom/nvm_via.c': 'nvm_via_erase nvm_via_read_magic nvm_via_update_magic nvm_via_read_layout_options nvm_via_update_layout_options',
        'quantum/keyboard.c': 'quantum_init',
    }
    body = '\n'.join(extract(path, name) for path, names in owners.items() for name in names.split())
    if mutation == 'boot_gate':
        body = body.replace('eeconfig_init_failed = true;', 'eeconfig_init_failed = false;', 1)
    elif mutation == 'nested_defaults':
        before = 'eeprom_transaction_active() || !eeprom_begin_transaction()'
        assert before in body
        body = body.replace(before, '!eeprom_begin_transaction()', 1)
    write('boot_functions.inc', body)
    write('stubs/util.h', '#pragma once\n#define MIN(a,b) ((a)<(b)?(a):(b))\n')
    write('stubs/nvm_eeprom_eeconfig_internal.h', '#pragma once\n#include "eeconfig.h"\n')
    write('stubs/nvm_eeconfig.h', '#pragma once\n#include <stdbool.h>\nbool nvm_eeconfig_is_enabled(void);\nbool nvm_eeconfig_is_disabled(void);\nvoid nvm_eeconfig_disable(void);\nvoid nvm_eeconfig_enable(void);\n')
    for name in ('nor.c', 'boot.c'):
        shutil.copyfile(HERE/name, out/name)
    return ['gcc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-DVIA_ENABLE', '-DERA_STATE_SYNC_TEST', '-DEEPROM_CUSTOM', '-DEEPROM_DRIVER', '-DEEPROM_SIZE=24576',
            '-I'+str(out/'stubs'), '-I'+str(out), '-I'+str(root/'platforms'), '-I'+str(root/'drivers/eeprom'), '-I'+str(root)]


def overlay(out, path, before, after):
    root = out/'source'
    for directory in ('keyboards/era/common', 'drivers/eeprom', 'quantum', 'lib/fnv'):
        shutil.copytree(ROOT/directory, root/directory)
    (root/'platforms').mkdir()
    shutil.copyfile(ROOT/'platforms/eeprom.h', root/'platforms/eeprom.h')
    target = root/path
    source = target.read_text(encoding='utf-8')
    assert before in source, path
    target.write_text(source.replace(before, after, 1), encoding='utf-8')
    return root


class PersistenceFailures(unittest.TestCase):
    def run_wl(self, mutation=None):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            root = ROOT
            if mutation == 'duplicate_notification':
                root = overlay(out, 'keyboards/era/common/system/era_state_sync.c',
                               '        era_state_sync_note_eeprom_mask(mask);\n    }\n}\n#endif',
                               '        era_state_sync_note_eeprom_mask(mask); era_state_sync_note_eeprom_mask(mask);\n    }\n}\n#endif')
            nor_fixture(out, root)
            header = out/'stubs/dynamic_keymap.h'
            header.write_text(header.read_text().replace('16384U', '128U'))
            scope = dict(ROOT=root, OUT=out, Path=Path, hashlib=hashlib, json=json, re=re)
            exec((HERE/'nor_setup.py').read_text(encoding='utf-8'), scope)
            header.write_text(header.read_text().replace('16384U', '128U'))
            extract = scope['extract']
            names = [
                ('keyboards/era/common/system/era_board_hooks.c', 'era_board_keyboard_channel_save_arm era_board_keyboard_channel_save_commit era_board_keyboard_channel_save_now era_board_persistence_flush_pending shutdown_kb'),
                ('quantum/quantum.c', 'shutdown_quantum soft_reset_keyboard'),
                ('keyboards/era/common/system/era_via_system.c', 'era_via_system_raw_hid_quiet_ms era_via_system_restart_quiet_ok era_via_system_eeprom_invalidate era_via_system_eeprom_clean_handed_off era_via_system_trigger_eeprom_reset_if_confirmed era_via_system_task'),
            ]
            body = '\n'.join(extract(path, name) for path, functions in names for name in functions.split())
            if mutation == 'clean_receipt':
                body = body.replace('return persisted &&', '(void)persisted; return')
            elif mutation == 'clean_retry':
                before = 'if (eeprom_reset_retry_delay_ms && timer_elapsed32(eeprom_reset_retry_started_ms) < eeprom_reset_retry_delay_ms)'
                assert before in body
                body = body.replace(before, 'if (false)', 1)
            body += '\n#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR 801U\n' + (out/'macro_functions.inc').read_text()
            source = (HERE/'wl.c').read_text().replace('/* PRODUCTION_FUNCTIONS */', body)
            (out/'wl.c').write_text(source)
            sources = ['drivers/eeprom/eeprom_driver.c', 'drivers/eeprom/eeprom_wear_leveling.c', 'quantum/wear_leveling/wear_leveling.c', 'lib/fnv/hash_64a.c', 'keyboards/era/common/system/era_state_sync.c', 'keyboards/era/common/storage/era_eeprom_config_io.c']
            exe = out/'wl.exe'
            flags = ['-DEEPROM_WEAR_LEVELING', '-DVIA_ENABLE', '-DERA_STATE_SYNC_TEST', '-DBACKING_STORE_WRITE_SIZE=2', '-DWEAR_LEVELING_BACKING_SIZE=8192', '-DWEAR_LEVELING_LOGICAL_SIZE=4096']
            includes = ['-I'+str(out/'stubs'), *['-I'+str(root/p) for p in ('platforms', 'drivers/eeprom', 'quantum', 'quantum/wear_leveling', 'lib/fnv')], '-I'+str(root)]
            execute(['gcc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', *flags, *includes, str(out/'wl.c'), *[str(root/p) for p in sources], '-o', str(exe)])
            execute([str(exe)], failure=('Assertion' if mutation else None))

    def test_wl_clean_and_recovery_notifications(self):
        self.run_wl()

    def test_negative_clean_cache_receipt(self):
        self.run_wl('clean_receipt')

    def test_negative_clean_retry_interval(self):
        self.run_wl('clean_retry')

    def test_negative_wl_duplicate_notification(self):
        self.run_wl('duplicate_notification')

    def run_nor(self, boot=False, mutation=None):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            root = ROOT
            if mutation == 'macro_equal':
                root = overlay(out, 'keyboards/era/common/storage/era_eeprom_driver.c',
                               '!era_eeprom_driver_range_touches_macro(address, length) &&', '((void)address, (void)length, true) &&')
            elif mutation == 'semantic_envelope':
                root = overlay(out, 'keyboards/era/common/storage/era_eeprom_driver.c',
                               'if (s_edit_completing) {', 'if (false) {')
            flags = nor_fixture(out, root, mutation)
            sources = ['drivers/eeprom/eeprom_driver.c', 'keyboards/era/common/storage/era_eeprom_config_io.c', 'keyboards/era/common/system/era_state_sync.c']
            if boot:
                sources.append('keyboards/era/common/storage/era_reset_guard.c')
            exe = out/'test.exe'
            execute([*flags, str(out/('boot.c' if boot else 'nor.c')), *[str(root/p) for p in sources], '-o', str(exe)])
            execute([str(exe)], failure=('Assertion' if mutation else None))

    def test_nor_faults_macro_and_semantic_domains(self):
        self.run_nor()

    def test_boot_reset_key_failure_and_retry(self):
        self.run_nor(boot=True)

    def test_negative_boot_gate(self):
        self.run_nor(boot=True, mutation='boot_gate')

    def test_negative_nested_defaults_completion(self):
        self.run_nor(boot=True, mutation='nested_defaults')

    def test_negative_equal_macro_elision(self):
        self.run_nor(mutation='macro_equal')

    def test_negative_semantic_envelope(self):
        self.run_nor(mutation='semantic_envelope')

    def run_callers(self, mutation=None):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            root = ROOT
            if mutation == 'save_receipt':
                root = overlay(out, 'keyboards/era/common/features/era_tapdance.c',
                               'return eeprom_end_transaction();', 'eeprom_end_transaction(); return true;')
            elif mutation == 'same_policy':
                root = overlay(out, 'keyboards/era/common/split/era_split_sync_policy.c',
                               'return !era_split_sync_policy_state.persist_pending || era_split_sync_policy_persist_current();',
                               'return !changed || era_split_sync_policy_persist_current();')
            elif mutation == 'same_sleep':
                root = overlay(out, 'keyboards/era/common/features/era_lighting_sleep.c',
                               'if (era_lighting_sleep_rgb_timeout_seconds() != seconds) {',
                               'if (era_lighting_sleep_rgb_timeout_seconds() == seconds) return true;\n    if (era_lighting_sleep_rgb_timeout_seconds() != seconds) {')
            result = execute([sys.executable, '-X', 'utf8', str(HERE/'callers.py'), str(out)],
                             failure=('Assertion' if mutation else None), env={**os.environ, 'ERA_FIXTURE_ROOT': str(root)})
            # A negative control counts only after its production fixture compiled.
            self.assertIn('compile return 0', result.stdout)

    def test_custom_caller_failures_and_same_value_retry(self):
        self.run_callers()

    def test_negative_save_receipt(self):
        self.run_callers('save_receipt')

    def test_negative_same_value_policy(self):
        self.run_callers('same_policy')

    def test_negative_same_value_sleep(self):
        self.run_callers('same_sleep')


if __name__ == '__main__':
    unittest.main()
