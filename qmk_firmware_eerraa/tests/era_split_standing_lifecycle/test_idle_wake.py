#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production standing caller and backend alarm; hardware/time/WFE are simulated."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
COMMON='keyboards/era/common/'


def stripped(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)


def function(text, name):
    clean = stripped(text)
    found = re.search(r'^[ \t]*(?:static[ \t]+)?(?:inline[ \t]+)?[A-Za-z_]\w*[ \t*]+' +
                      re.escape(name) + r'\([^;{}]*\)\s*\{', clean, re.M)
    if not found:
        raise RuntimeError('Missing production function ' + name)
    depth = 1
    for end in range(found.end(), len(clean)):
        depth += (clean[end] == '{') - (clean[end] == '}')
        if not depth:
            return clean[found.start():end + 1]
    raise RuntimeError('Unclosed production function ' + name)


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')


def table(root):
    text = (root / (COMMON + 'split/era_split_keyboard.c')).read_text(encoding='utf-8')
    start = text.index('const era_split_restart_act_rules_t era_split_restart_act_rules[')
    return '#define ERA_SPLIT_LINK_LEVEL_LOW 2\n' + text[start:text.index('\n};', start) + 3]


def compile_and_run(cc, root, unit, extra=(), opts=()):
    with tempfile.TemporaryDirectory(prefix='era-idle-wake-') as directory:
        dest=Path(directory)
        write(dest/'matrix.h', '#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n#define MATRIX_ROWS 12\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\ntypedef uint16_t matrix_row_t;\n')
        write(dest/'timer.h', '#pragma once\n#include <stdint.h>\nuint32_t timer_read32(void);\nuint32_t timer_elapsed32(uint32_t);\n')
        write(dest/'sync_timer.h', '#pragma once\n#include <stdint.h>\nuint32_t sync_timer_read32(void);\n')
        write(dest/'qmk_stub.h', '#pragma once\n#include <stdbool.h>\n#include <stdint.h>\nvoid soft_reset_keyboard(void);\n')
        write(dest/'hardware/structs/timer.h', '#pragma once\n#include <stdint.h>\ntypedef struct { volatile uint32_t timerawl, intr, inte, alarm[4]; } timer_hw_t;\nextern timer_hw_t *timer_hw;\n')
        source=dest/'probe.c';source.write_text(unit)
        output=dest/'probe'
        includes=[dest,root,root/'tmk_core/protocol',root/'tests/era_split_standing_lifecycle',root/'tests/era_split_standing_lifecycle/include']
        args=[cc,'-std=gnu11','-Wall','-Wextra','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
              *['-I'+str(i) for i in includes],'-DQMK_KEYBOARD_H="qmk_stub.h"','-include','stddef.h',*opts,str(source),*map(str,extra),'-o',str(output)]
        compiled=subprocess.run(args,capture_output=True,text=True)
        if compiled.returncode: raise AssertionError(compiled.stderr)
        return subprocess.run([str(output)],capture_output=True,text=True)


