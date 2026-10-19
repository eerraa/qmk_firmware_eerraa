#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent production-executor parity, immutable snapshot and bounded-work proof.

Build the exact executor with mechanical clock/storage/output seams. The reference
uses the complete production send_char/tap functions and actual layout tables;
no fixture implements the executor. GPIO, QMK output ownership and USB timing
are deliberately left to their separate integration/device gates.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
root=Path(__file__).resolve().parents[2]

def fixture(out, interval=10, tap_delay=5, mutation=None, custom_layout=False):
    def extract(path,name):
     src=(root/path).read_text(); m=re.search(r'^(?:(?:static|inline) )?(?:__attribute__\(\(weak\)\) )?\w+[ *]+'+name+r'\([^;{}]*\)\s*\{',src,re.M); assert m,name
     n=1;p=m.end()
     while n:
      n+=(src[p]=='{')-(src[p]=='}');p+=1
     return src[m.start():p]
    ss=(root/'quantum/send_string/send_string.c').read_text(); lut=ss[ss.index('__attribute__((weak)) const uint8_t'):ss.index('// clang-format on')]
    common='''#pragma once
    #include <stdint.h>
    #include <stdbool.h>
    #include <stddef.h>
    #include <string.h>
    #include <stdio.h>
    #include <assert.h>
    #include "quantum/keycodes.h"
    #define ERA_MACRO_ENABLE
    #include "quantum/action_output.h"
    #define PROGMEM
    #define pgm_read_byte(p) (*(const uint8_t *)(p))
    #define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 512
    #define DYNAMIC_KEYMAP_MACRO_DELAY INTERVAL_VALUE
    #define TAP_CODE_DELAY TAP_VALUE
    #define TAP_HOLD_CAPS_DELAY 80
    #include "quantum/send_string/send_string.h"
    void register_code(uint8_t);void unregister_code(uint8_t);
    void host_keyboard_delay(uint16_t);void wait_ms(uint16_t);
    void tap_code(uint8_t);void tap_code_delay(uint8_t,uint16_t);void tap_code_wait(uint16_t,uint16_t);
    void send_keyboard_report(void);
    uint32_t timer_read32(void);
    uint8_t dynamic_keymap_macro_get_count(void);
    uint16_t dynamic_keymap_macro_get_buffer_size(void);
    void dynamic_keymap_macro_get_buffer(uint16_t,uint16_t,uint8_t *);
    '''
    common=common.replace('INTERVAL_VALUE',str(interval)).replace('TAP_VALUE',str(tap_delay))
    (out/'common.h').write_text(common)
    for name in ['action.h','action_util.h','dynamic_keymap.h','send_string.h','timer.h','host.h','eeprom.h']:(out/name).write_text('#include "common.h"\n')
    (out/'progmem.h').write_text('#pragma once\n#define PROGMEM\n#define pgm_read_byte(p) (*(const uint8_t *)(p))\n')
    unit='''#include "common.h"
    #include "quantum/era_macro.h"
    static uint32_t now;static unsigned events,waits,intervals,reads;
    static uint8_t memory[512];
    typedef struct {uint32_t t;uint8_t code,down;} event_t;
    static event_t trace[4096];
    void register_code(uint8_t c) {if(c) trace[events++]=(event_t){now,c,1};}
    void unregister_code(uint8_t c) {if(c) trace[events++]=(event_t){now,c,0};}
    void host_keyboard_delay(uint16_t d) {(void)d;++intervals;}
    void wait_ms(uint16_t d) {now+=d;++waits;}
    void send_keyboard_report(void) {}
    static action_output_owner_t selected_owner = ACTION_OUTPUT_OWNER_REGULAR;
    action_output_owner_t action_output_set_owner(action_output_owner_t owner) {
        action_output_owner_t previous = selected_owner;
        selected_owner = owner;
        return previous;
    }
    action_output_owner_t action_output_get_owner(void) {return selected_owner;}
    void action_output_clear_owner_keys(action_output_owner_t owner) {(void)owner;}
    void action_output_clear_owner_mods(action_output_owner_t owner) {(void)owner;}
    void action_output_clear_owner_hid(action_output_owner_t owner) {(void)owner;}
    void era_macro_platform_sync(void) {}
    bool era_macro_platform_pressured(void) {return false;}
    uint32_t timer_read32(void) {return now;}
    uint8_t dynamic_keymap_macro_get_count(void) {return 16;}
    uint16_t dynamic_keymap_macro_get_buffer_size(void) {return sizeof(memory);}
    void dynamic_keymap_macro_get_buffer(uint16_t o,uint16_t n,uint8_t*d) {assert(o+n<=sizeof(memory));memcpy(d,memory+o,n);reads+=n;}
    '''+lut+'\n#define PGM_LOADBIT(mem,pos) ((pgm_read_byte(&((mem)[(pos)/8]))>>((pos)%8))&1)\n'
    for name in ['tap_code_wait','tap_code_delay','tap_code']:unit+=extract('quantum/action.c',name)+'\n'
    unit+=extract('quantum/send_string/send_string.c','send_char_with_delay')+'\n'
    production=(root/'quantum/era_macro.c').read_text()
    if mutation == 'wait':
        assert 'waiting = delay != 0;' in production
        production=production.replace('waiting = delay != 0;', 'waiting = false;', 1)
    elif mutation == 'budget':
        assert 'budget < 16' in production
        production=production.replace('budget < 16','budget < 250',1)
    (out/'production_macro.c').write_text(production)
    unit+='#include "production_macro.c"\n'
    unit+='''
    static void reset(void) {era_macro_cancel();era_macro_session(1,true,false);now=events=waits=intervals=reads=0;memset(memory,0,sizeof(memory));}
    static void parity(unsigned c,bool command) {
     reset(); if(command){tap_code(c);wait_ms(DYNAMIC_KEYMAP_MACRO_DELAY);}else send_char_with_delay(c,DYNAMIC_KEYMAP_MACRO_DELAY);
     unsigned nr=events, ni=intervals;uint32_t end=now;event_t ref[16];memcpy(ref,trace,nr*sizeof(event_t));
     reset();if(command){memory[0]=SS_QMK_PREFIX;memory[1]=SS_TAP_CODE;memory[2]=c;}else memory[0]=c;
     assert(era_macro_request(0)); unsigned limit=0;
     while((count||active)&&limit++<1000) {era_macro_task();if(waiting)++now;}
     if(now!=end || nr!=events || ni!=intervals || waits) {printf("FAIL %s %u ref_end=%u now=%u ref_events=%u events=%u intervals=%u/%u waits=%u\\n",command?"TAP":"ASCII",c,end,now,nr,events,ni,intervals,waits);abort();}
     for(unsigned i=0;i<nr;i++) if(trace[i].t!=ref[i].t||trace[i].code!=ref[i].code||trace[i].down!=ref[i].down){printf("ORDER FAIL %u event %u\\n",c,i);abort();}
    }
    int main(void) {
     for(unsigned c=1;c<128;c++) if(c!=SS_QMK_PREFIX)parity(c,false);
     for(unsigned c=1;c<256;c++)parity(c,true);
     puts("PASS production ASCII1..127/TAP1..255 trace+timing; no wait_ms from executor");
     reset();memory[0]=SS_QMK_PREFIX;memory[1]=SS_DELAY_CODE;memcpy(memory+2,"1000|a",6);assert(era_macro_request(0));era_macro_task();assert(waiting);memory[7]='b';now=1000;while(count||active){era_macro_task();if(waiting)++now;}assert(trace[0].code==KC_A);puts("PASS active snapshot immutable across storage change");
     reset();memset(memory,'a',sizeof(memory)-1);for(unsigned i=0;i<ERA_MACRO_QUEUE_CAPACITY;i++)assert(era_macro_request(0));assert(!era_macro_request(0));era_macro_task();assert(events<16);puts("PASS finite request capacity and one task work budget");
     return 0;
    }
    '''
    (out/'review.c').write_text(unit)
    cmd=['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-include','stdlib.h','-I'+str(out),'-I'+str(root),'-I'+str(root/'quantum'),str(out/'review.c'),'-o',str(out/'review.exe')]
    if custom_layout:
        # Real weak send-string tables are replaced by an ordinary strong layout
        # definition. Both production executor and reference read these symbols.
        layout = out / 'layout.c'
        layout.write_text('#include "common.h"\n'
                          'const uint8_t ascii_to_keycode_lut[128] = {[1]=KC_NO,[65]=KC_AUDIO_VOL_UP,[66]=MS_BTN1,[97]=KC_A,[98]=KC_B};\n'
                          'const uint8_t ascii_to_shift_lut[16] = {[0]=2,[8]=2};\n'
                          'const uint8_t ascii_to_altgr_lut[16] = {[0]=2,[8]=6};\n'
                          'const uint8_t ascii_to_dead_lut[16] = {[0]=2,[8]=4};\n')
        cmd.insert(-2, str(layout))
    compiled=subprocess.run(cmd,capture_output=True,text=True)
    if compiled.returncode:
        raise AssertionError("fixture did not compile: " + compiled.stderr)
    return subprocess.run([str(out/'review.exe')],capture_output=True,text=True)

class IndependentMacroReview(unittest.TestCase):
    def check_fixture(self, interval=10, tap=5, mutation=None, custom_layout=False):
        with tempfile.TemporaryDirectory(prefix="era-macro-review-") as folder:
            result=fixture(Path(folder), interval, tap, mutation, custom_layout)
        if mutation:
            self.assertNotEqual(result.returncode, 0, "runtime mutation escaped assertions")
        else:
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

    def test_positive_intervals_and_all_byte_codes(self):
        self.check_fixture()

    def test_zero_intervals_and_all_byte_codes(self):
        self.check_fixture(0,0)

    def test_altgr_dead_and_nonkeyboard_layout(self):
        self.check_fixture(custom_layout=True)

    def test_negative_removed_wait(self):
        self.check_fixture(mutation="wait")

    def test_negative_removed_work_budget(self):
        self.check_fixture(0,0,"budget")

if __name__ == "__main__":
    unittest.main(verbosity=2)
