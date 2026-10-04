#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the complete mouse engine with mechanical clock and USB seams."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

HOST = r'''#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef struct {uint8_t buttons; int8_t x,y,v,h;} report_mouse_t;
void host_mouse_send(report_mouse_t *report);
static inline bool has_mouse_report_changed(report_mouse_t *a, report_mouse_t *b) {
    return a->buttons != b->buttons || a->x || a->y || a->v || a->h;
}
'''
TIMER = r'''#pragma once
#include <stdint.h>
extern uint32_t clock_ms;
static inline uint32_t timer_read32(void) {return clock_ms;}
static inline uint16_t timer_read(void) {return clock_ms;}
static inline uint32_t timer_elapsed32(uint32_t start) {return clock_ms-start;}
static inline uint16_t timer_elapsed(uint16_t start) {return (uint16_t)(clock_ms-start);}
'''
TEST = r'''
#include <assert.h>
#include <stdlib.h>
#include "mousekey.c"
uint32_t clock_ms;
static unsigned sent;
static report_mouse_t emitted;
void host_mouse_send(report_mouse_t *report) {emitted=*report; ++sent;}
static int axis(uint8_t code) {
    if (IS_MOUSEKEY_MOVE(code)) return code <= QK_MOUSE_CURSOR_DOWN ? mouse_report.y : mouse_report.x;
    return code <= QK_MOUSE_WHEEL_DOWN ? mouse_report.v : mouse_report.h;
}
int main(void) {
    const uint8_t directions[] = {QK_MOUSE_CURSOR_UP, QK_MOUSE_CURSOR_DOWN, QK_MOUSE_CURSOR_LEFT, QK_MOUSE_CURSOR_RIGHT,
                                 QK_MOUSE_WHEEL_UP, QK_MOUSE_WHEEL_DOWN, QK_MOUSE_WHEEL_LEFT, QK_MOUSE_WHEEL_RIGHT};
    for (unsigned i=0; i<sizeof(directions); ++i) {
        mousekey_clear(); clock_ms=1000;
        mousekey_on(QK_MOUSE_ACCELERATION_0); mousekey_on(QK_MOUSE_ACCELERATION_2);
        mousekey_on(QK_MOUSE_BUTTON_1);
        // Unrelated axes remain live while the selected axis changes direction.
        mousekey_on(i<2 ? QK_MOUSE_CURSOR_LEFT : QK_MOUSE_CURSOR_UP);
        mousekey_on(i<6 ? QK_MOUSE_WHEEL_LEFT : QK_MOUSE_WHEEL_UP);
        mousekey_on(directions[i]);
        const int held=axis(directions[i]); assert(held);
        mousekey_on(directions[i^1]);
        assert((axis(directions[i])>0) != (held>0));
        mousekey_repeat=11; mousekey_wheel_repeat=13;
        mousekey_send();
        const report_mouse_t before=mouse_report;
        const uint8_t accel=mousekey_accel;
        const uint16_t cursor_timer=last_timer_c, wheel_timer=last_timer_w;
        const unsigned reports=sent;
#ifdef ERA_MOUSEKEY_RUNTIME_DELTA
        const uint32_t cursor_started=era_cursor_started, wheel_started=era_wheel_started;
#endif
#ifdef MK_KINETIC_SPEED
        const uint16_t kinetic_timer=mouse_timer;
#endif
#ifdef MOUSEKEY_INERTIA
        const uint8_t frame=mousekey_frame;
        const int8_t x_velocity=mousekey_x_inertia, y_velocity=mousekey_y_inertia;
#endif
        clock_ms=1100;
        mousekey_restore_direction(directions[i]);
        assert((axis(directions[i])>0) == (held>0));
        assert(abs(axis(directions[i])) == abs(i<2 ? before.y : i<4 ? before.x : i<6 ? before.v : before.h));
        assert(mousekey_repeat==11 && mousekey_wheel_repeat==13 && mousekey_accel==accel);
        assert(last_timer_c==cursor_timer && last_timer_w==wheel_timer && sent==reports);
        assert(mouse_report.buttons==before.buttons);
        if(i>=2) assert(mouse_report.y==before.y);
        if(i<2 || i>=4) assert(mouse_report.x==before.x);
        if(i<4 || i>=6) assert(mouse_report.v==before.v);
        if(i<6) assert(mouse_report.h==before.h);
#ifdef ERA_MOUSEKEY_RUNTIME_DELTA
        assert(era_cursor_started==cursor_started && era_wheel_started==wheel_started);
#endif
#ifdef MK_KINETIC_SPEED
        assert(mouse_timer==kinetic_timer);
#endif
#ifdef MOUSEKEY_INERTIA
        assert(mousekey_frame==frame && mousekey_x_inertia==x_velocity && mousekey_y_inertia==y_velocity);
#endif
        mousekey_off(directions[i^1]);
        assert((axis(directions[i])>0)==(held>0));
        assert(mousekey_repeat==11 && mousekey_wheel_repeat==13 && mousekey_accel==accel);
        // Restoring the current winner for a hidden release changes nothing.
        const report_mouse_t restored=mouse_report;
        mousekey_restore_direction(directions[i]);
        assert(memcmp(&restored,&mouse_report,sizeof(restored))==0 && sent==reports);
    }
#ifdef ERA_MOUSEKEY_RUNTIME_DELTA
    // Cursor and wheel restoration retain elapsed ramp time and repeat cadence.
    mousekey_clear(); clock_ms=1000; mk_delay=0; mk_interval=1;
    mk_move_delta=7; mk_cursor_top=17; mk_cursor_ramp_ms=137;
    mousekey_on(QK_MOUSE_CURSOR_LEFT); mousekey_send();
    clock_ms=1068; mousekey_task(); assert(emitted.x==-11);
    mousekey_on(QK_MOUSE_CURSOR_RIGHT); mousekey_send(); assert(emitted.x==11);
    mk_delay=50; clock_ms=1100;
    mousekey_restore_direction(QK_MOUSE_CURSOR_LEFT); mousekey_off(QK_MOUSE_CURSOR_RIGHT); mousekey_send();
    assert(era_cursor_started==1000 && emitted.x==-11);
    clock_ms=1110; mousekey_task(); assert(emitted.x==-15);
    mousekey_clear(); clock_ms=2000; mk_wheel_delay=0; mk_wheel_interval=1;
    mk_wheel_delta=1; mk_wheel_top=13; mk_wheel_ramp_ms=137;
    mousekey_on(QK_MOUSE_WHEEL_UP); mousekey_send();
    clock_ms=2068; mousekey_task(); assert(emitted.v==6);
    mousekey_on(QK_MOUSE_WHEEL_DOWN); mousekey_send(); assert(emitted.v==-6);
    mk_wheel_delay=50; clock_ms=2100;
    mousekey_restore_direction(QK_MOUSE_WHEEL_UP); mousekey_off(QK_MOUSE_WHEEL_DOWN); mousekey_send();
    assert(era_wheel_started==2000 && emitted.v==6);
    clock_ms=2110; mousekey_task(); assert(emitted.v==10);
#endif
#ifdef MOUSEKEY_INERTIA
    // Preserve live velocity while restoring the surviving direction impulse.
    mousekey_clear(); clock_ms=3000;
    mousekey_on(QK_MOUSE_CURSOR_RIGHT);
    mousekey_frame=2; mousekey_x_inertia=12; mouse_report.x=31;
    mousekey_send(); const unsigned before=sent;
    mousekey_restore_direction(QK_MOUSE_CURSOR_LEFT); mousekey_off(QK_MOUSE_CURSOR_RIGHT);
    assert(mousekey_x_dir==-1 && mousekey_x_inertia==12 && mousekey_frame==2 && mouse_report.x==31);
    assert(sent==before);
    for(unsigned step=0;step<25;++step){clock_ms+=mk_interval+1; mousekey_task();}
    assert(mousekey_x_dir==-1 && mousekey_x_inertia<0 && emitted.x<0);
#endif
    return 0;
}
'''


