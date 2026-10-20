#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production LINK boundary must request fresh SESSION after channel replacement.

Backend lease/clock operations are supplied boundaries, not a physical-board
reproduction. The included production LINK dispatch owns the revalidation call.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
split = root / "keyboards/era/common/split"
source = (split / "era_split_transport_scheduler.c").read_text()
match = re.search(r"^void era_split_transport_scheduler_request_sync_session\(void\)", source, re.M)
brace = source.index("{", match.start())
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
request_session = source[match.start():end]
match = re.search(r"^static void era_split_transport_scheduler_sync_channel_restored\(bool initiator\)", source, re.M)
brace = source.index("{", match.start())
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
channel_restored = source[match.start():end]
start = source.index("    bool sync_paired =")
sync_block = source[start:source.index("\n#endif", start)]
prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ERA_TOMAK_SYNC_ENABLE
#define ERA_SPLIT_LINK_LEVEL_LOW 2
#define ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE 64
#define ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS 1
#define ERA_SPLIT_MODE_HOST_PEER_HOST 1
#define ERA_SPLIT_MODE_HOST_PEER_PEER 2
#define ERA_SPLIT_MODE_DUAL_HOST_LEFT 3
#define ERA_SPLIT_MODE_DUAL_HOST_RIGHT 4
typedef struct { unsigned unused; } era_tomak_sync_wire_t;
typedef struct { unsigned local_owner,local_relation; } era_tomak_sync_binding_t;
static era_tomak_sync_binding_t binding;
static struct {
    bool communication_core_started,local_wire_available,local_wire_initiator;
    bool responder_snapshot_publish_due,local_status_pending,attach_status_last_tx_valid;
    unsigned mode,core1_initiator_relation_generation;
} g_era_split_transport_scheduler;
static unsigned level,owner=3,relation=2,due,offers;
static bool fail_reset, accepted_edge;
static void era_split_transport_scheduler_sync_channel_restored(bool initiator);
static unsigned era_split_link_active_level(void) { return level; }
static bool era_split_transport_scheduler_stop_communication_core_for_flash_write(void) { return true; }
static unsigned era_split_link_speed(unsigned l) { return l; }
static bool era_split_transaction_backend_set_speed(unsigned l) { return true; }
static void era_split_link_note_level_selected(unsigned l) { level=l; }
static bool era_split_transport_scheduler_reset_serial_for_transport_role(bool available,bool initiator,bool rotate) {
    if(fail_reset) return false;
    /* This boundary mirrors the production reset's new backend lease and
     * optional relation rotation; no SESSION side effect is supplied. */
    owner+=2; if(rotate) ++relation;
    g_era_split_transport_scheduler.core1_initiator_relation_generation=relation;
    era_split_transport_scheduler_sync_channel_restored(initiator);
    return true;
}
static bool era_split_transport_scheduler_publish_communication_core_responder_snapshot(void) { return true; }
static bool era_split_transport_scheduler_publish_standing_plan(void) { return true; }
static bool era_split_transport_scheduler_standing_plan_stale(void) { return false; }
static void era_split_transport_scheduler_mark_dirty(unsigned f) {}
static void era_split_transport_scheduler_mark_route_due(unsigned f) { due|=f; }
static bool era_split_scheduler_session_peer_known(void) { return true; }
static unsigned era_split_communication_core_owner_epoch(void) { return owner; }
static void era_tomak_sync_runtime_bind(unsigned o,unsigned r) {
    if(binding.local_owner!=o || binding.local_relation!=r) {
        binding=(era_tomak_sync_binding_t){o,r}; accepted_edge=false;
    }
}
static void era_tomak_sync_runtime_snapshot(era_tomak_sync_binding_t *b,era_tomak_sync_wire_t *p) { *b=binding; }
static void era_tomak_sync_runtime_relation(bool paired) {}
static bool era_tomak_sync_runtime_transition_pending(void) { return accepted_edge; }
static bool era_host_peer_storage_advertised_pending(void) { return false; }
static unsigned era_host_peer_storage_indicator_diag(void) { return 2; }
static bool era_tomak_sync_runtime_request(bool work,era_tomak_sync_wire_t *q) {
    assert(work); ++offers; return true;
}
static bool era_split_transport_scheduler_submit_sync(const era_tomak_sync_wire_t *q) { return true; }
"""
cases = r"""
int main(void) {
    g_era_split_transport_scheduler.communication_core_started=true;
    g_era_split_transport_scheduler.local_wire_available=true;
    g_era_split_transport_scheduler.local_wire_initiator=true;
    g_era_split_transport_scheduler.attach_status_last_tx_valid=true;
    assert(era_split_transport_scheduler_apply_link_step(1,true));
    assert(owner==5 && relation==3);
    assert(g_era_split_transport_scheduler.local_status_pending &&
           !g_era_split_transport_scheduler.attach_status_last_tx_valid &&
           (due & ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS));
    puts("PASS LINK replacement queues ordinary SESSION with fresh channel identity");
    due=0; g_era_split_transport_scheduler.local_status_pending=false;
    accepted_edge=true;
    era_split_transport_scheduler_sync_channel_restored(true);
    assert(!due && !g_era_split_transport_scheduler.local_status_pending && accepted_edge);
    ++owner;
    /* Target initiator differs from the scheduler's not-yet-committed role. */
    g_era_split_transport_scheduler.local_wire_initiator=false;
    era_split_transport_scheduler_sync_channel_restored(true);
    assert(due && g_era_split_transport_scheduler.local_status_pending && !accepted_edge);
    assert(binding.local_owner==owner);
    g_era_split_transport_scheduler.local_wire_initiator=true;
    puts("PASS no-op preserves proof; owner-only replacement revokes proof and uses target initiator");
    g_era_split_transport_scheduler.mode=ERA_SPLIT_MODE_HOST_PEER_PEER;
    scheduler_sync_step();
    assert(!offers); /* Not just until the retry timer ceases to be due. */
    g_era_split_transport_scheduler.attach_status_last_tx_valid=true;
    scheduler_sync_step(); assert(!offers);
    /* A SESSION obligation must not suspend accepted ARM/output/receipt work.
     * This fixture supplies the phase predicate; the runtime fixture checks
     * that predicate against real endpoint states. */
    accepted_edge=true;
    scheduler_sync_step(); assert(offers==1);
    accepted_edge=false;
    /* The SESSION result owner clears this only after successful exchange. */
    g_era_split_transport_scheduler.local_status_pending=false;
    scheduler_sync_step(); assert(offers==2);
    puts("PASS production SYNC block waits through SESSION retry/in-flight phase before starting OFFER");
    due=0; g_era_split_transport_scheduler.local_status_pending=false;
    g_era_split_transport_scheduler.local_wire_initiator=false;
    assert(era_split_transport_scheduler_apply_link_step(0,true));
    assert(!due && !g_era_split_transport_scheduler.local_status_pending);
    puts("PASS responder stays responder; its new identity is published for the initiator's SESSION");
    g_era_split_transport_scheduler.local_wire_initiator=true;
    fail_reset=true;
    assert(!era_split_transport_scheduler_apply_link_step(1,true));
    assert(!due && !g_era_split_transport_scheduler.local_status_pending);
    puts("PASS failed lease rebuild does not announce a usable SESSION");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-sync-link-session-") as directory:
    c, exe = Path(directory) / "link.c", Path(directory) / "link.exe"
    c.write_text(prefix + request_session + "\n" + channel_restored + "\n" +
                 '#include "keyboards/era/common/split/scheduler/era_split_transport_scheduler_link.inc"\n' +
                 "static void scheduler_sync_step(void) {\n" + sync_block + "\n}\n" +
                 cases)
    subprocess.run(["gcc", "-I", str(root), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
