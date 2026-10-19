"""VIA-only init completion through production NOR/WL and boot callers."""
from pathlib import Path
import tempfile
import unittest

from test_persistence import HERE, ROOT, execute, nor_fixture


def build(out, backend, mutation=None):
    flags = nor_fixture(out)
    flags += ['-DVIA_INIT_RECEIPT_FIXTURE']
    functions = out / 'boot_functions.inc'
    body = functions.read_text(encoding='utf-8')
    if mutation == 'via_receipt':
        before = 'return !via_init_failed && via_eeprom_is_valid();'
        assert before in body
        body = body.replace(before, 'return via_eeprom_is_valid();', 1)
    elif mutation == 'full_retry':
        start = body.index('void quantum_init(void) {')
        before = 'if (eeconfig_is_init_required())'
        assert before in body[start:]
        body = body[:start] + body[start:].replace(before, 'if (!eeconfig_is_enabled())', 1)
    elif mutation == 'via_scope':
        start = body.index('void eeconfig_init_via(void) {')
        before = 'eeprom_transaction_active() || !eeprom_begin_transaction()'
        assert before in body[start:]
        body = body[:start] + body[start:].replace(before, '!eeprom_begin_transaction()', 1)
    elif mutation == 'full_receipt':
        before = 'eeconfig_init_failed = !eeprom_end_transaction();'
        assert before in body
        body = body.replace(before, '(void)eeprom_end_transaction(); eeconfig_init_failed = false;', 1)
    functions.write_text(body, encoding='utf-8')
    common = (out / 'boot.c').read_text(encoding='utf-8')
    # The hook belongs to a modeled optional payload owner, after the real macro
    # default function returns. The following physical fault is in production
    # nvm_via_update_magic/adapter/engine, never a replacement initializer.
    common = common.replace('void dynamic_keymap_macro_reset(void) {',
                            'static void fixture_after_macro_reset(void);\nvoid dynamic_keymap_macro_reset(void) {')
    common = common.replace('macro_calls++;nvm_dynamic_keymap_macro_reset();',
                            'macro_calls++;nvm_dynamic_keymap_macro_reset();fixture_after_macro_reset();')
    (out / 'boot.c').write_text(common, encoding='utf-8')
    (out / 'boot_common.inc').write_text(common[common.index('#include <setjmp.h>'):common.index('int main(void) {')], encoding='utf-8')
    (out / 'via_boot.c').write_bytes((HERE / 'via_boot.c').read_bytes())
    sources = ['drivers/eeprom/eeprom_driver.c', 'keyboards/era/common/storage/era_eeprom_config_io.c',
               'keyboards/era/common/system/era_state_sync.c', 'keyboards/era/common/storage/era_reset_guard.c']
    if backend == 'wl':
        flags = [f for f in flags if f not in ('-DEEPROM_CUSTOM', '-DEEPROM_SIZE=24576')]
        flags += ['-DEEPROM_WEAR_LEVELING', '-DBACKING_STORE_WRITE_SIZE=2',
                  '-DWEAR_LEVELING_BACKING_SIZE=8192', '-DWEAR_LEVELING_LOGICAL_SIZE=4096',
                  '-I' + str(ROOT / 'quantum'), '-I' + str(ROOT / 'quantum/wear_leveling'), '-I' + str(ROOT / 'lib/fnv')]
        header = out / 'stubs/dynamic_keymap.h'
        header.write_text(header.read_text(encoding='utf-8').replace('16384U', '128U'), encoding='utf-8')
        sources += ['drivers/eeprom/eeprom_wear_leveling.c', 'quantum/wear_leveling/wear_leveling.c', 'lib/fnv/hash_64a.c']
    exe = out / 'via_boot.exe'
    execute([*flags, str(out / 'via_boot.c'), *[str(ROOT / p) for p in sources], '-o', str(exe)])
    return exe


class ViaBootCompletion(unittest.TestCase):
    def run_backend(self, backend, mutation=None):
        with tempfile.TemporaryDirectory() as temp:
            exe = build(Path(temp), backend, mutation)
            execute([str(exe)], failure=('Assertion' if mutation else None))

    def test_nor_via_only_outer_failure_preserves_config(self):
        self.run_backend('nor')

    def test_wl_final_magic_failure_blocks_normal_boot(self):
        self.run_backend('wl')

    def test_negative_via_completion_ignored(self):
        self.run_backend('wl', 'via_receipt')

    def test_negative_via_failure_expands_full_reset(self):
        self.run_backend('nor', 'full_retry')

    def test_negative_foreign_via_scope_completion(self):
        self.run_backend('nor', 'via_scope')

    def test_negative_full_outer_completion_ignored(self):
        self.run_backend('nor', 'full_receipt')


if __name__ == '__main__':
    unittest.main()
