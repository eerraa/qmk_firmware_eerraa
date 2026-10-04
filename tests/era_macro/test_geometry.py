#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile real EEPROM geometry/backend and executor in separate translation units.

Physical EEPROM, output ownership and clocks are mechanical seams. Header defaults,
board config headers, encoder counts, NVM reads, macro wrappers and execution are
production code. Negative runtime controls must compile before failing.
"""
from pathlib import Path
import json
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GEOMETRY = 'quantum/nvm/eeprom/nvm_dynamic_keymap_geometry.h'


def function(path, name):
    source = (ROOT / path).read_text(encoding='utf-8')
    match = re.search(r'^\w+[ *]+' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


COMMON = r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "quantum/keycodes.h"
#include "quantum/action_output.h"
#include "quantum/era_macro.h"
typedef struct { struct { bool pressed; uint32_t time; } event; } keyrecord_t;
#include "quantum/send_string/send_string.h"
void register_code(uint8_t); void unregister_code(uint8_t);
void send_keyboard_report(void); void host_keyboard_delay(uint16_t);
uint32_t timer_read32(void);
size_t fixture_snapshot_size(void);
'''

SEAMS = r'''
#include "common.h"
#include "eeprom.h"
#include "dynamic_keymap.h"
#include "nvm_dynamic_keymap.h"
#include "nvm_dynamic_keymap_geometry.h"
#ifdef __AVR__
#undef eeprom_write_byte
#undef eeprom_write_word
#undef eeprom_write_dword
#undef eeprom_write_block
#undef eeprom_update_byte
#undef eeprom_update_word
#undef eeprom_update_dword
#undef eeprom_update_block
#endif
static uint8_t physical[TOTAL_EEPROM_BYTE_COUNT];
static unsigned bytes_read, blocks_read, a_down, b_down, b_up;
static size_t highest_read;
static size_t address(const void *p, size_t n) {
    size_t at = (uintptr_t)p;
    assert(at <= sizeof(physical) && n <= sizeof(physical) - at);
    return at;
}
uint8_t eeprom_read_byte(const uint8_t *p) {
    size_t at = address(p, 1); ++bytes_read;
    if (at + 1 > highest_read) highest_read = at + 1;
    return physical[at];
}
void eeprom_read_block(void *dst, const void *src, size_t n) {
    size_t at = address(src, n); ++blocks_read; bytes_read += n;
    if (at + n > highest_read) highest_read = at + n;
    memcpy(dst, physical + at, n);
}
void eeprom_update_byte(uint8_t *p, uint8_t v) { physical[address(p, 1)] = v; }
void eeprom_write_byte(uint8_t *p, uint8_t v) { eeprom_update_byte(p, v); }
void eeprom_update_block(const void *src, void *dst, size_t n) { memcpy(physical + address(dst, n), src, n); }
void eeprom_write_block(const void *src, void *dst, size_t n) { eeprom_update_block(src, dst, n); }
bool eeprom_persistence_ok(void) { return true; }
bool eeprom_begin_transaction(void) { return true; }
bool eeprom_end_transaction(void) { return true; }
void eeprom_reject_write(void) { assert(!"unexpected write rejection"); }
void eeprom_native_note_result(bool ok) { assert(ok); }
void register_code(uint8_t c) { if (c == KC_A) ++a_down; if (c == KC_B) ++b_down; }
void unregister_code(uint8_t c) { if (c == KC_B) ++b_up; }
void send_keyboard_report(void) {}
void host_keyboard_delay(uint16_t t) { (void)t; }
uint32_t timer_read32(void) { return 0; }
void era_macro_platform_sync(void) { era_macro_session(1, true, false); }
bool era_macro_platform_pressured(void) { return false; }
static action_output_owner_t selected = ACTION_OUTPUT_OWNER_REGULAR;
action_output_owner_t action_output_set_owner(action_output_owner_t o) { action_output_owner_t p = selected; selected = o; return p; }
action_output_owner_t action_output_get_owner(void) { return selected; }
void action_output_clear_owner_keys(action_output_owner_t o) { (void)o; }
void action_output_clear_owner_mods(action_output_owner_t o) { (void)o; }
void action_output_clear_owner_hid(action_output_owner_t o) { (void)o; }
'''

CASES = r'''
int main(void) {
    const size_t size = nvm_dynamic_keymap_macro_size();
    printf("geometry backend=%zu start=%u snapshot=%zu expected=%u\n", size, (unsigned)DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR, fixture_snapshot_size(), EXPECT_SIZE);
    fflush(stdout);
    assert(size == EXPECT_SIZE && DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR == EXPECT_START);
    assert(fixture_snapshot_size() == EXPECT_CAPACITY);
    uint8_t *macro = physical + DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR;
    memset(macro, 'a', size);
    macro[size - 1] = 0xff;
    assert(era_macro_request(0)); era_macro_task();
    assert(bytes_read == size && !a_down && !b_down);
    macro[size - 1] = 0;
    memset(macro, 'a', size - 1);
    memset(macro, 0, 15); /* Last legal ID starts after fifteen terminators. */
    macro[size - 4] = SS_QMK_PREFIX;
    macro[size - 3] = SS_DOWN_CODE;
    macro[size - 2] = KC_B;
    bytes_read = blocks_read = 0; highest_read = 0;
    assert(era_macro_request(15)); era_macro_task();
    assert(bytes_read == size && blocks_read == 1 && highest_read == DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + size);
    memset(macro, 0, size); /* In-flight execution remains the accepted snapshot. */
    for (size_t i = 0; i < size; ++i) era_macro_task();
    assert(a_down && b_down == 1 && !b_up && bytes_read == size);
    uint8_t end[3] = {9, 9, 9};
    nvm_dynamic_keymap_macro_read_buffer(size - 1, sizeof(end), end);
    assert(!end[0] && !end[1] && !end[2] && bytes_read == size + 1);
    assert(!era_macro_request(DYNAMIC_KEYMAP_MACRO_COUNT));
    era_macro_cancel();
    puts("last ID/full-region copy/final command/public end boundary PASS");
}
'''

AVR_HEADER = r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
uint8_t eeprom_read_byte(const uint8_t *);
uint16_t eeprom_read_word(const uint16_t *);
uint32_t eeprom_read_dword(const uint32_t *);
void eeprom_read_block(void *, const void *, size_t);
void eeprom_write_byte(uint8_t *, uint8_t);
void eeprom_write_word(uint16_t *, uint16_t);
void eeprom_write_dword(uint32_t *, uint32_t);
void eeprom_write_block(const void *, void *, size_t);
void eeprom_update_byte(uint8_t *, uint8_t);
void eeprom_update_word(uint16_t *, uint16_t);
void eeprom_update_dword(uint32_t *, uint32_t);
void eeprom_update_block(const void *, void *, size_t);
static inline void eeprom_busy_wait(void) {}
'''


def build(out, defines='', size=3863, start=233, board=None, mutation=None, feature_off=False, invalid=None, capacity=None, cflags=(), macro_source=None):
    out.mkdir(parents=True, exist_ok=True)

    def write(name, text):
        dest = out / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text, encoding='utf-8')

    config = '#define MATRIX_ROWS 4\n#define MATRIX_COLS 8\n#define DYNAMIC_KEYMAP_LAYER_COUNT 3\n#define VIA_ENABLE\n#define NVM_DRIVER_EEPROM\n#define EEPROM_WEAR_LEVELING\n#define DYNAMIC_KEYMAP_MACRO_DELAY 0\n#define TAP_CODE_DELAY 0\n#define TAP_HOLD_CAPS_DELAY 80\n'
    if not feature_off:
        config += '#define ERA_MACRO_ENABLE\n'
    if board:
        j = json.loads((ROOT / f'keyboards/era/{board}/keyboard.json').read_text(encoding='utf-8'))
        rows = len(j['matrix_pins']['rows']) * (2 if board == 'sirind/tomak' else 1)
        cols = len(j['matrix_pins']['cols'])
        config += f'#undef MATRIX_ROWS\n#define MATRIX_ROWS {rows}\n#undef MATRIX_COLS\n#define MATRIX_COLS {cols}\n#undef DYNAMIC_KEYMAP_LAYER_COUNT\n#define DYNAMIC_KEYMAP_LAYER_COUNT {j["dynamic_keymap"]["layer_count"]}\n#include "keyboards/era/{board}/config.h"\n'
        if board == 'sirind/tomak':
            config += '#undef EEPROM_WEAR_LEVELING\n#define EEPROM_CUSTOM\n#include "keyboards/era/common/storage/era_storage_adoption.h"\n'
    config += defines + f'\n#define EXPECT_SIZE {size}\n#define EXPECT_START {start}\n#define EXPECT_CAPACITY {size if capacity is None else capacity}\n'
    write('config.h', config)
    write('common.h', COMMON)
    for name in ['action.h', 'action_util.h', 'timer.h', 'host.h', 'send_string.h']:
        write(name, '#include "common.h"\n')
    write('eeprom.h', '#pragma once\n#include "platforms/eeprom.h"\n')
    write('wear_leveling_drivers.h', '#pragma once\n#include "platforms/chibios/drivers/wear_leveling/wear_leveling_rp2040_flash_config.h"\n')
    write('hardware/flash.h', '#pragma once\n')
    write('eeconfig.h', '#pragma once\n#ifndef EECONFIG_KB_DATA_SIZE\n#define EECONFIG_KB_DATA_SIZE 0\n#endif\n#ifndef EECONFIG_USER_DATA_SIZE\n#define EECONFIG_USER_DATA_SIZE 0\n#endif\n')
    write('gpio.h', '#pragma once\n#include <stdint.h>\ntypedef uint8_t pin_t;\n')
    write('progmem.h', '#pragma once\n#define PROGMEM\n#define pgm_read_byte(p) (*(const uint8_t *)(p))\n')
    write('avr/eeprom.h', AVR_HEADER)
    # Same production VIA header, with its unrelated action dependency mocked.
    write('via.h', (ROOT / 'quantum/via.h').read_text(encoding='utf-8'))
    macro = macro_source if macro_source is not None else (ROOT / 'quantum/era_macro.c').read_text(encoding='utf-8')
    if mutation == 'fallback':
        begin, end = macro.index('#ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE'), macro.index('_Static_assert')
        macro = macro[:begin] + '#ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE\n#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE TOTAL_EEPROM_BYTE_COUNT\n#endif\n' + macro[end:]
    write('macro.c', macro + '\nsize_t fixture_snapshot_size(void) { return sizeof(snapshot); }\n')
    lut = (ROOT / 'quantum/send_string/send_string.c').read_text(encoding='utf-8')
    lut = lut[lut.index('__attribute__((weak)) const uint8_t'):lut.index('// clang-format on')]
    wrappers = '\n'.join(function('quantum/dynamic_keymap.c', name) for name in ['dynamic_keymap_macro_get_count', 'dynamic_keymap_macro_get_buffer_size', 'dynamic_keymap_macro_get_buffer'])
    body = SEAMS + lut + wrappers + CASES
    if feature_off:
        body = body[:body.index('int main(void)')] + '\nsize_t fixture_snapshot_size(void) { return 0; }\nint main(void) { assert(nvm_dynamic_keymap_macro_size() == EXPECT_SIZE); assert(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR == EXPECT_START); puts("feature-off backend geometry PASS"); }\n'
        # The no-executor build has no session owner to call.
        body = body.replace('void era_macro_platform_sync(void) { era_macro_session(1, true, false); }', 'void era_macro_platform_sync(void) {}')
    write('main.c', body)
    executable = out / 'geometry'
    cmd = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-int-to-pointer-cast', *cflags, '-ffunction-sections', '-fdata-sections', '-include', str(out / 'config.h'), '-I' + str(out), '-I' + str(ROOT), '-I' + str(ROOT / 'quantum'), '-I' + str(ROOT / 'quantum/nvm'), '-I' + str(ROOT / 'quantum/nvm/eeprom'), str(out / 'main.c'), str(ROOT / 'quantum/nvm/eeprom/nvm_dynamic_keymap.c')]
    if not feature_off:
        cmd.append(str(out / 'macro.c'))
    cmd += ['-Wl,--gc-sections', '-o', str(executable)]
    compiled = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    if invalid:
        if not compiled.returncode or invalid not in compiled.stderr:
            raise AssertionError('invalid geometry did not reach expected compile gate: ' + compiled.stderr)
        return compiled
    if compiled.returncode:
        raise AssertionError('fixture did not compile: ' + compiled.stderr)
    return subprocess.run([str(executable)], capture_output=True, text=True, errors='replace')


class MacroGeometry(unittest.TestCase):
    def check(self, **args):
        with tempfile.TemporaryDirectory(prefix='era-macro-geometry-') as folder:
            result = build(Path(folder), **args)
        if args.get('invalid') or args.get('mutation'):
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            if args.get('board'):
                print(args['board'] + ': ' + result.stdout.splitlines()[0], flush=True)

    def test_supported_board_geometry_and_maximum_execution(self):
        for board, size, start in [('comm/riley', 2935, 1161), ('newone/odessey60s', 2875, 1221), ('sirind/tomak', 16384, 1905)]:
            with self.subTest(board=board):
                self.check(board=board, size=size, start=start)

    def test_via_nonvia_and_config_prefix(self):
        for defines, size, start in [
                ('', 3863, 233),
                ('#undef VIA_ENABLE\n', 3867, 229),
                ('#define EECONFIG_KB_DATA_SIZE 11\n#define EECONFIG_USER_DATA_SIZE 13\n#define VIA_EEPROM_LAYOUT_OPTIONS_SIZE 2\n#define VIA_EEPROM_CUSTOM_CONFIG_SIZE 7\n', 3831, 265),
                ('#undef DYNAMIC_KEYMAP_LAYER_COUNT\n', 3799, 297),
                ('#undef VIA_ENABLE\n#define DYNAMIC_KEYMAP_EEPROM_ADDR 128\n', 3776, 320)]:
            with self.subTest(defines=defines):
                self.check(defines=defines, size=size, start=start)

    def test_encoder_counts_map_gate_and_overrides(self):
        encoder = '#define ENCODER_ENABLE\n#define ENCODER_A_PINS {1, 2}\n'
        for defines, size, start in [
                (encoder, 3863, 233),
                (encoder + '#define ENCODER_MAP_ENABLE\n', 3839, 257),
                ('#define ENCODER_ENABLE\n#define ENCODER_MAP_ENABLE\n#define SPLIT_KEYBOARD\n#define ENCODER_A_PINS {1}\n#define ENCODER_A_PINS_RIGHT {2, 3}\n', 3827, 269),
                (encoder + '#define ENCODER_MAP_ENABLE\n#define DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR 500\n', 3572, 524),
                ('#define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR 2047\n', 1815, 233),
                ('#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR 777\n', 3319, 777),
                ('#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR 777\n#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 512\n', 512, 777)]:
            with self.subTest(defines=defines):
                self.check(defines=defines, size=size, start=start)

    def test_uint16_max_snapshot_and_compile_gates(self):
        maximum = '#undef EEPROM_WEAR_LEVELING\n#define EEPROM_CUSTOM\n#define EEPROM_SIZE 65536\n#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR 1\n'
        # The unchanged uint16_t size > sizeof(snapshot) check warns when the
        # theoretical capacity is UINT16_MAX. Only this hypothetical case
        # suppresses that existing warning; supported boards keep -Werror.
        self.check(defines=maximum, size=65535, start=1, cflags=('-Wno-type-limits',))
        self.check(defines=maximum + '#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 65536\n', invalid='Macro snapshot index width')
        self.check(defines='#define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR 4096\n', invalid='configured to use more space')
        self.check(defines='#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR 3999\n', invalid='Dynamic keymaps are configured to use more EEPROM')

    def test_avr_and_feature_off_backend_geometry(self):
        self.check(defines='#undef EEPROM_WEAR_LEVELING\n#define __AVR__\n#define __AVR_ATmega32U4__\n', size=791, start=233, feature_off=True)
        self.check(feature_off=True)

    def test_custom_nvm_upper_bound_and_explicit_size(self):
        self.check(defines='#undef NVM_DRIVER_EEPROM\n#define NVM_DRIVER_CUSTOM\n', capacity=4096)
        self.check(defines='#undef NVM_DRIVER_EEPROM\n#define NVM_DRIVER_CUSTOM\n#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 512\n', size=512)

    def test_negative_restored_full_eeprom_snapshot(self):
        with tempfile.TemporaryDirectory(prefix='era-macro-geometry-control-') as folder:
            result = build(Path(folder), board='comm/riley', size=2935, start=1161, mutation='fallback')
        self.assertNotEqual(result.returncode, 0, 'restored whole-EEPROM capacity escaped runtime assertion')
        self.assertIn('backend=2935 start=1161 snapshot=4096', result.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
