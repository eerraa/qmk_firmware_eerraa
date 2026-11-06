#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Storage-owned route availability with a simulated Core1 slot."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
path = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "keyboards/era/common/split/scheduler/era_split_transport_scheduler_routes.c"
source = path.read_text()


def function(name):
    match = re.search(r"^(?:static )?(?:bool|void|uint16_t) " + name + r"\(", source, re.M)
    brace = source.index("{", match.start())
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


prefix = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ERA_HOST_PEER_STORAGE_V1_ENABLE
#include "keyboards/era/common/split/communication_core/era_split_communication_core_initiator.h"
#include "keyboards/era/common/split/era_split_mode_planner.h"
#define ERA_HOST_PEER_STORAGE_RETRY_MS 25U
static struct {
    bool local_status_pending, revalidation_active, core1_initiator_async_pending;
    bool core1_initiator_peer_known_before_request;
    uint16_t core1_initiator_relation_generation, core1_initiator_request_generation;
    uint16_t core1_initiator_pending_generation, core1_initiator_pending_standing_stop_generation;
    uint8_t last_owner_route, last_owner_reason, core1_initiator_pending_lane;
    uint8_t mode;
    uint32_t core1_initiator_pending_since_ms;
} g_era_split_transport_scheduler = {.core1_initiator_relation_generation=2,.mode=ERA_SPLIT_MODE_HOST_PEER_PEER};
static bool storage_pending, exclusive, input_pending;
static uint32_t now;
static uint32_t timer_read32(void) { return now; }
static bool timer_expired32(uint32_t a,uint32_t b) { return (int32_t)(a-b)>=0; }
static uint16_t era_split_communication_core_owner_epoch(void) { return 1; }
bool era_split_communication_core_initiator_pending(void) { return false; }
bool era_split_communication_core_initiator_result_ready(void) { return false; }
static bool era_split_scheduler_session_peer_known(void) { return true; }
static bool era_host_peer_storage_initiator_request_pending(void) { return storage_pending; }
static bool era_host_peer_storage_route_exclusive(void) { return exclusive; }
bool era_split_input_source_recovery_pending(void) { return input_pending; }
static uint16_t era_split_communication_core_standing_stop_generation(uint16_t o,uint16_t r) { return 0; }
static uint32_t era_split_transport_scheduler_core1_request_queue_window_us(void) { return 25000; }
"""
cases = r"""
int main(void) {
    assert(era_split_transport_scheduler_initiator_route_available());
    storage_pending=true;
    assert(!era_split_transport_scheduler_initiator_route_available());
    storage_pending=false; input_pending=true;
    assert(era_split_transport_scheduler_initiator_route_available());
    input_pending=false;
    g_era_split_transport_scheduler.core1_initiator_async_pending=true;
    assert(!era_split_transport_scheduler_initiator_route_available());
    puts("PASS storage-owned route availability: storage and Core1 work retain priority without presentation admission");
}
"""
names = ("era_split_transport_scheduler_core1_initiator_pending",
         "era_split_transport_scheduler_initiator_route_available",
         )
with tempfile.TemporaryDirectory(prefix="tomak-sync-route-") as tmp:
    out = Path(tmp)
    (out / "matrix.h").write_text("#pragma once\n#include <stdint.h>\ntypedef uint16_t matrix_row_t;\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\n")
    c, exe = out / "route.c", out / "route"
    c.write_text(prefix + "\n".join(function(name) for name in names) + cases)
    subprocess.run(["gcc", "-I", str(out), "-I", str(root), "-I", str(root / "tmk_core/protocol"),
                    str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
