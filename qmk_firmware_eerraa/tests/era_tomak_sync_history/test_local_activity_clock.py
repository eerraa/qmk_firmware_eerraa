#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production matrix activity and sleep policy across shared-clock changes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
keyboard = (root / "quantum/keyboard.c").read_text()
start = keyboard.index("static uint32_t last_matrix_modification_time = 0;")
activity = keyboard[start:keyboard.index("static uint32_t last_encoder_modification_time = 0;", start)]
prefix = r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "keyboards/era/common/features/era_lighting_sleep_policy.h"
static uint32_t local_now, shared_now, last_input_modification_time;
static uint32_t timer_read32(void) { return local_now; }
static uint32_t timer_elapsed32(uint32_t previous) { return local_now-previous; }
static uint32_t sync_timer_read32(void) { return shared_now; }
static uint32_t sync_timer_elapsed32(uint32_t previous) { return shared_now-previous; }
"""
cases = r"""
static bool sleeping(bool suspend,bool frames,bool enabled) {
    return era_lighting_sleep_policy_requested(suspend,frames,enabled,60,last_matrix_activity_local_elapsed());
}
int main(void) {
    local_now=1000; shared_now=50000;
    last_matrix_activity_trigger();
    assert(last_matrix_activity_time()==50000 && !sleeping(false,false,true));
    local_now+=100; shared_now=10000;
    assert(last_matrix_activity_local_elapsed()==100 && !sleeping(false,false,true));
    shared_now=4000000;
    assert(last_matrix_activity_local_elapsed()==100 && !sleeping(false,false,true));
    local_now+=59900;
    assert(sleeping(false,false,true));
    /* No synthetic wake on role/source changes: genuinely idle age survives. */
    shared_now=1; assert(sleeping(false,false,true));
    last_matrix_activity_trigger(); assert(!sleeping(false,false,true));
    assert(sleeping(true,false,true) && sleeping(false,true,true));
    assert(!sleeping(true,true,false));
    local_now=UINT32_MAX-10; last_matrix_activity_trigger();
    local_now=9; assert(last_matrix_activity_local_elapsed()==20);
    assert(!sleeping(false,false,true));
    puts("PASS local idle survives shared-clock jumps/source changes, timeout, wake, USB reasons, OFF and local wrap");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-local-activity-") as directory:
    c, exe = Path(directory) / "activity.c", Path(directory) / "activity.exe"
    c.write_text(prefix + activity + cases)
    subprocess.run(["gcc", "-I", str(root), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
