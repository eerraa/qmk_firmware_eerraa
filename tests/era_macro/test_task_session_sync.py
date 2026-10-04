#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the real common/service/session-sync composition with HAL seams.

One platform synchronization per common pass, before inactive admission and
configured replay. This counts work/order; it does not measure MCU timing.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GLUE = 'keyboards/era/common/system/era_hid_report_interval_chibios.c'


def function(path, name):
    source = (ROOT / path).read_text(encoding='utf-8')
    match = re.search(r'^void ' + name + r'\([^)]*\)\s*\{', source, re.M)
    assert match, name
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


SEAMS = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#define ERA_MACRO_ENABLE
#define ERA_HID_REPORT_INTERVAL_ENABLE
#define USB_DEVICE_STATE_CONFIGURED 1
#define USB_DEVICE_STATE_SUSPEND 2
#define USB_ACTIVE 1
#define USB_ENDPOINT_IN_KEYBOARD 0
static unsigned sync_calls, configure_reads, locks, ticks, replay, generation_seen;
static unsigned era_hid_session_events;
static bool era_hid_reopen, era_hid_auxiliary_reopen, era_hid_retire_pending, era_hid_retire_reset;
static unsigned era_hid_format;
static struct { int obqueue; } usb_endpoints_in[1];
static int USB_DRIVER;
static bool configured = true, source_available, source_paused;
uint8_t era_usb_session_configure_state(void) {
    ++configure_reads;
    return configured ? USB_DEVICE_STATE_CONFIGURED : 0;
}
void era_macro_session(uint32_t gen, bool valid, bool paused) {
    ++sync_calls; generation_seen = gen; source_available = valid; source_paused = paused;
}
void era_usb_session_task(void) { assert(generation_seen == era_hid_session_events); }
void era_macro_task(void) { ++ticks; assert(source_available == configured); }
bool era_hid_report_interval_active(void) { return false; }
bool era_usb_auxiliary_reports_pending(void) { return false; }
unsigned era_hid_report_interval_current_format(void) { return 0; }
void era_hid_report_interval_ensure_ready(void) { assert(generation_seen == era_hid_session_events); }
void osalSysLock(void) { ++locks; }
void osalSysUnlock(void) {}
int usbGetDriverStateI(int *d) { (void)d; return configured ? USB_ACTIVE : 0; }
bool bqIsSuspendedX(int *q) { (void)q; return false; }
bool era_hid_report_interval_select_format(unsigned f) { (void)f; return false; }
void era_hid_report_interval_retire_task(void) {}
void era_hid_report_interval_task(unsigned now) { (void)now; }
unsigned era_hid_report_interval_now(void) { return 0; }
void force_send_keyboard_report(void) { assert(source_available && !source_paused); ++replay; }
void host_resend_last_auxiliary_reports(void) { assert(source_available && !source_paused); ++replay; }
void era_usb_auxiliary_reports_task(void) {}
'''

CASES = r'''
int main(void) {
    for (unsigned i = 0; i < 100000; ++i) era_common_features_task();
    printf("idle100000 sync=%u configure_reads=%u locks=%u task=%u\n", sync_calls, configure_reads, locks, ticks);
    fflush(stdout);
    assert(sync_calls == 100000 && configure_reads == 100000 && !locks && ticks == 100000);
    unsigned before = sync_calls;
    era_hid_report_interval_service();
    assert(sync_calls == before + 1); /* Direct service still owns this boundary. */
    configured = false; ++era_hid_session_events;
    era_hid_reopen = era_hid_auxiliary_reopen = true;
    before = sync_calls; era_common_features_task();
    assert(sync_calls == before + 1 && !source_available && generation_seen == era_hid_session_events);
    assert(!replay && era_hid_reopen && era_hid_auxiliary_reopen);
    configured = true;
    before = sync_calls; era_common_features_task();
    assert(sync_calls == before + 1 && source_available && replay == 2);
    assert(!era_hid_reopen && !era_hid_auxiliary_reopen);
    puts("service direct sync and RESET-inactive-CONFIGURED ordering PASS");
}
'''


def run(mutation=None):
    common = function('keyboards/era/common/system/era_common_features.c', 'era_common_features_task')
    service = function(GLUE, 'era_hid_report_interval_service')
    sync = function(GLUE, 'era_macro_platform_sync')
    if mutation == 'duplicate':
        common = common.replace('{', '{\n    era_macro_platform_sync();', 1)
    elif mutation == 'service_sync':
        needle = '    era_macro_platform_sync();'
        assert service.count(needle) == 1
        service = service.replace(needle, '', 1)
    with tempfile.TemporaryDirectory(prefix='era-macro-sync-') as folder:
        folder = Path(folder)
        source, executable = folder / 'sync.c', folder / 'sync'
        source.write_text(SEAMS + sync + service + common + CASES, encoding='utf-8')
        compiled = subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(executable)], capture_output=True, text=True)
        if compiled.returncode:
            raise AssertionError('fixture did not compile: ' + compiled.stderr)
        return subprocess.run([str(executable)], capture_output=True, text=True, errors='replace')


class TaskSessionSync(unittest.TestCase):
    def test_one_sync_per_pass_before_reopen(self):
        result = run()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_negative_reinserted_common_sync(self):
        result = run('duplicate')
        self.assertNotEqual(result.returncode, 0, 'duplicate sync escaped runtime assertions')
        self.assertIn('sync=200000 configure_reads=200000', result.stdout)

    def test_negative_removed_service_sync(self):
        result = run('service_sync')
        self.assertNotEqual(result.returncode, 0, 'missing service sync escaped runtime assertions')


if __name__ == '__main__':
    unittest.main(verbosity=2)
