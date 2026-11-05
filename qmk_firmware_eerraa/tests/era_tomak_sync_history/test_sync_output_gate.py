#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the production RGB flush with a physical-frame-only board gate."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
board = (root / "keyboards/era/sirind/common/tomak_common.c").read_text()
rgb = (root / "quantum/rgb_matrix/rgb_matrix.c").read_text()


def function(source, name):
    start = re.search(r"^(?:static )?(?:void|int8_t) " + name + r"\(", source, re.M).start()
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ERA_TOMAK_SYNC_ENABLE
#define RGB_MATRIX_RENDER_POLICY_ENABLE
enum { STARTING, SYNCING, FLUSHING };
static bool ready, rgb_last_enable;
static bool rgb_render_policy_refresh_pending, rgb_render_policy_refresh_in_progress;
static uint8_t rgb_render_frame_flags, rgb_last_effect, rgb_task_state;
static struct { bool enable; } rgb_matrix_config = {true};
static unsigned services, submissions, receipts;
static void ws2812_transfer_service(void) { ++services; }
static bool ws2812_transfer_ready(void) { return ready; }
static void rgb_matrix_update_pwm_buffers(void) { ++submissions; }
static void rgb_matrix_render_policy_flush_kb(uint8_t flags) { ++receipts; }
"""
cases = r"""
int main(void) {
    rgb_task_state=FLUSHING;
    rgb_render_frame_flags=4;
    rgb_render_policy_refresh_in_progress=true;
    rgb_task_flush(3);
    assert(services==1 && submissions==0 && receipts==0);
    assert(rgb_task_state==FLUSHING && rgb_render_frame_flags==4);
    ready=true;
    rgb_task_flush(3);
    assert(submissions==1 && receipts==1 && rgb_render_frame_flags==0);
    assert(rgb_task_state==SYNCING && !rgb_render_policy_refresh_in_progress);
    /* Normal effect frames obey exactly the same physical ownership rule. */
    ready=false;
    rgb_task_state=FLUSHING;
    rgb_render_frame_flags=1;
    rgb_task_flush(7);
    assert(submissions==1 && receipts==1 && rgb_render_frame_flags==1);
    ready=true;
    rgb_render_policy_refresh_pending=true;
    rgb_task_flush(7);
    assert(submissions==2 && receipts==2 && rgb_render_frame_flags==0);
    assert(rgb_task_state==STARTING && rgb_last_effect==7 && rgb_last_enable);
    puts("PASS production RGB flush defers only for physical-frame ownership, without semantic admission");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-sync-output-gate-") as directory:
    directory = Path(directory)
    c, exe = directory / "gate.c", directory / "gate.exe"
    c.write_text(prefix + function(board, "rgb_matrix_sync_output_gate_kb") +
                 "\n" + function(rgb, "rgb_task_flush") + cases)
    subprocess.run(["gcc", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
