#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""First-frame readiness with actual PIO sampler and Bootmagic callers."""
import unittest
from test_boot_sequence import HERE, COMMON, source, functions, run_c


def unit(case):
    sampler = source(COMMON + 'system/era_rp2040_matrix_pio.c')
    sampler = sampler[sampler.index('#ifndef MATRIX_INPUT_PRESSED_STATE'):sampler.index('void era_rp2040_matrix_pio_get_diagnostics')]
    text = (HERE / 'matrix_startup.c').read_text()
    text = text.replace('/* MATRIX_SAMPLER */', sampler)
    text = text.replace('/* BOOTMAGIC */', functions('quantum/bootmagic/bootmagic.c', 'bootmagic_should_reset bootmagic_scan'))
    return text.replace('/* CASE */', case)


class MatrixStartup(unittest.TestCase):
    def execute(self, case):
        run_c(unit(case), flags=('-no-pie', '-Ikeyboards/era/common/system'))

    def test_zero_frame_timeout_and_late_recovery(self):
        self.execute('timeout_case(0);')

    def test_partial_frame_timeout_and_late_recovery(self):
        self.execute('timeout_case(1);')

    def test_complete_frame_keeps_intentional_bootmagic(self):
        self.execute('fresh(2);era_rp2040_matrix_init_pins();bootmagic_scan();assert(resets==1 && jumps==1);')

    def test_failed_init_never_uses_stale_complete_frame(self):
        self.execute('''
            for(unsigned fail=0;fail<3;++fail) {
                fresh(2);era_rp2040_matrix_init_pins();assert(era_rp2040_matrix_pio_state.ready);
                program_failure=fail==0;sm_failure=fail==1;
                if(fail==2){allocation_failure=true;era_rp2040_matrix_pio_state.sample_channel=NULL;}
                era_rp2040_matrix_init_pins();bootmagic_scan();assert(resets==0 && jumps==0);
            }
        ''')

    def test_reentered_timeout_ignores_previous_pressed_frame(self):
        self.execute('''
            fresh(2);era_rp2040_matrix_init_pins();mode=0;
            era_rp2040_matrix_init_pins();bootmagic_scan();
            assert(resets==0 && jumps==0 && !era_rp2040_matrix_pio_state.ready);
        ''')

    def test_timeout_mutation_recreates_false_bootmagic(self):
        text = unit('timeout_case(0);')
        old = '''return false;
        }
    }
    state->first_frame_pending = false;'''
        self.assertIn(old, text)
        text = text.replace(old, old.replace('return false;', 'break;'), 1)
        run_c(text, flags=('-no-pie', '-Ikeyboards/era/common/system'),
              failure='resets==0 && jumps==0')


if __name__ == '__main__':
    unittest.main(verbosity=2)
