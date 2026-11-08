#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""First-frame readiness with actual PIO sampler and Bootmagic callers."""
import unittest
from test_boot_sequence import HERE, COMMON, source, functions, run_c


def unit(case, late=False):
    sampler = source(COMMON + 'system/era_rp2040_matrix_pio.c')
    sampler = sampler[sampler.index('#ifndef MATRIX_INPUT_PRESSED_STATE'):sampler.index('void era_rp2040_matrix_pio_get_diagnostics')]
    text = (HERE / 'matrix_startup.c').read_text()
    text = text.replace('/* MATRIX_SAMPLER */', sampler)
    text = text.replace('/* BOOTMAGIC */', functions('quantum/bootmagic/bootmagic.c', 'bootmagic_should_reset bootmagic_scan') if late else '')
    return text.replace('/* CASE */', case)


class MatrixStartup(unittest.TestCase):
    def execute(self, case, split=False):
        flags = ('-DSPLIT_KEYBOARD',) if split else ()
        run_c(unit(case), flags=('-no-pie', '-Ikeyboards/era/common/system', *flags))

    def test_zero_frame_timeout_and_late_recovery(self):
        self.execute('timeout_case(0);')

    def test_partial_frame_timeout_and_late_recovery(self):
        self.execute('timeout_case(1);')

    def test_complete_frame_keeps_intentional_bootmagic(self):
        for split in (False, True):
            self.execute('''
                for(unsigned side=0;side<2;side++) {
                    physical_left=side==0;fresh(2);bootmagic_scan();
                    assert(resets==0 && jumps==1 && waits>1);
                    assert((uint32_t)(clock_register.timerawl-(UINT32_MAX-200))<11000);
                }
            ''', split)

    def test_failed_init_never_uses_stale_complete_frame(self):
        self.execute('''
            for(unsigned fail=0;fail<3;++fail) {
                fresh(2);deliver(ERA_RP2040_MATRIX_PIO_FRAME_WORDS,true);
                program_failure=fail==0;sm_failure=fail==1;
                if(fail==2)allocation_failure=true;
                era_rp2040_matrix_init_pins();bootmagic_scan();assert(resets==0 && jumps==0);
            }
        ''')

    def test_sampler_is_allocated_once(self):
        self.execute('''
            for(unsigned delivery=0;delivery<4;delivery++) {
                fresh(delivery);bootmagic_scan();
                for(unsigned i=0;i<4;i++) era_rp2040_matrix_init_pins();
                assert(allocation==2 && claims==1 && programs==1);
            }
        ''')

    def test_released_stalled_or_interrupted_hold_does_not_jump(self):
        self.execute('''
            for(unsigned delivery=3;delivery<=6;delivery++) {
                fresh(delivery);bootmagic_scan();assert(resets==0 && jumps==0);
                assert((uint32_t)(clock_register.timerawl-(UINT32_MAX-200))<11000);
                if(delivery==3) assert(waits==0);
            }
        ''')

    def test_torn_frame_cannot_authorize_recovery(self):
        self.execute('''
            fresh(2);era_rp2040_matrix_init_pins();inject_torn=true;
            bootmagic_scan();assert(resets==0 && jumps==0);
        ''')

    def test_frame_counter_and_ring_pointer_must_agree(self):
        self.execute('''
            fresh(4);era_rp2040_matrix_init_pins();
            registers[1].WRITE_ADDR+=ERA_RP2040_MATRIX_PIO_FRAME_BYTES;
            bool changed;uint32_t frame=0;
            assert(!era_rp2040_matrix_read_raw_rows(rows,&changed,&frame));
            assert(frame==0);
        ''')

    def test_late_stock_path_remains_available(self):
        run_c(unit('fresh(2);era_rp2040_matrix_init_pins();bootmagic_scan();assert(resets==1 && jumps==1);', late=True),
              flags=('-DFIXTURE_LATE', '-no-pie', '-Ikeyboards/era/common/system'))

    def test_timeout_mutation_recreates_false_frame_readiness(self):
        text = unit('timeout_case(0);')
        old = '''return false;
        }
    }
    state->first_frame_pending = false;'''
        self.assertIn(old, text)
        text = text.replace(old, old.replace('return false;', 'break;'), 1)
        run_c(text, flags=('-no-pie', '-Ikeyboards/era/common/system'),
              failure='!era_rp2040_matrix_pio_state.ready')

    def test_missing_freshness_check_is_detected(self):
        text = unit('fresh(4);bootmagic_scan();assert(jumps==0);')
        text = text.replace('valid && frame != last_frame', 'valid', 1)
        run_c(text, flags=('-no-pie', '-Ikeyboards/era/common/system'), failure='jumps==0')

    def test_reallocation_mutation_is_detected(self):
        text = unit('fresh(3);bootmagic_scan();era_rp2040_matrix_init_pins();assert(claims==1);')
        text = text.replace('if (state->initialized)', 'if (false)', 1)
        run_c(text, flags=('-no-pie', '-Ikeyboards/era/common/system'), failure='claims==1')


if __name__ == '__main__':
    unittest.main(verbosity=2)
