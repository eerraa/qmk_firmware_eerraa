#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Link production ChibiOS wake ordering to the real native QMK objects.

Only HAL endpoints/locks and the instant at which a wake IRQ arrives are
simulated. protocol_pre_task, deferred USB event handlers, HAL _usb_wakeup,
matrix capture, QMK tapping/dispatch, shared output and ChibiOS wake clear are
the current production functions. This does not execute the complete ChibiOS
binary or establish device timing. Run after the native wake targets, under
the host build lock; all compilation/mutation output is isolated.
"""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess

STUBS = r'''
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "action.h"
#include "suspend.h"
#include "usb_device_state.h"
#include "wait.h"
#define USB_EVENT_QUEUE_SIZE 16
typedef enum {USB_EVENT_ADDRESS,USB_EVENT_CONFIGURED,USB_EVENT_SUSPEND,USB_EVENT_UNCONFIGURED,USB_EVENT_RESET,USB_EVENT_WAKEUP,USB_EVENT_STALLED} usbevent_t;
enum {USB_ACTIVE=1,USB_SUSPENDED=2,USB_GETSTATUS_REMOTE_WAKEUP_ENABLED=2};
typedef struct {int state,saved_state;unsigned status;uint8_t configuration;} USBDriver;
static USBDriver usb_driver;
#define USB_DRIVER usb_driver
#define USB_ENDPOINT_IN_COUNT 3
#define USB_ENDPOINT_OUT_COUNT 1
static int usb_endpoints_in[3],usb_endpoints_out[1];
static usbevent_t event_queue[USB_EVENT_QUEUE_SIZE];
static uint8_t event_queue_head,event_queue_tail;
static void osalSysLockFromISR(void){}
static void osalSysUnlockFromISR(void){}
static void chSysLockFromISR(void){}
static void chSysUnlockFromISR(void){}
static void usb_endpoint_in_configure_cb(int *p){(void)p;}
static void usb_endpoint_out_configure_cb(int *p){(void)p;}
static void usb_endpoint_in_suspend_cb(int *p){(void)p;}
static void usb_endpoint_out_suspend_cb(int *p){(void)p;}
static void usb_endpoint_in_wakeup_cb(int *p){(void)p;}
static void usb_endpoint_out_wakeup_cb(int *p){(void)p;}
#define dprintln(x) ((void)0)
static void usb_event_cb(USBDriver *,usbevent_t);
#define _usb_isr_invoke_event_cb(d,e) usb_event_cb(d,e)
static void _usb_wakeup(USBDriver *);
static unsigned wake_calls,remote_wake_calls;
static bool host_wake_in_sleep;
static void usbWakeupHost(USBDriver *d){++remote_wake_calls;_usb_wakeup(d);}
void suspend_power_down_kb(void){if(host_wake_in_sleep)_usb_wakeup(&USB_DRIVER);}
void suspend_wakeup_init_kb(void){++wake_calls;}
'''

HELPERS = r'''
void review_suspend(bool remote){
  usb_event_queue_init();last_suspend_state=false;wake_calls=remote_wake_calls=0;
  host_wake_in_sleep=!remote;
  USB_DRIVER=(USBDriver){.state=USB_SUSPENDED,.saved_state=USB_ACTIVE,.configuration=1,
                         .status=remote?USB_GETSTATUS_REMOTE_WAKEUP_ENABLED:0};
  usb_device_state_init();usb_device_state_set_configuration(true,1);
  usb_event_cb(&USB_DRIVER,USB_EVENT_SUSPEND);
}
void review_protocol_pre_task(void){protocol_pre_task();}
unsigned review_wake_calls(void){return wake_calls;}
unsigned review_remote_wake_calls(void){return remote_wake_calls;}
bool review_wake_queued(void){return event_queue_head!=event_queue_tail;}
void review_host_resume(void){_usb_wakeup(&USB_DRIVER);}
'''

CASES = r'''
extern "C" {
void advance_time(uint32_t);
void review_suspend(bool);
void review_protocol_pre_task(void);
unsigned review_wake_calls(void);
unsigned review_remote_wake_calls(void);
bool review_wake_queued(void);
void review_host_resume(void);
}
TEST_F(EraWakeInput,ProtocolWakeClearPrecedesFirstResumedQmkPass) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,TD(0)),KeymapKey(1,1,0,KC_A)});
    dance(KC_LCTL);down(0);down(1);ASSERT_FALSE(layer_state_is(1));
    review_suspend(false);advance_time(TAPPING_TERM+1);review_protocol_pre_task();
    ASSERT_EQ(review_wake_calls(),1U);ASSERT_FALSE(review_wake_queued());
    run_one_scan_loop();ASSERT_TRUE(layer_state_is(1));ASSERT_TRUE(is_key_pressed(KC_A));
    review_protocol_pre_task();EXPECT_TRUE(is_key_pressed(KC_A));
    up(1);up(0);EXPECT_FALSE(is_key_pressed(KC_A));EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraWakeInput,ProtocolShortSuspendPendingLayerTapAndRemappedDance) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,TD(0)),KeymapKey(1,1,0,KC_A)});
    dance(KC_LCTL);down(0);down(1);review_suspend(false);review_protocol_pre_task();
    ASSERT_EQ(review_wake_calls(),1U);run_one_scan_loop();idle_for(TAPPING_TERM+1);
    ASSERT_TRUE(layer_state_is(1));ASSERT_TRUE(is_key_pressed(KC_A));
    up(1);up(0);EXPECT_FALSE(is_key_pressed(KC_A));EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraWakeInput,ProtocolPendingModTapExecutesAfterWakeAndReleases) {
    mapping({KeymapKey(0,0,0,MT(MOD_LCTL,KC_X))});down(0);
    review_suspend(false);advance_time(TAPPING_TERM+1);review_protocol_pre_task();
    ASSERT_EQ(review_wake_calls(),1U);run_one_scan_loop();ASSERT_EQ(get_mods(),MOD_BIT(KC_LCTL));
    up(0);EXPECT_EQ(get_mods(),0);EXPECT_FALSE(is_key_pressed(KC_X));
}
TEST_F(EraWakeInput,ProtocolAlreadySettledLayerTapReleases) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X))});down(0);idle_for(TAPPING_TERM+1);
    ASSERT_TRUE(layer_state_is(1));review_suspend(false);advance_time(TAPPING_TERM+1);
    review_protocol_pre_task();run_one_scan_loop();up(0);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraWakeInput,ProtocolAlreadySettledMomentaryLayerReleases) {
    mapping({KeymapKey(0,0,0,MO(1))});down(0);ASSERT_TRUE(layer_state_is(1));
    review_suspend(false);advance_time(TAPPING_TERM+1);review_protocol_pre_task();
    run_one_scan_loop();up(0);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraWakeInput,ProtocolAlreadyClearedLayerModsPreserveFreshModifier) {
    mapping({KeymapKey(0,0,0,LM(1,MOD_LCTL)),KeymapKey(0,1,0,KC_LCTL)});down(0);
    review_suspend(false);advance_time(TAPPING_TERM+1);review_protocol_pre_task();
    ASSERT_EQ(get_mods(),0);down(1);up(0);EXPECT_FALSE(layer_state_is(1));
    EXPECT_EQ(get_mods(),MOD_BIT(KC_LCTL));up(1);
}
TEST_F(EraWakeInput,ProtocolPendingLayerModsUseOriginalModifiersAfterRemap) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,TD(0)),KeymapKey(1,1,0,LM(2,MOD_LCTL)),
             KeymapKey(0,2,0,KC_RCTL)});down(0);down(1);
    review_suspend(false);advance_time(TAPPING_TERM+1);review_protocol_pre_task();run_one_scan_loop();
    ASSERT_TRUE(layer_state_is(2));ASSERT_EQ(get_mods(),MOD_BIT(KC_LCTL));
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,LM(3,MOD_RCTL)),KeymapKey(0,2,0,KC_RCTL)});
    down(2);up(1);EXPECT_FALSE(layer_state_is(2));EXPECT_FALSE(layer_state_is(3));
    EXPECT_EQ(get_mods(),MOD_BIT(KC_RCTL));up(2);up(0);
}
TEST_F(EraWakeInput,ProtocolHeldDanceSurvivesWakeAndReleases) {
    mapping({KeymapKey(0,0,0,TD(0))});dance(KC_A);down(0);idle_for(TAPPING_TERM+1);
    review_suspend(false);advance_time(TAPPING_TERM+1);review_protocol_pre_task();run_one_scan_loop();
    ASSERT_TRUE(is_key_pressed(KC_A));up(0);EXPECT_FALSE(is_key_pressed(KC_A));
}
TEST_F(EraWakeInput,ProtocolRemoteWakeCapturesAndReleasesMomentaryLayer) {
    mapping({KeymapKey(0,0,0,MO(1))});down(0);review_suspend(true);
    review_protocol_pre_task();EXPECT_EQ(review_wake_calls(),1U);EXPECT_EQ(review_remote_wake_calls(),1U);
    run_one_scan_loop();up(0);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraWakeInput,ProtocolHostResumeBeforePreTaskStillReleasesLayer) {
    mapping({KeymapKey(0,0,0,MO(1))});down(0);review_suspend(false);review_host_resume();
    review_protocol_pre_task();ASSERT_EQ(review_wake_calls(),1U);run_one_scan_loop();
    up(0);EXPECT_FALSE(layer_state_is(1));
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    root = parser.parse_args().root.resolve()
    if os.name != 'posix' or str(root).startswith('/mnt/'):
        raise SystemExit('Use the Linux local-filesystem root synchronized by era_build.py and its native build objects.')
    out = root / '.era-artifacts/improvement-V261004R2/usb/protocol' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    out.mkdir(parents=True)
    commands = []

    def run(label, command, timeout=90):
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=timeout)
        (out / (label + '.log')).write_text(result.stdout + result.stderr)
        commands.append(dict(name=label, argv=command, exit=result.returncode))
        (out / 'commands.json').write_text(json.dumps(commands, indent=2))
        return result

    def function(relative, name):
        source = (root / relative).read_text()
        match = re.search(r'^(?:static\s+)?(?:inline\s+)?(?:bool|void)\s+' + re.escape(name) + r'\([^;{}]*\)\s*\{', source, re.M)
        assert match, name
        depth, end = 1, match.end()
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        return source[match.start():end]

    meta = root / '.build/test_obj/era_wake_input'
    flags = shlex.split((meta / 'cflags.txt').read_text())
    cxxflags = shlex.split((meta / 'cxxflags.txt').read_text())
    objects = list(dict.fromkeys(shlex.split((meta / 'obj.txt').read_text())))
    links = shlex.split((meta / 'ldflags.txt').read_text())
    replaced = [p for p in objects if p.endswith('tests/era_wake_input/test_wake_input.o')]
    assert len(replaced) == 1, replaced
    product_paths = ['quantum/action.c', 'quantum/action.h', 'quantum/action_tapping.c', 'quantum/action_tapping.h',
                     'quantum/action_layer.c', 'quantum/keyboard.c', 'quantum/quantum.c', 'platforms/chibios/suspend.c',
                     'platforms/suspend.c', 'tmk_core/protocol/chibios/chibios.c',
                     'tmk_core/protocol/chibios/usb_main.c', 'lib/chibios/os/hal/src/hal_usb.c']
    tracked = {root / name for name in [*objects, *product_paths, 'tests/era_wake_input/test_wake_input.cpp']}
    fingerprint = lambda: {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(tracked)}
    before = fingerprint()
    sequence = []
    for name in ['usb_event_queue_init', 'usb_event_queue_enqueue', 'usb_event_queue_dequeue',
                 'usb_event_suspend_handler', 'usb_event_wakeup_handler', 'usb_event_queue_task', 'usb_event_cb']:
        if name == 'usb_event_queue_task':
            sequence.append('bool last_suspend_state=false;')
        sequence.append(function('tmk_core/protocol/chibios/usb_main.c', name))
    pre = function('tmk_core/protocol/chibios/chibios.c', 'protocol_pre_task')
    sequence.extend([pre, function('lib/chibios/os/hal/src/hal_usb.c', '_usb_wakeup').replace('void _usb_wakeup(', 'static void _usb_wakeup(', 1)])
    protocol = STUBS + '\n'.join(sequence) + HELPERS
    cpp, test_obj = out / 'protocol_cases.cpp', out / 'protocol_cases.o'
    cpp.write_text((root / 'tests/era_wake_input/test_wake_input.cpp').read_text() + CASES)
    compiled = run('cases-compile', ['g++', *cxxflags, '-c', str(cpp), '-o', str(test_obj)])
    assert compiled.returncode == 0, 'Fixture compile failed; not a runtime result'
    for label, extra, source, case, expected in [
        ('production', [], protocol, 'EraWakeInput.Protocol*', 0),
        ('production-delay5', ['-DUSB_SUSPEND_WAKEUP_DELAY=5'], protocol, 'EraWakeInput.Protocol*', 0),
        ('omit-resume-drain', [], protocol.replace(pre, pre[:pre.rfind('usb_event_queue_task();')] + '/* isolated removal */' + pre[pre.rfind('usb_event_queue_task();') + len('usb_event_queue_task();'):], 1),
         'EraWakeInput.ProtocolWakeClearPrecedesFirstResumedQmkPass', 1),
    ]:
        copied, obj, binary = out / (label + '.c'), out / (label + '.o'), out / (label + '.elf')
        copied.write_text(source)
        compiled = run(label + '-compile', ['gcc', *flags, *extra, '-Wno-unused-function', '-Wno-unused-parameter', '-c', str(copied), '-o', str(obj)])
        assert compiled.returncode == 0, label + ': compile failure is NOT runtime detection'
        linked = run(label + '-link', ['gcc', *(str(test_obj) if p == replaced[0] else p for p in objects), str(obj), '-o', str(binary), *links])
        assert linked.returncode == 0, label + ': link failure is NOT runtime detection'
        result = run(label + '-run', [str(binary), '--gtest_filter=' + case], 30)
        assert result.returncode == expected, label + ': unexpected runtime result'
        if expected:
            assert '[  FAILED  ] ' + case in result.stdout, 'Named assertion did not fail'
        else:
            assert '[  PASSED  ] 10 tests' in result.stdout, 'Protocol cases were not all run'
        print('PASS', label, 'compiled runtime negative' if expected else 'production protocol/QMK', flush=True)
    assert fingerprint() == before, 'Product sources or native objects changed'
    (out / 'source-preserved.json').write_text(json.dumps(dict(unchanged=True, fingerprints=before), indent=2))
    print('Evidence:', out, flush=True)


if __name__ == '__main__':
    main()