def idle(cc, root, mutate=False):
    backend = (root / (COMMON + 'split/era_split_transaction_backend_rp2040.c')).read_text(encoding='utf-8')
    names = ['deadline_expired', 'disarm_core1_deadline', 'arm_core1_deadline', 'arm_core1_idle_wake']
    functions = '\n'.join(function(backend, 'era_split_transaction_backend_' + name) for name in names)
    functions = functions.replace('void era_split_transaction_backend_arm_core1_idle_wake(',
                                  'void production_arm_core1_idle_wake(')
    if mutate:
        original = function(backend, 'era_split_transaction_backend_arm_core1_idle_wake').replace(
            'void era_split_transaction_backend_arm_core1_idle_wake(', 'void production_arm_core1_idle_wake(')
        functions = functions.replace(original,
            'void production_arm_core1_idle_wake(uint32_t deadline_us) { era_split_transaction_backend_arm_core1_deadline(deadline_us); }')
    fixture = (root / 'tests/era_split_standing_lifecycle/standing_under_test.c').read_text(encoding='utf-8')
    fixture = fixture.replace('#define __SEV() ((void)0)', '#define __SEV() record_sev()')
    noop = 'void era_split_transaction_backend_arm_core1_idle_wake(uint32_t deadline_us) { (void)deadline_us; }'
    if noop not in fixture:
        raise RuntimeError('Fixture idle seam changed; inspect before using this probe')
    fixture = fixture.replace(noop, '''void era_split_transaction_backend_arm_core1_idle_wake(uint32_t deadline_us) {
        ++idle_arms;
        if (inject_arm) timer_hw->timerawl = injected_now;
        production_arm_core1_idle_wake(deadline_us);
    }''')
    preamble = r'''
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "hardware/structs/timer.h"
#define ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_MASK 8u
#define ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_ALARM 3u
#define ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_IRQ 3u
typedef unsigned IRQn_Type;
static unsigned events, idle_arms;
static bool inject_arm;
static uint32_t injected_now;
static void record_sev(void) { ++events; }
#define __SEV() record_sev()
#define __DMB() __sync_synchronize()
static void hw_clear_bits(volatile uint32_t *reg, uint32_t bits) { *reg &= ~bits; }
static void hw_set_bits(volatile uint32_t *reg, uint32_t bits) { *reg |= bits; }
static void NVIC_DisableIRQ(IRQn_Type n) { (void)n; }
static void NVIC_EnableIRQ(IRQn_Type n) { (void)n; }
'''
    unit = preamble + functions + '\n' + fixture + '\n#include "' + COMMON + 'split/era_split_restart_agreement.h"\n'
    unit += table(root) + r'''
static uint32_t clock_ms;
uint32_t timer_read32(void) { return clock_ms; }
uint32_t timer_elapsed32(uint32_t since) { return clock_ms - since; }
uint32_t sync_timer_read32(void) { return clock_ms; }
uint32_t era_split_input_clock(void) { return timer_hw->timerawl; }
bool era_split_restart_prepare_local(era_split_restart_act_t act,uint8_t param) { (void)act;(void)param;return true; }
bool era_split_restart_arm_ready(era_split_restart_act_t act) { (void)act;return true; }
void soft_reset_keyboard(void) {}
int main(void) {
    unsigned bad=0;
    const uint32_t starts[]={1000, UINT32_MAX-5000u};
    for(unsigned s=0;s<2;++s) {
        for(int offset=-1;offset<=1;++offset) {
            inject_arm=false;
            era_test_standing_reset();
            era_test_standing_mode(false,10);
            assert(era_test_standing_at(starts[s]));
            uint32_t due=starts[s]+10000u;
            events=idle_arms=0;
            inject_arm=true;
            injected_now=due+(uint32_t)offset;
            assert(!era_test_standing_at(due-2u));
            assert(idle_arms==1 && timer_hw->alarm[3]==due);
            bool expired=era_split_transaction_backend_deadline_expired(due);
            bool has_progress_wake=!expired || events!=0;
            printf("IDLE start=%u arm_at=%u due=%u expired=%u SEV=%u progress_wake=%u\n",
                   starts[s],injected_now,due,expired,events,has_progress_wake);
            if(!has_progress_wake) ++bad;
        }
    }
    puts("No external SEV/PIO IRQ was injected. Future arms may wake at the next comparator crossing; expired arms require an explicit event. WFE/electrical timing NOT MEASURED.");
    return bad?1:0;
}
'''
    files = [root / (COMMON + 'split/' + name) for name in
             ['era_split_input.c', 'era_split_matrix_frame.c', 'era_split_restart_agreement.c']]
    return compile_and_run(cc, root, unit, files)


class IdleWake(unittest.TestCase):
    def test_deadline_crossing_and_wrap(self):
        result=idle('cc',Path(__file__).resolve().parents[2])
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual(result.stdout.count('progress_wake=1'),6)

    def test_missing_post_arm_check_is_detected(self):
        result=idle('cc',Path(__file__).resolve().parents[2],mutate=True)
        self.assertEqual(result.returncode,1,result.stdout+result.stderr)
        self.assertEqual(result.stdout.count('progress_wake=0'),4)


if __name__=='__main__':
    unittest.main(verbosity=2)
