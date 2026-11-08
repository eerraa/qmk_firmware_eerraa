#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Real QMK entry through the PIO recovery probe, before any storage work."""
import unittest
import subprocess
import tempfile
from pathlib import Path
from test_boot_sequence import HERE, ROOT, COMMON, function, functions, run_c
from test_matrix_startup import unit


FLAGS = ('-no-pie', '-Ikeyboards/era/common/system',
         '-DEEPROM_DRIVER', '-DFIXTURE_REAL_BOOT_CHAIN')


def early_unit(case):
    chain = (HERE / 'early_boot.c').read_text()
    chain = chain.replace('/* PLATFORM */', function('platforms/chibios/platform.c', 'platform_setup'))
    chain = chain.replace('/* STORAGE */', function(COMMON + 'storage/era_eeprom_driver.c', 'eeprom_driver_init'))
    chain = chain.replace('/* MATRIX_INIT */', function(COMMON + 'system/era_rp2040_matrix_core.c', 'matrix_init'))
    chain = chain.replace('/* BOOTMAGIC_ENTRY */', function('quantum/bootmagic/bootmagic.c', 'bootmagic'))
    chain = chain.replace('/* KEYBOARD */', functions('quantum/keyboard.c', 'keyboard_setup quantum_init keyboard_init'))
    chain = chain.replace('/* BOOTLOADER */', functions('platforms/chibios/bootloaders/rp2040.c',
        'rp2040_bootloader_double_tap_reset_disarm bootloader_jump'))
    chain = chain.replace('/* MAIN */', function('quantum/main.c', 'main').replace('int main(', 'int firmware_main(', 1))
    return unit(case).replace('/* EARLY_BOOT_CHAIN */', chain)


class EarlyBoot(unittest.TestCase):
    def test_disabled_bootmagic_keeps_normal_init(self):
        flags = (*FLAGS, '-DFIXTURE_DISABLED')
        run_c(early_unit('check_boot(3,false);'), flags)

    def test_build_placement_follows_qmk_feature_and_sampler(self):
        # Execute the actual common fragment, including post-keymap feature
        # resolution. QMK enables Bootmagic for VIA after this fragment runs.
        with tempfile.TemporaryDirectory() as tmp:
            makefile = Path(tmp) / 'fixture.mk'
            makefile.write_text('''
KEYMAP_PATH := fixture
BACKLIGHT_ENABLE := yes
RGBLIGHT_ENABLE := yes
RGB_MATRIX_ENABLE := yes
MOUSEKEY_ENABLE := yes
include keyboards/era/common/system/era_common_qmk_rules.mk
$(info EARLY=$(filter -DERA_BOOTMAGIC_EARLY,$(OPT_DEFS)))
all: ; @:
''')
            for matrix in ('yes', 'no'):
                for boot in ('yes', 'no'):
                    for via in ('yes', 'no'):
                        result = subprocess.run(['make', '-f', str(makefile),
                            f'ERA_RP2040_MATRIX_ENABLE={matrix}', f'BOOTMAGIC_ENABLE={boot}',
                            f'VIA_ENABLE={via}'], cwd=ROOT, text=True, capture_output=True)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        emitted = 'EARLY=-DERA_BOOTMAGIC_EARLY' in result.stdout
                        self.assertEqual(emitted, matrix == 'yes' and (boot == 'yes' or via == 'yes'))

    def test_recovery_and_normal_boot_across_builds(self):
        for split in (False, True):
            for via in (False, True):
                for reset in (False, True):
                    flags = FLAGS + (('-DSPLIT_KEYBOARD',) if split else ())
                    flags += ('-DVIA_ENABLE',) if via else ()
                    flags += ('-DERA_SETTINGS_RESET_IMAGE',) if reset else ()
                    with self.subTest(split=split, via=via, reset=reset):
                        run_c(early_unit('''
                            for(unsigned side=0;side<2;side++) {
                                physical_left=side==0;
                                check_boot(2,false);check_boot(2,true);
                                check_boot(3,false);check_boot(0,false);
                            }
                        '''), flags)

    def test_storage_before_probe_is_rejected(self):
        c = early_unit('check_boot(2,false);')
        start = c.index('void keyboard_setup(void)')
        c = c[:start] + c[start:].replace('bootmagic();', 'eeprom_driver_init();bootmagic();', 1)
        run_c(c, FLAGS, failure='storage_binds==0')

    def test_duplicate_late_probe_is_rejected(self):
        c = early_unit('check_boot(3,false);')
        c = c.replace('defined(BOOTMAGIC_ENABLE) && !defined(ERA_BOOTMAGIC_EARLY)',
                      'defined(BOOTMAGIC_ENABLE)', 1)
        # Changing the key after setup must never turn normal init into recovery.
        c = c.replace('static void via_init(void) {assert(mounts==1);via_inits++;}',
                      'static void via_init(void) {assert(mounts==1);via_inits++;mode=2;deliver(delivered_words+4,true);}')
        run_c(c, (*FLAGS, '-DVIA_ENABLE'), failure='storage_binds==0')


if __name__ == '__main__':
    unittest.main(verbosity=2)
