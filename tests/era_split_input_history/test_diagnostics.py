#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Diagnostic trigger isolation and truthful local reset results; OS is stubbed."""
import subprocess
import unittest
from test_production import ROOT, body, run_c

SCHEDULER = 'keyboards/era/common/split/era_split_transport_scheduler.c'
FUNCTION = 'era_split_transport_scheduler_flush_communication_core_for_diagnostics'


class Diagnostics(unittest.TestCase):
    def test_release_excludes_reset_function(self):
        source = (ROOT / SCHEDULER).read_text(encoding='utf-8')
        start = source.index('#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE\nbool ' + FUNCTION)
        end = source.index('/* The clock-free half', start)
        guarded = source[start:end]
        for enabled in (False, True):
            args = ['cc', '-E', '-P', '-x', 'c', '-']
            if enabled:
                args.insert(1, '-DERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE')
            result = subprocess.run(args, input=guarded, text=True, capture_output=True, check=True)
            self.assertEqual(FUNCTION in result.stdout, enabled)

    def test_reset_failures_are_reported_after_capture(self):
        reset = 'bool ' + FUNCTION + '(void) {\n' + body(SCHEDULER, FUNCTION) + '\n}\n'
        dispatch = 'bool era_split_wire_diagnostics_process_record(uint16_t keycode, keyrecord_t *record) {\n'
        dispatch += body('keyboards/era/common/split/diagnostics/era_split_wire_diagnostics.c',
                         'era_split_wire_diagnostics_process_record') + '\n}\n'
        run_c(STUBS + reset + dispatch + CASES)


STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#define ERA_HOST_PEER_STORAGE_V1_ENABLE
#define ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE 1
enum { WIRE_DIAG = 1, WIRE_DIAG_2 };
typedef struct { struct { bool pressed; } event; } keyrecord_t;
static struct { bool local_wire_available, local_wire_initiator; } g_era_split_transport_scheduler = {true, false};
static bool stop_ok, queue_ok, restore_ok;
static unsigned stopped, queued, restored, recovery, dirty, snapshots, printed, diag;
static unsigned printed_result;
static void era_split_transport_scheduler_ensure_initialized(void) {}
static bool era_split_transport_scheduler_stop_communication_core_for_flash_write(void) { ++stopped; return stop_ok; }
static bool era_split_communication_core_queue_reset(void) { assert(stopped && stop_ok); ++queued; return queue_ok; }
static bool era_split_transport_scheduler_reset_serial_for_transport_role(bool available, bool initiator, bool rotate) {
    assert(available && !initiator && rotate && queued); ++restored; return restore_ok;
}
static void era_split_transport_scheduler_force_storage_recovery(bool revalidate) { assert(revalidate && restored && restore_ok); ++recovery; }
static void era_split_transport_scheduler_mark_dirty(unsigned reason) { assert(reason == 1); ++dirty; }
static void era_split_wire_diagnostics_communication_core_on_diag(void) { ++diag; }
static void era_split_wire_diagnostics_print_snapshot(void) { assert(!printed); ++snapshots; }
static void uprintf(const char *format, ...) {
    (void)format; assert(snapshots == 1); va_list args; va_start(args, format);
    printed_result = va_arg(args, unsigned); va_end(args); ++printed;
}
'''
CASES = r'''
int main(void) {
    keyrecord_t press = {.event.pressed = true}, release = {0};
    for (unsigned failures = 0; failures < 8; ++failures) {
        stop_ok = !(failures & 1); queue_ok = !(failures & 2); restore_ok = !(failures & 4);
        stopped = queued = restored = recovery = dirty = snapshots = printed = 0;
        assert(!era_split_wire_diagnostics_process_record(WIRE_DIAG_2, &press));
        assert(stopped == 1 && queued == stop_ok && restored == stop_ok);
        assert(recovery == (stop_ok && restore_ok));
        assert(dirty == (!stop_ok || !restore_ok));
        assert(snapshots == 1 && printed == 1 && printed_result == (stop_ok && queue_ok && restore_ok));
    }
    assert(era_split_wire_diagnostics_process_record(WIRE_DIAG_2, &release));
    assert(era_split_wire_diagnostics_process_record(WIRE_DIAG_2, NULL));
    stopped = snapshots = printed = 0;
    assert(!era_split_wire_diagnostics_process_record(WIRE_DIAG, &press));
    assert(!stopped && snapshots == 1 && diag == 1);
    puts("PASS diagnostic local stop/queue/restore failures, snapshot-before-print, passive key and release compile guard");
    return 0;
}
'''

if __name__ == '__main__':
    unittest.main(verbosity=2)
