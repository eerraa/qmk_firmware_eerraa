#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Adversarial reset interleavings through real keyboard/auxiliary report owners.

The endpoint seam models generation rejection; production interval and auxiliary
retention code must not retain that rejected old-session report for later replay.
Each negative control compiles with its new admission guard removed, then fails
at runtime. This is software lifetime evidence, not hardware USB timing.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

def function(path, name):
    source = (ROOT / path).read_text()
    match = re.search(r'^(?:static )?(?:bool|void) ' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]

STUBS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "keyboards/era/common/system/era_hid_report_interval.h"
typedef uint8_t usb_endpoint_in_lut_t;
static struct { uint8_t endpoint,size,bytes[32]; } auxiliary_reports[4];
static uint8_t auxiliary_pending;
static volatile bool auxiliary_session_reset;
static bool source_current, inject_after_sample;
static unsigned posted;
static uint8_t first;
static void osalSysLock(void) {}
static void osalSysUnlock(void) {}
bool era_macro_report_source_current(void) {
    bool before=source_current;
    if(inject_after_sample) {
        inject_after_sample=false;
        era_hid_report_interval_note_session_edge(false);
        auxiliary_session_reset=true;
        source_current=false;
    }
    return before;
}
static bool send_report(uint8_t endpoint,const void *data,size_t size) {
    (void)endpoint;(void)size;
    if(!source_current)return false;
    if(!posted)first=*(const uint8_t *)data;
    ++posted;return true;
}
bool era_hid_report_interval_platform_post(uint8_t lane,const uint8_t *data,uint8_t size) {
    return send_report(lane,data,size);
}
'''
CASES = r'''
static void fresh(void) {
    era_hid_report_interval_init(1);
    auxiliary_pending=0;auxiliary_session_reset=false;
    source_current=false;inject_after_sample=false;posted=0;first=0;
}
static void keyboard_before_retention(void) {
    uint8_t stale=4, neutral=0;fresh();
    era_hid_report_interval_note_session_edge(false);
    assert(era_hid_report_interval_hold(0,&stale,1));
    source_current=true;
    assert(era_hid_report_interval_hold(0,&neutral,1));
    era_hid_report_interval_task(1);
    assert(posted==1 && first==0);
}
static void keyboard_after_sample(void) {
    uint8_t stale=4, neutral=0;fresh();source_current=true;inject_after_sample=true;
    assert(era_hid_report_interval_hold(0,&stale,1));
    source_current=true;
    assert(era_hid_report_interval_hold(0,&neutral,1));
    era_hid_report_interval_task(1);
    assert(posted==1 && first==0);
}
static void auxiliary_before_retention(void) {
    uint8_t stale=4;fresh();auxiliary_session_reset=true;
    auxiliary_report(0,0,&stale,1);
    source_current=true;era_usb_auxiliary_reports_task();
    assert(posted==0 && auxiliary_pending==0);
}
static void auxiliary_after_sample(void) {
    uint8_t stale=4;fresh();source_current=true;inject_after_sample=true;
    auxiliary_report(0,0,&stale,1);
    source_current=true;era_usb_auxiliary_reports_task();
    assert(posted==0 && auxiliary_pending==0);
}
int main(void) {
    keyboard_before_retention();keyboard_after_sample();
    auxiliary_before_retention();auxiliary_after_sample();return 0;
}
'''

def run(mutation=None):
    source = (ROOT / 'keyboards/era/common/system/era_hid_report_interval.c').read_text()
    auxiliary = '\n'.join(function('tmk_core/protocol/chibios/usb_main.c', name) for name in
                          ['auxiliary_apply_edge', 'auxiliary_report', 'era_usb_auxiliary_reports_task'])
    if mutation == 'keyboard':
        guard='if (!era_macro_report_source_current()) return true;'
        assert guard in source
        source=source.replace(guard,'/* negative control: accept stale keyboard source */',1)
    elif mutation == 'auxiliary':
        guard='if (!era_macro_report_source_current()) return;'
        assert guard in auxiliary
        auxiliary=auxiliary.replace(guard,'/* negative control: accept stale auxiliary source */',1)
    with tempfile.TemporaryDirectory(prefix='era-macro-lifetime-') as folder:
        path=Path(folder)
        (path/'interval.c').write_text(source)
        (path/'fixture.c').write_text(STUBS+auxiliary+CASES)
        executable=path/'fixture.exe'
        subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-DERA_MACRO_ENABLE',
                        '-I'+str(ROOT),'-I'+str(ROOT/'keyboards/era/common/system'),
                        str(path/'interval.c'),str(path/'fixture.c'),'-o',str(executable)],
                       check=True,capture_output=True,text=True)
        return subprocess.run([str(executable)],capture_output=True,text=True)

class MacroTransportLifetime(unittest.TestCase):
    def test_real_report_owners_reject_stale_prefix_and_latest(self):
        result=run()
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_negative_keyboard_admission_removed(self):
        self.assertNotEqual(run('keyboard').returncode,0)

    def test_negative_auxiliary_admission_removed(self):
        self.assertNotEqual(run('auxiliary').returncode,0)

if __name__ == '__main__':
    unittest.main(verbosity=2)
