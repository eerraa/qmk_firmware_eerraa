#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production executor, host projection, USB retry and reopen service together.

Only the HAL, clock, storage and logical contribution seams are simulated.
RESET -> inactive task -> CONFIGURED must survive the second queue discard.
Negative controls remove the actual reopen dispatch and zero-motion boundary.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_transport_lifetime import function

ROOT = Path(__file__).resolve().parents[2]
GLUE = 'keyboards/era/common/system/era_hid_report_interval_chibios.c'

COMMON = r'''
#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "quantum/keycodes.h"
#define ERA_MACRO_ENABLE
#define ERA_HID_REPORT_INTERVAL_ENABLE
#define EXTRAKEY_ENABLE
#define MOUSE_ENABLE
#define MOUSEKEY_ENABLE
#define MOUSE_SHARED_EP
#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 64
#define DYNAMIC_KEYMAP_MACRO_DELAY 0
#define TAP_CODE_DELAY 0
#define TAP_HOLD_CAPS_DELAY 80
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#include "quantum/send_string/send_string.h"
#include "quantum/action_output.h"
#include "quantum/era_macro.h"
typedef struct { uint8_t report_id,buttons; int8_t x,y,v,h; } report_mouse_t;
void register_code(uint8_t);void unregister_code(uint8_t);
void send_keyboard_report(void);void host_keyboard_delay(uint16_t);
void host_resend_last_extra_reports(void);void host_mouse_send(report_mouse_t *);
report_mouse_t mousekey_get_report(void);
uint32_t timer_read32(void);
uint8_t dynamic_keymap_macro_get_count(void);
uint16_t dynamic_keymap_macro_get_buffer_size(void);
void dynamic_keymap_macro_get_buffer(uint16_t,uint16_t,uint8_t *);
'''
STUBS = r'''
#include "common.h"
#define REPORT_ID_SYSTEM 1
#define REPORT_ID_CONSUMER 2
#define REPORT_ID_MOUSE 3
#define USB_ACTIVE 1
#define USB_ENDPOINT_IN_KEYBOARD 0
#define USB_DRIVER usb_driver
typedef uint8_t usb_endpoint_in_lut_t;
typedef struct { uint8_t report_id; uint16_t usage; } report_extra_t;
typedef struct { void (*send_extra)(report_extra_t *); void (*send_mouse)(report_mouse_t *); } host_driver_t;
static struct {uint8_t endpoint,size,bytes[32];} auxiliary_reports[4];
static uint8_t auxiliary_pending;
static bool auxiliary_session_reset,connected=true,busy,inject_reset;
static uint16_t last_system_usage,last_consumer_usage,host_consumer,regular_consumer,macro_consumer;
static uint8_t last_mouse_buttons,host_buttons,regular_buttons,macro_buttons;
static int motion;
static action_output_owner_t owner=ACTION_OUTPUT_OWNER_REGULAR;
static uint8_t memory[64]={SS_QMK_PREFIX,SS_DOWN_CODE,KC_VOLD,SS_QMK_PREFIX,SS_DOWN_CODE,MS_BTN2,0};
const uint8_t ascii_to_keycode_lut[128]={0},ascii_to_shift_lut[16]={0},ascii_to_altgr_lut[16]={0},ascii_to_dead_lut[16]={0};
static bool era_hid_reopen,era_hid_auxiliary_reopen,era_hid_retire_reset;
static uint8_t era_hid_format=1,era_hid_retire_pending,era_hid_retire_debt;
static uint32_t era_hid_session_events=1,era_hid_suspend_events,era_hid_protocol_events,era_hid_protocol_seen;
static int usb_driver;
static struct { int obqueue; } usb_endpoints_in[1];
static void osalSysLock(void){}
static void osalSysUnlock(void){}
static int usbGetDriverStateI(int *d){(void)d;return connected?USB_ACTIVE:0;}
static bool bqIsSuspendedX(int *q){(void)q;return !connected;}
static void era_hid_report_interval_note_session_edge(bool keep){(void)keep;}
static uint8_t era_hid_report_interval_current_format(void){return 1;}
static bool era_hid_report_interval_active(void){return false;}
static void era_hid_report_interval_ensure_ready(void){}
static bool era_hid_report_interval_select_format(uint8_t f){(void)f;return false;}
static void era_hid_report_interval_retire_task(void){era_hid_retire_reset=false;}
static uint32_t era_hid_report_interval_now(void){return 1;}
static void era_hid_report_interval_task(uint32_t t){(void)t;}
static void force_send_keyboard_report(void){}
static void reset_edge(void);
bool era_macro_report_source_current(void){return era_macro_session_current(era_hid_session_events);}
static bool send_report(uint8_t endpoint,const void *data,size_t size){
 (void)endpoint;(void)size;
 if(inject_reset){inject_reset=false;reset_edge();}
 if(!connected||busy||!era_macro_report_source_current())return false;
 if(*(const uint8_t *)data==REPORT_ID_MOUSE){
  const report_mouse_t *r=data;host_buttons=r->buttons;motion+=r->x+r->y+r->v+r->h;
 }else{
  const report_extra_t *r=data;if(r->report_id==REPORT_ID_CONSUMER)host_consumer=r->usage;
 }
 return true;
}
'''
SEAMS = r'''
static void send_extra(report_extra_t *r){auxiliary_report(r->report_id,1,r,sizeof(*r));}
static void send_mouse(report_mouse_t *r){auxiliary_report(0,1,r,sizeof(*r));}
static host_driver_t driver={send_extra,send_mouse};
static host_driver_t *host_get_active_driver(void){return &driver;}
'''
CASES = r'''
action_output_owner_t action_output_set_owner(action_output_owner_t n){action_output_owner_t p=owner;owner=n;return p;}
action_output_owner_t action_output_get_owner(void){return owner;}
void action_output_clear_owner_keys(action_output_owner_t o){(void)o;}
void action_output_clear_owner_mods(action_output_owner_t o){(void)o;}
report_mouse_t mousekey_get_report(void){return (report_mouse_t){.buttons=regular_buttons|macro_buttons,.x=7};}
void action_output_clear_owner_hid(action_output_owner_t o){
 if(o==ACTION_OUTPUT_OWNER_MACRO_HOLD){
  macro_consumer=0;macro_buttons=0;host_consumer_send(regular_consumer);
 }
}
void register_code(uint8_t code){
 if(code==MS_BTN1||code==MS_BTN2){
  if(owner==ACTION_OUTPUT_OWNER_REGULAR)regular_buttons=1;else macro_buttons=2;
  report_mouse_t r=mousekey_get_report();host_mouse_send(&r);
 }else{
  if(owner==ACTION_OUTPUT_OWNER_REGULAR)regular_consumer=1;else macro_consumer=2;
  host_consumer_send(macro_consumer?macro_consumer:regular_consumer);
 }
}
void unregister_code(uint8_t code){(void)code;}
void send_keyboard_report(void){}
void host_keyboard_delay(uint16_t d){(void)d;}
uint32_t timer_read32(void){return 1;}
uint8_t dynamic_keymap_macro_get_count(void){return 16;}
uint16_t dynamic_keymap_macro_get_buffer_size(void){return sizeof(memory);}
void dynamic_keymap_macro_get_buffer(uint16_t o,uint16_t n,uint8_t *d){memcpy(d,memory+o,n);}
void era_macro_platform_sync(void){era_macro_session(era_hid_session_events,connected,false);}
bool era_macro_platform_pressured(void){return auxiliary_pending!=0;}
#include "macro.c"
static void reset_edge(void){
 connected=false;era_hid_report_interval_session_edge_i(false);auxiliary_session_reset=true;
 host_consumer=host_buttons=0;
}
static void configure_edge(void){
 connected=true;era_hid_report_interval_session_edge_i(false);auxiliary_session_reset=true;
}
int main(void){
 era_macro_platform_sync();register_code(KC_VOLU);register_code(MS_BTN1);
 assert(era_macro_request(0));era_macro_task();assert(host_consumer==2&&host_buttons==3);
 reset_edge();era_hid_report_interval_service();
 assert(last_consumer_usage==1&&last_mouse_buttons==1);
 configure_edge();motion=0;era_hid_report_interval_service();
 assert(host_consumer==1&&host_buttons==1&&motion==0);
 /* A reset during replay rearms the USB obligation, independently of macros. */
 reset_edge();era_hid_report_interval_service();configure_edge();inject_reset=true;
 era_hid_report_interval_service();assert(!connected&&era_hid_auxiliary_reopen);
 configure_edge();era_hid_report_interval_service();
 assert(host_consumer==1&&host_buttons==1&&motion==0);
 /* Normal BUSY retains the regenerated report for later retry. */
 reset_edge();era_hid_report_interval_service();configure_edge();busy=true;
 era_hid_report_interval_service();assert(auxiliary_pending);
 busy=false;era_hid_report_interval_service();
 assert(host_consumer==1&&host_buttons==1&&motion==0);
 /* Cancellation within a live session must not repeat relative motion either. */
 assert(era_macro_request(0));era_macro_task();motion=0;era_macro_cancel();
 assert(host_consumer==1&&host_buttons==1&&motion==0);
 return 0;
}
'''

