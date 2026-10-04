"""Macro upload session proof at production raw VIA/native/WL boundaries."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
GEOMETRIES = [('odessey', 5, 15, 6, 2875, 'wl'), ('riley', 5, 14, 6, 2935, 'wl'), ('brick', 5, 16, 4, 343, 'avr')]


def extract(path, name, receipts):
    source = path.read_text(encoding='utf-8')
    match = re.search(r'^[ \t]*(?:(?:static|inline)\s+)*(?:__attribute__\(\(weak\)\)\s+)?\w+[ \t*]+' + re.escape(name) + r'\([^;{}]*\)\s*\{', source, re.M)
    if not match:
        raise AssertionError((path, name))
    depth = 1
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[match.end():], re.S):
        if token[0] == '{':
            depth += 1
        elif token[0] == '}':
            depth -= 1
        if depth == 0:
            end = match.end() + token.end()
            text = source[match.start():end] + '\n'
            receipts.append(dict(path=str(path), function=name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                                 extracted_sha256=hashlib.sha256(text.encode()).hexdigest(),
                                 lines=[source[:match.start()].count('\n') + 1, source[:end].count('\n') + 1]))
            return text
    raise AssertionError(name)


def build(out, geometry, mutation=None):
    name, rows, columns, layers, size, backend = geometry
    out.mkdir(parents=True, exist_ok=True)
    stub = out / 'stub'
    stub.mkdir(exist_ok=True)
    receipts = []

    def write(path, text):
        dest = out / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text, encoding='utf-8')

    write('stub/quantum.h', '#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n#include "eeprom.h"\nuint16_t timer_read(void);\nuint32_t timer_read32(void);\nvoid eeconfig_init(void);\nbool via_command_kb(uint8_t *,uint8_t);\n')
    write('stub/action.h', '#pragma once\n#include "quantum.h"\n')
    with (stub / 'quantum.h').open('a', encoding='utf-8') as header:
        header.write('typedef struct { struct { bool pressed; uint32_t time; } event; } keyrecord_t;\n')
    write('stub/util.h', '#pragma once\n#define PACKED __attribute__((packed))\n')
    write('stub/eeprom.h', '#pragma once\n#include "platforms/eeprom.h"\n')
    write('stub/compiler_support.h', '#pragma once\n#define STATIC_ASSERT _Static_assert\n#define PACKED __attribute__((packed))\n')
    write('stub/keycodes.h', '#pragma once\n#define KC_NO 0\n')
    write('stub/keycode_config.h', '#pragma once\n#include <stdint.h>\ntypedef struct { uint16_t raw; } keymap_config_t;\n')
    write('stub/eeconfig.h', '#pragma once\n#include "quantum.h"\n#define EECONFIG_KB_DATA_SIZE 0\n#define EECONFIG_USER_DATA_SIZE 0\n')
    write('stub/raw_hid.h', '#pragma once\n#include <stdint.h>\nvoid raw_hid_send(uint8_t *,uint8_t);\n')
    write('stub/matrix.h', '#pragma once\n#include <stdint.h>\ntypedef uint16_t matrix_row_t;\n')
    for path in ['quantum/via.h', 'quantum/dynamic_keymap.h']:
        raw = (ROOT / path).read_bytes()
        (stub / Path(path).name).write_bytes(raw)
        receipts.append(dict(path=path, sha256=hashlib.sha256(raw).hexdigest(), scope='exact production header copy'))
    write('config.h', f'#define MATRIX_ROWS {rows}\n#define MATRIX_COLS {columns}\n#define DYNAMIC_KEYMAP_LAYER_COUNT {layers}\n#define DYNAMIC_KEYMAP_MACRO_COUNT 16\n#define EXPECTED_SIZE {size}\n#define EECONFIG_KB_DATA_SIZE 0\n#define EECONFIG_USER_DATA_SIZE 0\n' +
          ('#define VIA_EEPROM_MAGIC_ADDR 317\n#define DYNAMIC_KEYMAP_EEPROM_ADDR 321\n' if backend == 'wl' else '#define VIA_EEPROM_MAGIC_ADDR 37\n#define DYNAMIC_KEYMAP_EEPROM_ADDR 41\n') +
          f'#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR {4096 - size if backend == "wl" else 1024 - size}\n')
    dynamic = '#include "dynamic_keymap.h"\n#include "nvm_dynamic_keymap.h"\nuint16_t keycode_at_keymap_location_raw(uint8_t,uint8_t,uint8_t);\n'
    for function in 'dynamic_keymap_get_layer_count dynamic_keymap_get_keycode dynamic_keymap_set_keycode dynamic_keymap_reset dynamic_keymap_get_buffer dynamic_keymap_set_buffer dynamic_keymap_macro_get_count dynamic_keymap_macro_get_buffer_size dynamic_keymap_macro_get_buffer dynamic_keymap_macro_set_buffer dynamic_keymap_macro_reset'.split():
        dynamic += extract(ROOT / 'quantum/dynamic_keymap.c', function, receipts)
    write('dynamic-owner.inc', dynamic)
    via = '#include "keyboards/era/common/storage/era_via_magic.h"\n'
    for function in 'via_eeprom_is_valid via_eeprom_set_valid via_get_layout_options via_set_layout_options_kb via_set_layout_options via_custom_value_command raw_hid_receive'.split():
        via += extract(ROOT / 'quantum/via.c', function, receipts)
    write('via-owner.inc', via)

    macro_path = ROOT / 'quantum/nvm/eeprom/nvm_dynamic_keymap.c'
    macro = macro_path.read_text(encoding='utf-8')
    if mutation == 'cold_guard':
        for text in ['if (!opener && !dynamic_keymap_macro_upload_open) { eeprom_reject_write(); return; }',
                     'if (has_marker && !opener && !payload && !dynamic_keymap_macro_payload_seen) { eeprom_reject_write(); return; }']:
            assert text in macro
            macro = macro.replace(text, '', 1)
    elif mutation == 'opener_proof':
        text = 'if (has_marker && !opener && !payload && !dynamic_keymap_macro_payload_seen) { eeprom_reject_write(); return; }'
        assert text in macro
        macro = macro.replace(text, '', 1)
    elif mutation == 'native_owner_failure':
        text = 'if (!opener && dynamic_keymap_macro_upload_failed) { eeprom_reject_write(); return; }'
        assert text in macro
        macro = macro.replace(text, '', 1)
    elif mutation == 'skip_payload':
        text = '            eeprom_update_byte(target, *source);'
        assert text in macro
        # Keep opener/CLOSE writes intact so the full-image cold readback, not
        # a missing marker transition, detects the absent payload operation.
        macro = macro.replace(text, '            if (offset + i == DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1U) eeprom_update_byte(target, *source);', 1)
    elif mutation == 'repaired_close':
        text = 'if (!opener && payload && eeprom_read_byte(marker) == 0U) { eeprom_reject_write(); return; }'
        assert text in macro
        macro = macro.replace(text, '', 1)
    elif mutation == 'combined_opener':
        text = '    if (opener) eeprom_update_byte(marker, 0xFFU);'
        assert text in macro
        macro = macro.replace(text, '', 1)
    write('macro-owner.c', macro)
    receipts.append(dict(path=str(macro_path), sha256=hashlib.sha256(macro_path.read_bytes()).hexdigest(),
                         compiled_sha256=hashlib.sha256((out / 'macro-owner.c').read_bytes()).hexdigest(),
                         scope='complete production module; intentional mutation only when recorded', mutation=mutation))
    direct = ['quantum/nvm/eeprom/nvm_via.c']
    includes = ['-I' + str(stub), '-I' + str(out), *['-I' + str(ROOT / p) for p in ['', 'platforms', 'drivers/eeprom', 'quantum', 'quantum/nvm', 'quantum/nvm/eeprom', 'quantum/wear_leveling', 'lib/fnv']]]
    flags = ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror=implicit-function-declaration', '-DVIA_ENABLE', '-include', str(out / 'config.h')]
    if backend == 'wl':
        flags += ['-DEEPROM_WEAR_LEVELING', '-DERA_STATE_SYNC_TEST', '-DBACKING_STORE_WRITE_SIZE=2', '-DWEAR_LEVELING_BACKING_SIZE=8192', '-DWEAR_LEVELING_LOGICAL_SIZE=4096']
        # Link ERA's real strong observers/receipt instead of relying on the
        # MinGW COFF weak fallback for references from a separate adapter TU.
        direct += ['drivers/eeprom/eeprom_driver.c', 'drivers/eeprom/eeprom_wear_leveling.c',
                   'keyboards/era/common/system/era_state_sync.c', 'lib/fnv/hash_64a.c']
        engine = '#include <assert.h>\n#include "' + (ROOT / 'quantum/wear_leveling/wear_leveling.c').as_posix() + '"\n'
        engine += '''uint8_t macro_test_cold_byte(uint32_t address) {
    unsigned char saved[sizeof(wear_leveling)]; memcpy(saved, &wear_leveling, sizeof(saved));
    bool pending = wear_leveling_pending; uint8_t v = 0;
    assert(wear_leveling_init() != WEAR_LEVELING_FAILED); assert(wear_leveling_read(address, &v, 1) != WEAR_LEVELING_FAILED);
    memcpy(&wear_leveling, saved, sizeof(saved)); wear_leveling_pending = pending; return v;
}
'''
        write('wl-engine.c', engine)
    else:
        flags += ['-D__AVR__', '-D__AVR_ATmega32U4__']
        ee = (ROOT / 'quantum/eeconfig.c').read_text(encoding='utf-8')
        start = ee.index('static uint16_t eeprom_native_depth;')
        end = ee.index('\n#else', start)
        write('native-receipt.inc', ee[start:end] + '\n')
        receipts.append(dict(path='quantum/eeconfig.c', sha256=hashlib.sha256((ROOT / 'quantum/eeconfig.c').read_bytes()).hexdigest(),
                             scope='complete native receipt declarations and function block', extracted_sha256=hashlib.sha256(ee[start:end].encode()).hexdigest()))
        write('stub/avr/eeprom.h', '''#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
extern uint8_t native_storage[1024];
void macro_test_native_write(void *, const void *, size_t, bool);
static inline void eeprom_busy_wait(void) {}
static inline uint8_t eeprom_read_byte(const uint8_t *p) { return native_storage[(uintptr_t)p]; }
static inline uint16_t eeprom_read_word(const uint16_t *p) { uint16_t v; memcpy(&v, native_storage + (uintptr_t)p, 2); return v; }
static inline uint32_t eeprom_read_dword(const uint32_t *p) { uint32_t v; memcpy(&v, native_storage + (uintptr_t)p, 4); return v; }
static inline void eeprom_read_block(void *d, const void *p, size_t n) { memcpy(d, native_storage + (uintptr_t)p, n); }
#define NATIVE_LIBC(suffix,type) \\
static inline void eeprom_write_##suffix(type *p, type v) { macro_test_native_write(p, &v, sizeof(v), false); } \\
static inline void eeprom_update_##suffix(type *p, type v) { macro_test_native_write(p, &v, sizeof(v), true); }
NATIVE_LIBC(byte,uint8_t) NATIVE_LIBC(word,uint16_t) NATIVE_LIBC(dword,uint32_t)
static inline void eeprom_write_block(const void *d, void *p, size_t n) { macro_test_native_write(p, d, n, false); }
static inline void eeprom_update_block(const void *d, void *p, size_t n) { macro_test_native_write(p, d, n, true); }
''')
        if mutation == 'native_readback':
            header = (ROOT / 'platforms/eeprom.h').read_text(encoding='utf-8')
            text = 'eeprom_native_note_result(eeprom_read_##suffix(p) == value);'
            assert text in header
            write('platforms/eeprom.h', header.replace(text, 'eeprom_native_note_result(true);'))

    shutil.copyfile(HERE / 'macro_remount.c', out / 'fixture.c')
    gcc = os.environ.get('CC', shutil.which('gcc') or 'gcc')
    exe = out / 'macro.exe'
    command = [gcc, *flags, *includes, str(out / 'fixture.c'), *[str(ROOT / p) for p in direct], *([str(out / 'wl-engine.c')] if backend == 'wl' else []), '-o', str(exe)]
    for path in direct + ['platforms/eeprom.h', 'quantum/wear_leveling/wear_leveling.c']:
        receipts.append(dict(path=path, sha256=hashlib.sha256((ROOT / path).read_bytes()).hexdigest(), scope='production source/header'))
    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace')
    (out / 'compile.log').write_text('exit=' + str(result.returncode) + '\n' + result.stdout + result.stderr, encoding='utf-8')
    (out / 'manifest.json').write_text(json.dumps(dict(geometry=geometry, flags=flags, mutation=mutation, command=command, inputs=receipts, compiler_exit=result.returncode), indent=2), encoding='utf-8')
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return exe


class MacroRemount(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = os.environ.get('ERA_MACRO_TEST_OUTPUT')
        cls.temporary = None if path else tempfile.TemporaryDirectory()
        cls.out = Path(path or cls.temporary.name)
        cls.results = []

    @classmethod
    def tearDownClass(cls):
        if cls.temporary:
            cls.temporary.cleanup()

    def execute(self, geometry, mutation=None, scenario=None):
        out = self.out / (geometry[0] + '-' + (mutation or 'production'))
        exe = build(out, geometry, mutation)
        result = subprocess.run([str(exe), *([scenario] if scenario else [])], capture_output=True, text=True, encoding='utf-8', errors='replace')
        (out / 'runtime.log').write_text('exit=' + str(result.returncode) + '\n' + result.stdout + result.stderr, encoding='utf-8')
        record = dict(geometry=geometry, mutation=mutation, scenario=scenario, compiled=True, exit=result.returncode, stdout=result.stdout, stderr=result.stderr)
        self.results.append(record)
        (self.out / 'results.json').write_text(json.dumps(self.results, indent=2), encoding='utf-8')
        if mutation:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            expected = {
                'skip_payload': 'memcmp(readback, data + offset, count) == 0',
                'native_readback': 'response[0] == id_unhandled && !eeprom_persistence_ok()',
                'cold_guard': 'response[0] == id_unhandled',
                'opener_proof': 'response[0] == id_unhandled',
                'native_owner_failure': 'response[0] == id_unhandled',
                'repaired_close': 'MACRO_ASSERT_REPAIRED_CLOSE_REJECTS_PAYLOAD',
                'combined_opener': 'MACRO_ASSERT_OPENER_INVALIDATES_BEFORE_PAYLOAD',
            }
            self.assertIn(expected[mutation], result.stderr)
            if mutation in ['repaired_close', 'combined_opener']:
                self.assertEqual(result.returncode, 91, result.stdout + result.stderr)
            else:
                self.assertIn('Assertion', result.stderr)
        else:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS actual geometry', result.stdout)

    def test_production_geometries_full_cold_and_live_transcript(self):
        for geometry in GEOMETRIES:
            with self.subTest(geometry=geometry):
                self.execute(geometry)

    def test_negative_cold_and_opener_only_guards(self):
        for geometry in GEOMETRIES:
            for mutation in ['cold_guard', 'opener_proof']:
                with self.subTest(geometry=geometry, mutation=mutation):
                    self.execute(geometry, mutation, 'cold')

    def test_negative_actual_payload_write(self):
        for geometry in GEOMETRIES:
            with self.subTest(geometry=geometry):
                self.execute(geometry, 'skip_payload', 'stock')

    def test_negative_native_owner_failure_and_readback(self):
        self.execute(GEOMETRIES[-1], 'native_owner_failure', 'live')
        self.execute(GEOMETRIES[-1], 'native_readback', 'cold')

    def test_negative_repaired_close_and_combined_opener(self):
        for geometry in GEOMETRIES[:2]:
            for mutation, scenario in [('repaired_close', 'sealed'), ('combined_opener', 'combined')]:
                with self.subTest(geometry=geometry, mutation=mutation):
                    self.execute(geometry, mutation, scenario)


if __name__ == '__main__':
    unittest.main()
