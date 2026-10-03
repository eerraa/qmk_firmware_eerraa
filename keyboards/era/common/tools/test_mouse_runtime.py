"""Compile the real default mouse engine with only clock/USB host shims."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
QUANTUM = ROOT / 'src/ap/modules/qmk/quantum'
if not QUANTUM.exists():
    ROOT = Path(__file__).resolve().parents[4]
    QUANTUM = ROOT / 'quantum'
gcc = shutil.which('gcc') or r'D:\baram-fw-tools_exe\arm_toolchain\mingw_gcc\bin\gcc.exe'
with tempfile.TemporaryDirectory(prefix='era-mouse-runtime-') as directory:
    out = Path(directory)
    for name in ('mousekey.c', 'mousekey.h', 'era_mousekey_precision.h'):
        shutil.copy(QUANTUM / name, out / name)
    keys = ['QK_MOUSE_CURSOR_UP', 'QK_MOUSE_CURSOR_DOWN', 'QK_MOUSE_CURSOR_LEFT', 'QK_MOUSE_CURSOR_RIGHT',
            'QK_MOUSE_WHEEL_UP', 'QK_MOUSE_WHEEL_DOWN', 'QK_MOUSE_WHEEL_LEFT', 'QK_MOUSE_WHEEL_RIGHT',
            'QK_MOUSE_BUTTON_1', 'QK_MOUSE_ACCELERATION_0', 'QK_MOUSE_ACCELERATION_1', 'QK_MOUSE_ACCELERATION_2']
    (out/'keycode.h').write_text('#pragma once\n' + '\n'.join(f'#define {key} {i+1}' for i,key in enumerate(keys)) + '''
#define IS_MOUSEKEY_MOVE(c) ((c)>=1 && (c)<=4)
#define IS_MOUSEKEY_WHEEL(c) ((c)>=5 && (c)<=8)
#define IS_MOUSEKEY_BUTTON(c) ((c)==9)
#define KC_MS_UP QK_MOUSE_CURSOR_UP
#define KC_MS_DOWN QK_MOUSE_CURSOR_DOWN
#define KC_MS_LEFT QK_MOUSE_CURSOR_LEFT
#define KC_MS_RIGHT QK_MOUSE_CURSOR_RIGHT
#define KC_MS_WH_UP QK_MOUSE_WHEEL_UP
#define KC_MS_WH_DOWN QK_MOUSE_WHEEL_DOWN
#define KC_MS_WH_LEFT QK_MOUSE_WHEEL_LEFT
#define KC_MS_WH_RIGHT QK_MOUSE_WHEEL_RIGHT
#define KC_MS_BTN1 QK_MOUSE_BUTTON_1
#define KC_MS_ACCEL0 QK_MOUSE_ACCELERATION_0
#define KC_MS_ACCEL1 QK_MOUSE_ACCELERATION_1
#define KC_MS_ACCEL2 QK_MOUSE_ACCELERATION_2
''')
    (out/'host.h').write_text('''#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
typedef struct {uint8_t buttons; int8_t x,y,v,h;} report_mouse_t;
void host_mouse_send(report_mouse_t *report);
static inline bool has_mouse_report_changed(report_mouse_t *a, report_mouse_t *b) {return memcmp(a,b,sizeof(*a)) != 0;}
''')
    (out/'timer.h').write_text('''#pragma once
#include <stdint.h>
extern uint32_t clock_ms;
static inline uint32_t timer_read32(void) {return clock_ms;}
static inline uint16_t timer_read(void) {return clock_ms;}
static inline uint32_t timer_elapsed32(uint32_t start) {return clock_ms-start;}
static inline uint16_t timer_elapsed(uint16_t start) {return (uint16_t)(clock_ms-start);}
''')
    (out/'debug.h').write_text('#define debug_mouse 0\n')
    (out/'print.h').write_text('\n'.join(f'#define {name}(x) ((void)(x))' for name in ['print','print_hex8','print_decs','print_dec']))
    (out/'test.c').write_text('''
#include <assert.h>
#include <stdio.h>
#include "mousekey.h"
#include "keycode.h"
uint32_t clock_ms;
static unsigned reports;
static report_mouse_t last;
void host_mouse_send(report_mouse_t *r) {last=*r; ++reports;}
static void tick(uint32_t time) {clock_ms=time; mousekey_task();}
int main(void) {
    mousekey_clear();
    unsigned idle=reports; tick(100); assert(reports==idle);
    mk_move_delta=7; mk_cursor_top=17; mk_cursor_ramp_ms=137;
    mk_interval=1; mk_delay=0; clock_ms=1000;
    mousekey_on(QK_MOUSE_CURSOR_RIGHT); mousekey_send(); assert(last.x==7);
    tick(1001); assert(last.x==7);
    tick(1002); assert(last.x==7);
    tick(1068); assert(last.x==11);
    tick(1137); assert(last.x==17);
    // An orthogonal key does not restart the ramp. Diagonal scaling is unchanged.
    mousekey_on(QK_MOUSE_CURSOR_DOWN); mousekey_send();
    tick(1150); assert(last.x>=11 && last.y>=11);
    mousekey_clear(); clock_ms=2000;
    mk_cursor_ramp_ms=60000; mk_move_delta=1; mk_cursor_top=127;
    mousekey_on(QK_MOUSE_CURSOR_RIGHT); mousekey_send();
    for(unsigned i=1;i<=300;i++) tick(2000+i*2);
    assert(last.x==2); // Saturated 8-bit repeat counter must not finish a long ramp.
    tick(32000); assert(last.x==64);
    tick(62000); assert(last.x==127);
    mousekey_clear(); clock_ms=70000; mk_cursor_ramp_ms=0; mk_move_delta=9;
    mousekey_on(QK_MOUSE_CURSOR_LEFT); mousekey_send(); assert(last.x==-9);
    tick(75000); assert(last.x==-9);
    mousekey_clear(); clock_ms=0xfffffff0U; mk_cursor_ramp_ms=137; mk_move_delta=7; mk_cursor_top=17;
    mousekey_on(QK_MOUSE_CURSOR_RIGHT); mousekey_send(); tick(121); assert(last.x==17);
    mousekey_clear(); clock_ms=1000; mk_wheel_delta=1; mk_wheel_top=13; mk_wheel_ramp_ms=137; mk_wheel_interval=1; mk_wheel_delay=0;
    mousekey_on(QK_MOUSE_WHEEL_UP); mousekey_send(); assert(last.v==1);
    tick(1001); assert(last.v==1);
    tick(1002); assert(last.v==1);
    tick(1068); assert(last.v==6); tick(1137); assert(last.v==13);
    mousekey_off(QK_MOUSE_WHEEL_UP); clock_ms=1200;
    mousekey_on(QK_MOUSE_WHEEL_DOWN); mousekey_send(); assert(last.v==-1);
    mousekey_clear(); idle=reports; tick(1500); assert(reports==idle);
    puts("PASS real mouse engine: integer targets, time, long ramp, wrap, restart, diagonal, idle");
}
''')
    exe=out/'test.exe'
    subprocess.run([gcc,'-std=gnu11','-Wall','-Wextra','-Werror','-DERA_MOUSEKEY_RUNTIME_DELTA',f'-I{out}',str(out/'mousekey.c'),str(out/'test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
