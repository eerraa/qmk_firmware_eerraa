#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the actual standalone output-owner interface across feature boundaries."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

SOURCE = r'''
#include "action_output.h"
#ifdef __cplusplus
#define CHECK static_assert
#else
#define CHECK _Static_assert
#endif
CHECK(sizeof(action_output_owner_t) == sizeof(uint8_t), "owner token width changed");
CHECK(ACTION_OUTPUT_OWNER_REGULAR == UINT8_MAX, "ordinary QMK token changed");
CHECK(ACTION_OUTPUT_TAP_DANCE_COUNT == EXPECT_TD, "TD capacity/override not preserved");
CHECK(ACTION_OUTPUT_OWNERS == EXPECT_TOTAL, "synthetic capacity changed");
CHECK(ACTION_OUTPUT_OWNERS < ACTION_OUTPUT_OWNER_REGULAR, "synthetic range overlaps regular");
#if EXPECT_TOTAL
#ifndef ACTION_OWNED_OUTPUTS
#error output ownership must be enabled
#endif
#ifdef __cplusplus
/* A prior C++ declaration would conflict with these C-linkage declarations. */
extern "C" {
#endif
action_output_owner_t action_output_set_owner(action_output_owner_t);
action_output_owner_t action_output_get_owner(void);
void action_output_clear_owner_keys(action_output_owner_t);
void action_output_clear_owner_mods(action_output_owner_t);
void action_output_clear_owner_hid(action_output_owner_t);
void action_output_reset_keys(void);
void action_output_reset_hid(void);
bool action_output_mouse_update(uint8_t, bool);
bool action_output_usage_update(uint8_t, uint16_t, uint16_t, bool);
#ifdef __cplusplus
}
#endif
#else
#ifdef ACTION_OWNED_OUTPUTS
#error disabled features unexpectedly enable owner storage
#endif
#endif
#if EXPECT_TD
#ifndef TAP_DANCE_OWNED_ACTIONS
#error TD adapter gate missing
#endif
#else
#ifdef TAP_DANCE_OWNED_ACTIONS
#error macro-only/feature-off configuration exposes TD adapter
#endif
#endif
#ifdef ERA_MACRO_ENABLE
CHECK(ACTION_OUTPUT_OWNER_MACRO_HOLD == EXPECT_TD, "macro hold overlaps TD slots");
CHECK(ACTION_OUTPUT_OWNER_MACRO_TAP == EXPECT_TD + 1, "macro tap overlaps hold slot");
CHECK(ACTION_OUTPUT_OWNER_MACRO_TAP < ACTION_OUTPUT_OWNERS, "macro slot outside storage");
#ifdef __cplusplus
extern "C" {
#endif
uint16_t action_output_usage_remaining(uint8_t);
#ifdef __cplusplus
}
#endif
#else
#ifdef ACTION_OUTPUT_OWNER_MACRO_HOLD
#error feature-off configuration exposes macro slot
#endif
#ifdef ACTION_OUTPUT_OWNER_MACRO_TAP
#error feature-off configuration exposes temporary macro slot
#endif
#endif
int main(void) { return 0; }
'''

class OutputHeaderBoundary(unittest.TestCase):
    def test_c_and_cpp_feature_registration(self):
        with tempfile.TemporaryDirectory(prefix='era-output-boundary-') as folder:
            folder = Path(folder)
            for language, compiler, standard, suffix in [
                    ('C', 'gcc', 'c11', '.c'), ('C++', 'g++', 'c++17', '.cpp')]:
                source = folder / ('boundary' + suffix)
                source.write_text(SOURCE)
                for td, macro in [(False, False), (True, False), (False, True), (True, True)]:
                    for override in (None, 3):
                        with self.subTest(language=language, td=td, macro=macro, capacity=override):
                            count = (override if override is not None else 80) if td else 0
                            defines = ['-DMATRIX_ROWS=12', '-DMATRIX_COLS=6',
                                       '-DEXPECT_TD=' + str(count),
                                       '-DEXPECT_TOTAL=' + str(count + (2 if macro else 0))]
                            if td:
                                defines.append('-DERA_TAP_DANCE_ENABLE')
                            if macro:
                                defines.append('-DERA_MACRO_ENABLE')
                            if override is not None:
                                defines.append('-DTAP_DANCE_MAX_SIMULTANEOUS=' + str(override))
                            result = subprocess.run(
                                [compiler, '-std=' + standard, '-Wall', '-Wextra', '-Werror',
                                 '-fsyntax-only', '-I' + str(ROOT / 'quantum'), *defines, str(source)],
                                capture_output=True, text=True)
                            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

if __name__ == '__main__':
    unittest.main(verbosity=2)