def run(mutation=None):
    auxiliary='\n'.join(function('tmk_core/protocol/chibios/usb_main.c', n) for n in
                         ['era_usb_auxiliary_reports_pending','auxiliary_apply_edge','auxiliary_report','era_usb_auxiliary_reports_task'])
    host='\n'.join(function('tmk_core/protocol/host.c', n) for n in
                    ['host_mouse_send','host_system_send','host_consumer_send','host_resend_last_extra_reports','host_resend_last_auxiliary_reports'])
    service='\n'.join(function(GLUE,n) for n in ['era_hid_report_interval_session_edge_i','era_hid_report_interval_service'])
    macro=(ROOT/'quantum/era_macro.c').read_text(encoding='utf-8')
    if mutation=='reopen':
        before='if (auxiliary_reopen) host_resend_last_auxiliary_reports();'
        assert before in service
        service=service.replace(before,'(void)auxiliary_reopen;')
    if mutation=='motion':
        before='report_mouse_t report = {.buttons = mousekey_get_report().buttons};'
        assert before in macro
        macro=macro.replace(before,'report_mouse_t report = mousekey_get_report();')
    with tempfile.TemporaryDirectory(prefix='era-auxiliary-reopen-') as directory:
        out=Path(directory)
        (out/'common.h').write_text(COMMON)
        for name in ['action.h','action_util.h','dynamic_keymap.h','send_string.h','timer.h','host.h','eeprom.h','mousekey.h']:
            (out/name).write_text('#include "common.h"\n')
        (out/'progmem.h').write_text('#pragma once\n#define PROGMEM\n#define pgm_read_byte(p) (*(const uint8_t *)(p))\n')
        (out/'macro.c').write_text(macro)
        (out/'fixture.c').write_text(STUBS+auxiliary+SEAMS+host+service+CASES)
        executable=out/'fixture'
        subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(out),
                        '-I'+str(ROOT),'-I'+str(ROOT/'quantum'),str(out/'fixture.c'),'-o',str(executable)],
                       check=True,capture_output=True,text=True)
        return subprocess.run([str(executable)],capture_output=True,text=True)

class AuxiliaryReopen(unittest.TestCase):
    def test_configured_replay_inactive_reset_race_and_busy(self):
        result=run()
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_negative_reopen_dispatch_removed(self):
        self.assertNotEqual(run('reopen').returncode,0)

    def test_negative_cancellation_replays_motion(self):
        self.assertNotEqual(run('motion').returncode,0)

if __name__=='__main__':
    unittest.main(verbosity=2)
