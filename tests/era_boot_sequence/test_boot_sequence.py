#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production boot boundaries with simulated USB, EEPROM, PIO and clocks."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
COMMON = 'keyboards/era/common/'
SCHED = COMMON + 'split/era_split_transport_scheduler.c'
OWNER = COMMON + 'split/communication_core/era_split_communication_core_owner.c'
MATRIX = COMMON + 'system/era_rp2040_matrix_core.c'
BOARD = 'keyboards/era/sirind/common/tomak_common.c'


def source(path):
    text = (ROOT / path).read_text(encoding='utf-8')
    # Preserve strings while removing comments before locating declarations.
    text = re.sub(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|/\*.*?\*/|//[^\n]*',
                  lambda m: m[1] or '', text, flags=re.S)
    return re.sub(r'__attribute__\s*\(\((?:weak|always_inline|noinline)\)\)', '', text)


def function(path, name):
    text = source(path)
    match = re.search(r'^[ \t]*(?:(?:static|inline)\s+)*\w+[ \t*]+' + name +
                      r'\([^;{}]*\)\s*\{', text, re.M)
    if not match:
        raise AssertionError(name)
    depth = 1
    for end in range(match.end(), len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if not depth:
            return text[match.start():end + 1] + '\n'
    raise AssertionError(name)


def functions(path, names):
    return ''.join(function(path, name) for name in names.split())


def run_c(text, flags=(), failure=None):
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / 'boot.c'
        exe = Path(tmp) / 'boot'
        c.write_text(text, encoding='utf-8')
        compiled = subprocess.run(['gcc', '-std=c11', '-O2', '-Werror=implicit-function-declaration',
                                   '-I' + str(ROOT), *flags, str(c), '-o', str(exe)],
                                  capture_output=True, text=True)
        if compiled.returncode:
            raise AssertionError(compiled.stderr)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        if failure is None:
            if result.returncode:
                raise AssertionError(result.stderr)
        elif result.returncode == 0 or failure not in result.stderr:
            raise AssertionError('Missing runtime counterexample: ' + result.stderr)


def launch_unit():
    owner = source(OWNER)
    declarations = owner[owner.index('enum {'):owner.index('static uint32_t era_split_communication_core_owner_word(')]
    names = '''era_split_communication_core_owner_word era_split_communication_core_owner_role_word
        era_split_communication_core_owner_word_role era_split_communication_core_owner_word_owner
        era_split_communication_core_owner_word_epoch era_split_communication_core_owner_next_epoch
        era_split_communication_core_owner_init era_split_communication_core_owner_epoch
        era_split_communication_core_owner_transfer_role
        era_split_communication_core_owner_lease_is_live_core1_role
        era_split_communication_core_owner_repair_core1_role
        era_split_communication_core_owner_ensure_core1_role
        era_split_communication_core_owner_ensure_core1
        era_split_communication_core_owner_core1_role_is_live'''
    c = (HERE / 'launch.c').read_text()
    c = c.replace('/* OWNER_STATE */', declarations)
    c = c.replace('/* OWNER_FUNCTIONS */', functions(OWNER, names))
    c = c.replace('/* BACKEND */', function(COMMON + 'split/era_split_transaction_backend_rp2040.c',
                                           'era_split_transaction_backend_set_speed'))
    names = '''era_split_transport_scheduler_reset_serial_for_transport_role
        era_split_transport_scheduler_start_communication_core
        era_split_transport_scheduler_local_initiator_step
        era_split_transport_scheduler_transport_slow
        era_split_transport_scheduler_transport_step'''
    c = c.replace('/* SCHEDULER */', functions(SCHED, names))
    c = c.replace('/* MATRIX */', functions(MATRIX, 'matrix_post_scan matrix_scan'))
    c = c.replace('/* BOOTMAGIC */', functions('quantum/bootmagic/bootmagic.c', 'bootmagic_scan bootmagic'))
    c = c.replace('/* KEYBOARD */', functions('quantum/keyboard.c', 'quantum_init keyboard_init'))
    return c


def settings_unit(nonsplit=False):
    c = (HERE / 'settings.c').read_text()
    sync = COMMON + 'split/era_split_sync_policy.c'
    storage = source(COMMON + 'split/era_split_sync_storage.h')
    policy = source(sync)
    stripped = re.sub(r'^#(?:include|pragma).*$', '', storage + policy, flags=re.M)
    c = c.replace('/* SYNC_POLICY */', stripped)
    board = COMMON + 'system/era_nonsplit_board.c' if nonsplit else BOARD
    funcs = '' if nonsplit else function(board, 'tomak_init_era_owned_modules')
    funcs += functions(board, 'matrix_init_kb via_init_kb eeconfig_init_kb')
    funcs += function(MATRIX, 'matrix_init')
    return c.replace('/* BOARD_HOOKS */', funcs)


def suspend_unit():
    c = (HERE / 'suspend.c').read_text()
    boot = 'platforms/chibios/bootloaders/rp2040.c'
    c = c.replace('/* DEADLINE */', functions(boot,
        'rp2040_bootloader_double_tap_reset_disarm rp2040_bootloader_double_tap_reset_task'))
    c = c.replace('/* HOOK */', function(COMMON + 'system/era_usb_session.c', 'suspend_power_down_kb'))
    c = c.replace('/* ACTIVE */', function(COMMON + 'system/era_common_features.c', 'era_common_features_task'))
    return c.replace('/* PROTOCOL */', function('tmk_core/protocol/chibios/chibios.c', 'protocol_pre_task'))


class BootSequence(unittest.TestCase):
    def test_bootmagic_and_owner_lifetime(self):
        for flags in ([], ['-DERA_HOST_PEER_STORAGE_V1_ENABLE']):
            with self.subTest(flags=flags):
                run_c(launch_unit(), flags)

    def test_request_cannot_restore_implicit_grant(self):
        c = launch_unit()
        start = c.index('    uint32_t lease_word =', c.index('static bool  era_split_communication_core_owner_repair_core1_role'))
        end = c.index('    return era_split_communication_core_owner_transfer_role', start)
        run_c(c[:start] + c[end:], failure='early_starts == 0')

    def test_default_and_via_settings(self):
        for nonsplit in (False, True):
            for via in (False, True):
                with self.subTest(nonsplit=nonsplit, via=via):
                    flags = (['-DNONSPLIT'] if nonsplit else []) + (['-DVIA_ENABLE'] if via else [])
                    run_c(settings_unit(nonsplit), flags)

    def test_missing_default_load_is_detected(self):
        c = settings_unit()
        start = c.index('void matrix_init_kb(')
        before, after = c[:start], c[start:]
        after = after.replace('tomak_init_era_owned_modules();', '', 1)
        run_c(before + after, failure='feature_init_calls == 1')

    def test_suspended_deadline_and_resume(self):
        run_c(suspend_unit())

    def test_missing_suspend_service_is_detected(self):
        c = suspend_unit()
        start = c.index('void suspend_power_down_kb(')
        before, after = c[:start], c[start:]
        after = after.replace('rp2040_bootloader_double_tap_reset_task();', '', 1)
        run_c(before + after, failure='magic_location == 0')


if __name__ == '__main__':
    unittest.main()
