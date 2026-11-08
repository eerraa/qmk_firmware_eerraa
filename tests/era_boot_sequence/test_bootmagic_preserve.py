#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Link QMK's real weak Bootmagic policy with ERA's production override."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_boot_sequence import COMMON, ROOT, function, source


class BootmagicPreserve(unittest.TestCase):
    def test_held_key_enters_bootloader_without_changing_settings(self):
        owner = COMMON + 'system/era_common_features.c'
        overrides = ''.join(
            function(owner, 'bootmagic_reset_eeprom')
            for _ in re.finditer(r'^void bootmagic_reset_eeprom\s*\(', source(owner), re.M)
        )
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            header = directory / 'fixture.h'
            header.write_text('''#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SPLIT_KEYBOARD
#define BOOTMAGIC_ROW 0
#define BOOTMAGIC_COLUMN 0
#define BOOTMAGIC_ROW_RIGHT 6
#define BOOTMAGIC_COLUMN_RIGHT 0
bool is_keyboard_left(void);
uint16_t matrix_get_row(uint8_t row);
void matrix_scan(void);
void wait_ms(unsigned milliseconds);
void eeconfig_disable(void);
void bootloader_jump(void);
void bootmagic(void);
''', encoding='utf-8')
            for name in ('bootmagic.h', 'matrix.h', 'keyboard.h', 'wait.h', 'eeconfig.h', 'bootloader.h'):
                (directory / name).write_text('#include "fixture.h"\n', encoding='utf-8')
            (directory / 'era.c').write_text('#include "fixture.h"\n' + overrides, encoding='utf-8')
            (directory / 'main.c').write_text('''#include "fixture.h"
#include <assert.h>
#include <string.h>
static bool left, held;
static unsigned jumps, resets, scans;
static unsigned char settings[64], saved[64];
bool is_keyboard_left(void) { return left; }
uint16_t matrix_get_row(uint8_t row) { return held && row == (left ? 0 : 6); }
void matrix_scan(void) { scans++; }
void wait_ms(unsigned milliseconds) { (void)milliseconds; }
void eeconfig_disable(void) { resets++; memset(settings, 0, sizeof settings); }
void bootloader_jump(void) { jumps++; }
int main(void) {
    for (unsigned side = 0; side < 2; ++side) {
        left = side == 0;
        for (unsigned index = 0; index < sizeof settings; ++index) settings[index] = index + 1;
        memcpy(saved, settings, sizeof saved);
        jumps = resets = scans = 0;
        held = false;
        bootmagic();
        assert(jumps == 0 && resets == 0 && scans == 2);
        held = true;
        bootmagic();
        assert(jumps == 1 && scans == 4);
        assert(resets == 0 && memcmp(settings, saved, sizeof saved) == 0);
    }
    return 0;
}
''', encoding='utf-8')
            executable = directory / 'bootmagic'
            compiled = subprocess.run([
                'gcc', '-std=c11', '-O2', '-Werror=implicit-function-declaration',
                '-I' + str(directory), str(ROOT / 'quantum/bootmagic/bootmagic.c'),
                str(directory / 'era.c'), str(directory / 'main.c'), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