def replace_restore(source, replacement):
    start = source.index('void mousekey_restore_direction(')
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[:start] + replacement + source[end:]


def run_engine(options, mutation=None):
    with tempfile.TemporaryDirectory(prefix='era-mouse-restore-') as directory:
        out = Path(directory)
        for name in ['mousekey.c', 'mousekey.h', 'era_mousekey_precision.h', 'keycode.h', 'keycodes.h', 'modifiers.h']:
            shutil.copy(ROOT / 'quantum' / name, out / name)
        if mutation:
            source = out / 'mousekey.c'
            source.write_text(replace_restore(source.read_text(), mutation))
        (out / 'host.h').write_text(HOST)
        (out / 'timer.h').write_text(TIMER)
        (out / 'debug.h').write_text('#define debug_mouse 0\n')
        (out / 'print.h').write_text('\n'.join(f'#define {name}(x) ((void)(x))' for name in ['print', 'print_hex8', 'print_decs', 'print_dec']))
        (out / 'test.c').write_text(TEST)
        exe = out / 'test.exe'
        compile_result = subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-DERA_MACRO_ENABLE',
                                         *['-D' + option for option in options], '-I' + str(out), str(out / 'test.c'), '-o', str(exe)],
                                        capture_output=True, text=True)
        if compile_result.returncode:
            raise AssertionError('Compilation failed: ' + compile_result.stdout + compile_result.stderr)
        return subprocess.run([str(exe)], capture_output=True, text=True)


class MouseDirectionRestore(unittest.TestCase):
    def test_complete_engine_modes(self):
        for options in [[], ['ERA_MOUSEKEY_RUNTIME_DELTA'], ['MK_COMBINED'], ['MK_KINETIC_SPEED'],
                        ['MOUSEKEY_OVERLAP_RESET'], ['MOUSEKEY_INERTIA'], ['MK_3_SPEED'], ['MK_3_SPEED', 'MK_MOMENTARY_ACCEL']]:
            with self.subTest(options=options):
                result = run_engine(options)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_removed_restore_fails_at_runtime(self):
        result = run_engine([], 'void mousekey_restore_direction(uint8_t code) { (void)code; }')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('axis(directions[i])>0', result.stderr)

    def test_repress_is_not_continuation(self):
        result = run_engine(['MOUSEKEY_OVERLAP_RESET'], 'void mousekey_restore_direction(uint8_t code) { mousekey_on(code); }')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('mousekey_repeat==11', result.stderr)


if __name__ == '__main__':
    unittest.main()
