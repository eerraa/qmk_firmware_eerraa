"""Compile the production physical-to-visual scheduler entry against deterministic peers."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SPLIT = ROOT / "keyboards/era/common/split"


def function(source, name):
    match = re.search(rf"^(?:static inline )?(?:void|bool) {name}\([^;]*?\)\s*\{{", source, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


source = (SPLIT / "era_split_transport_scheduler.c").read_text(encoding="utf-8")
entry = function(source, "era_split_transport_scheduler_transport_step")
note = function(source, "era_split_transport_scheduler_note_local_visual_change")
fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
enum { ERA_SPLIT_MODE_LOCAL_NO_LINK, ERA_SPLIT_MODE_HOST_PEER_HOST,
       ERA_SPLIT_MODE_HOST_PEER_PEER, ERA_SPLIT_MODE_DUAL_HOST_LEFT,
       ERA_SPLIT_MODE_DUAL_HOST_RIGHT };
#define ERA_SPLIT_SCHEDULER_ROUTE_DUE_DUAL_RUNTIME_PUSH 8
static struct { int mode; bool rgb_sync_requested_cached; uint8_t route_due_flags; }
    g_era_split_transport_scheduler;
static bool changed;
static unsigned slow_calls, marks, responder_calls;
static void era_split_transport_scheduler_ensure_initialized(void) {}
static void era_split_transport_scheduler_note_transport_step_call(void) {}
static bool era_matrix_engine_local_changed(void) { return changed; }
static void era_split_transport_scheduler_mark_route_due(uint8_t bit) {
    g_era_split_transport_scheduler.route_due_flags |= bit; ++marks;
}
static void era_split_transport_scheduler_publish_host_peer_responder_visual_snapshot(void) {
    ++responder_calls;
}
static bool era_split_transport_scheduler_scan_idle(void) {
    return g_era_split_transport_scheduler.route_due_flags == 0;
}
static bool era_split_transport_scheduler_transport_slow(void) {
    ++slow_calls;
    // The existing publication consumer retires this bit after capturing current rows.
    g_era_split_transport_scheduler.route_due_flags = 0;
    return true;
}
'''
checks = r'''
int main(void) {
    for (int mode = 0; mode <= ERA_SPLIT_MODE_DUAL_HOST_RIGHT; ++mode) {
        for (unsigned rgb = 0; rgb < 2; ++rgb) {
            g_era_split_transport_scheduler.mode = mode;
            g_era_split_transport_scheduler.rgb_sync_requested_cached = rgb;
            marks = slow_calls = responder_calls = 0;
            // No semantic action is dispatched: LT may still be undecided or filtered.
            changed = true;
            era_split_transport_scheduler_transport_step();
            unsigned expected = mode == ERA_SPLIT_MODE_DUAL_HOST_LEFT && rgb;
            assert(marks == expected && slow_calls == expected);
            // Idle must not republish. A later physical release must wake independently.
            changed = false;
            era_split_transport_scheduler_transport_step();
            assert(marks == expected && slow_calls == expected);
            changed = true;
            era_split_transport_scheduler_transport_step();
            assert(marks == 2 * expected && slow_calls == 2 * expected);
            assert(responder_calls == 3);
        }
    }
    puts("PASS: production visual wake before scan-idle; physical press/release, all roles, RGB off, idle");
}
'''
with tempfile.TemporaryDirectory(prefix="era-visual-input-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(fixture + note + entry + checks, encoding="utf-8")
    subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-O2", "-Wall", "-Wextra",
                    "-Werror", "-Wno-unused-function", str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)

keyboard = (SPLIT / "era_split_keyboard.c").read_text(encoding="utf-8")
assert "era_split_transport_scheduler_note_local_visual_change" not in function(keyboard, "era_split_keyboard_process_record")
print("PASS: semantic replay does not produce visual wake")
