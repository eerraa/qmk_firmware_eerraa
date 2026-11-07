#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute board STATUS against its storage-owned activity/receipt interface."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
board = (root / "keyboards/era/sirind/common/tomak_common.c").read_text()
link = (root / "keyboards/era/common/split/era_split_link.c").read_text()
link_header = (root / "keyboards/era/common/split/era_split_link.h").read_text()
rgb_header = (root / "quantum/rgb_matrix/rgb_matrix.h").read_text()
sync = (root / "keyboards/era/common/split/era_split_eeprom_sync.c").read_text()


def function(source, name):
    start = re.search(r"^(?:static )?(?:bool|void|int8_t) " + name + r"\(", source, re.M).start()
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
#include <string.h>
#define RGB_MATRIX_ENABLE
#define RGB_MATRIX_RENDER_POLICY_ENABLE
#define RGB_MATRIX_SLEEP
#define ERA_TOMAK_SYNC_ENABLE
#define ERA_SPLIT_EEPROM_SYNC_ENABLE
#define ERA_HOST_PEER_STORAGE_V1_ENABLE
#define RGB_MATRIX_LED_COUNT 3
#include "keyboards/era/common/system/era_status_terminal_pulse.h"
#include "keyboards/era/common/split/era_split_eeprom_sync.h"
static uint32_t now;
static bool asleep, active_content, indicator_pending, driver_ready=true;
static uint8_t pixels[3][3];
static uint32_t timer_read32(void) { return now; }
static uint32_t timer_elapsed32(uint32_t since) { return now - since; }
static bool era_host_peer_storage_content_attempt_active(void) { return active_content; }
static bool era_host_peer_storage_indicator_pending(void) { return indicator_pending; }
static void tomak_invalidate_indicator_render(void) {}
static bool rgb_matrix_get_suspend_state(void) { return asleep; }
static void rgb_matrix_set_color(unsigned i, uint8_t r, uint8_t g, uint8_t b) {
    pixels[i][0]=r; pixels[i][1]=g; pixels[i][2]=b;
}
static void ws2812_transfer_service(void) {}
static bool ws2812_transfer_ready(void) { return driver_ready; }
"""
rgb_start = rgb_header.index("typedef struct {", rgb_header.index("#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE)"))
rgb_types = rgb_header[rgb_start:rgb_header.index("void rgb_matrix_render_policy_kb(", rgb_start)]
link_start = link_header.rfind("typedef enum {", 0, link_header.index("ERA_SPLIT_LINK_APPLY_NONE"))
link_types = link_header[link_start:link_header.index("} era_split_link_apply_status_t;", link_start) + len("} era_split_link_apply_status_t;")]
fixtures = r"""
static bool tomak_launch_signal_active_cached, tomak_launch_signal_on_cached;
static bool tomak_link_fallback_active_cached, tomak_link_fallback_on_cached;
static bool tomak_link_apply_active_cached, tomak_link_apply_on_cached;
static era_split_link_apply_status_t tomak_link_apply_status_cached;
static bool tomak_link_reconcile_success_active_cached, tomak_link_reconcile_success_on_cached;
static bool tomak_eeprom_sync_status_visible_cached;
static void rgb_matrix_render_policy_user(rgb_matrix_render_policy_t *p) {}
static bool rgb_matrix_render_status_user(const rgb_matrix_render_policy_t *p) { return false; }
static void tomak_apply_indicator_render_policy(rgb_matrix_render_policy_t *p) {}
static struct {
    era_split_link_apply_status_t apply_status;
    bool apply_report_active;
    uint32_t apply_report_since_ms;
} g_era_split_link;
"""
cases = r"""
static rgb_matrix_render_policy_t render(void) {
    tomak_eeprom_sync_status_visibility_task();
    rgb_matrix_render_policy_t p = {0};
    memset(pixels,0,sizeof(pixels));
    rgb_matrix_render_policy_kb(&p);
    rgb_matrix_render_status_kb(&p);
    return p;
}
static void colour(unsigned r, unsigned g, unsigned b) {
    for (unsigned i=0;i<3;++i) {
        assert(pixels[i][0]==r && pixels[i][1]==g && pixels[i][2]==b);
    }
}
int main(void) {
    /* Preparation, upload, quiet and reconciliation share one owner span;
       content execution alone does not describe that operation. */
    assert(!(render().flags & RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE));
    era_tomak_sync_attempt_started_kb();
    now=10;
    indicator_pending=true; /* Preparation, before any content attempt. */
    tomak_link_reconcile_success_active_cached=true;
    tomak_link_reconcile_success_on_cached=true;
    render(); colour(0,0,255);
    now=20; /* Upload. */
    render(); colour(0,0,255);
    now=30; /* Quiet interval. */
    render(); colour(0,0,255);
    now=40; active_content=true;
    render(); colour(0,0,255);
    now=50; active_content=false; /* Reconciliation still owns the span. */
    render(); colour(0,0,255);
    now=51; indicator_pending=false;
    render(); colour(0,255,0);
    assert(!tomak_eeprom_sync_status_visible_cached);
    tomak_link_reconcile_success_active_cached=false;
    assert(!(render().flags & RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE));

    /* Storage supplies a terminal receipt. No pairing or output proof exists. */
    asleep=true;
    era_tomak_sync_failure_report_kb();
    /* Automatic equal-content audit admission cannot discard a hidden receipt. */
    era_tomak_sync_attempt_started_kb();
    assert(!(render().flags & RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE));
    now+=20000;
    tomak_sync_error_visibility_task();
    asleep=false;
    tomak_launch_signal_active_cached=true; /* Higher failure owns darkness too. */
    render(); colour(0,0,0);
    now+=1000;
    tomak_sync_error_visibility_task();
    tomak_launch_signal_active_cached=false;
    era_split_link_apply_feedback_set(ERA_SPLIT_LINK_APPLY_FAILED);
    tomak_link_apply_visibility_task();
    render(); colour(255,0,0);
    now+=800;
    tomak_sync_error_visibility_task();
    tomak_link_apply_visibility_task();
    /* LINK expires hidden; SYNC starts only on its first eligible selection. */
    indicator_pending=true;
    render(); colour(255,0,0);
    uint32_t first=now;
    now=first+200;
    era_tomak_sync_attempt_started_kb();
    render(); colour(255,0,0);
    now=first+639;
    /* Overlapping failures coalesce without renewing the original pulse. */
    era_tomak_sync_failure_report_kb();
    tomak_sync_error_visibility_task();
    render(); colour(255,0,0);
    now=first+640;
    tomak_sync_error_visibility_task();
    assert(render().flags & RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE);
    colour(0,0,0);
    indicator_pending=false;
    now=first+800;
    tomak_sync_error_visibility_task();
    assert(!(render().flags & RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE));

    /* A later terminal receipt can display after expiry, without an admission
       resetting presenter state or the driver acknowledging a frame. */
    era_tomak_sync_failure_report_kb();
    render(); colour(255,0,0);
    driver_ready=false;
    assert(rgb_matrix_sync_output_gate_kb(RGB_MATRIX_RENDER_FRAME_STATUS)==0);
    asleep=true;
    now+=900;
    tomak_sync_error_visibility_task();
    asleep=false;
    assert(!(render().flags & RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE));
    driver_ready=true;
    assert(rgb_matrix_sync_output_gate_kb(0)==1);
    era_split_link_apply_feedback_set(ERA_SPLIT_LINK_APPLY_APPLIED);
    now+=800;
    bool on=true;
    assert(!era_split_link_apply_report_advance(NULL,&on) && !on);
    puts("PASS operation-span blue without padding, finite/deferred failure receipt, coalesced failures, no output ACK");
}
"""

with tempfile.TemporaryDirectory(prefix="tomak-status-failure-") as directory:
    directory = Path(directory)
    state_start = board.index("#if defined(ERA_TOMAK_SYNC_ENABLE) && defined(RGB_MATRIX_ENABLE)",
                              board.index("static bool tomak_eeprom_sync_status_visible_cached;"))
    state_start = board.index("\n", state_start) + 1
    state_end = board.index("#endif", state_start)
    status_start = board.index("static bool    tomak_status_was_active;")
    status_end = board.index("#endif", status_start)
    unit = prefix + rgb_types + link_types + fixtures
    unit += "\n" + function(sync, "era_split_eeprom_sync_indicator_visible_advance") + "\n"
    unit += board[state_start:state_end] + "\n" + board[status_start:status_end]
    for name in ("era_split_link_apply_feedback_set", "era_split_link_apply_report_advance"):
        unit += "\n" + function(link, name)
    for name in ("tomak_eeprom_sync_status_visibility_task", "tomak_link_apply_visibility_task",
                 "rgb_matrix_render_policy_kb", "rgb_matrix_render_status_kb",
                 "rgb_matrix_sync_output_gate_kb"):
        unit += "\n" + function(board, name)
    c, exe = directory / "status.c", directory / "status.exe"
    c.write_text(unit + cases)
    subprocess.run(["gcc", "-I", str(root), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
