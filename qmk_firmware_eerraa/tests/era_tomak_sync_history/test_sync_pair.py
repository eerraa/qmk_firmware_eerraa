#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production two-endpoint protocol: ON barriers and serialized OFF/new work."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
cases = r"""
#include <assert.h>
#include <stdio.h>
#include "keyboards/era/common/split/era_tomak_sync_receiver.h"
static void edge(era_tomak_sync_receiver_t *l, era_tomak_sync_receiver_t *r,
                 uint8_t *progress, bool target, uint32_t now) {
    era_tomak_sync_wire_t q, pending;
    assert(era_tomak_sync_coordinator_request(l,progress,target,now,now+4000,120,&q));
    assert(q.operation==ERA_TOMAK_SYNC_WIRE_OFFER && q.target==target);
    pending=q; pending.operation=ERA_TOMAK_SYNC_WIRE_PENDING;
    assert(era_tomak_sync_coordinator_reply(l,progress,&pending,now+4001));
    assert(*progress==0 && !era_tomak_sync_coordinator_ready(l,*progress));
    assert(era_tomak_sync_receiver_accept(r,&r->binding,&q,now+10,now+8010,20,120));
    assert(era_tomak_sync_coordinator_reply(l,progress,&r->proof,now+4020));
    assert(*progress==ERA_TOMAK_SYNC_WIRE_INSTALLED);
    /* A new edit during the OFF proposal cannot overwrite the committed tuple. */
    assert(era_tomak_sync_coordinator_request(l,progress,true,now+20,now+4020,120,&q));
    assert(q.operation==ERA_TOMAK_SYNC_WIRE_ARM && q.target==target);
    assert(era_tomak_sync_receiver_accept(r,&r->binding,&q,now+30,now+8030,20,120));
    assert(era_tomak_sync_coordinator_reply(l,progress,&r->proof,now+4040));
    assert(l->edge.phase==ERA_TOMAK_EDGE_ARMED && r->edge.phase==ERA_TOMAK_EDGE_ARMED);
    assert(!era_tomak_sync_coordinator_ready(l,*progress));
    assert(!era_tomak_sync_transition_launch(&l->edge,now+4119,2));
    assert(era_tomak_sync_transition_launch(&l->edge,now+4120,2));
    assert(era_tomak_sync_transition_launch(&r->edge,now+8120,2));
    era_tomak_sync_receiver_output(l,true);
    assert(!era_tomak_sync_coordinator_ready(l,*progress)); /* Peer only submitted so far. */
    era_tomak_sync_receiver_output(r,true);
    assert(!era_tomak_sync_receiver_ready(r)); /* Local output alone can't release the responder's writer. */
    assert(era_tomak_sync_coordinator_reply(l,progress,&r->proof,now+4124));
    assert(l->edge.displayed==target && r->edge.displayed==target);
    assert(!era_tomak_sync_coordinator_ready(l,*progress));
    assert(era_tomak_sync_coordinator_request(l,progress,true,now+125,now+4125,120,&q));
    assert(q.operation==ERA_TOMAK_SYNC_WIRE_APPLIED && q.target==target);
    assert(era_tomak_sync_receiver_accept(r,&r->binding,&q,now+130,now+8130,20,120));
    assert(r->proof.operation==ERA_TOMAK_SYNC_WIRE_PAIR_READY);
    assert(era_tomak_sync_receiver_ready(r)==target);
    assert(!era_tomak_sync_coordinator_ready(l,*progress)); /* Lost final reply: wait, don't write. */
    assert(era_tomak_sync_receiver_accept(r,&r->binding,&q,now+140,now+8140,20,120));
    assert(era_tomak_sync_coordinator_reply(l,progress,&r->proof,now+4150));
    assert(era_tomak_sync_coordinator_ready(l,*progress)==target);
    q.operation=ERA_TOMAK_SYNC_WIRE_OFFER;
    assert(era_tomak_sync_receiver_accept(r,&r->binding,&q,now+150,now+8150,20,120));
    assert(r->proof.operation==ERA_TOMAK_SYNC_WIRE_PAIR_READY);
    assert(era_tomak_sync_receiver_ready(r)==target);
    q.operation=ERA_TOMAK_SYNC_WIRE_ARM;
    assert(era_tomak_sync_receiver_accept(r,&r->binding,&q,now+150,now+8150,20,120));
    assert(r->proof.operation==ERA_TOMAK_SYNC_WIRE_PAIR_READY);
}
int main(void) {
    era_tomak_sync_receiver_t l={0}, r={0};
    era_tomak_sync_binding_t lb={.local_owner=1,.local_relation=2,.peer_owner=7,.peer_relation=8};
    era_tomak_sync_binding_t rb={.local_owner=7,.local_relation=8,.peer_owner=1,.peer_relation=2};
    era_tomak_sync_receiver_bind(&l,&lb); era_tomak_sync_receiver_bind(&r,&rb);
    uint8_t progress=0;
    edge(&l,&r,&progress,true,1000);
    era_tomak_sync_wire_t q;
    assert(!era_tomak_sync_coordinator_request(&l,&progress,true,1200,5200,120,&q)); /* Stable ON, no chatter. */
    edge(&l,&r,&progress,false,1300);
    assert(!era_tomak_sync_coordinator_ready(&l,progress));
    edge(&l,&r,&progress,true,1600); /* Buffered edit opens the next span, not cancellation. */
    assert(l.edge.sequence==3 && r.edge.sequence==3);
    era_tomak_sync_binding_t old=lb;
    ++lb.local_relation; era_tomak_sync_receiver_bind(&l,&lb); progress=0;
    assert(!era_tomak_sync_coordinator_ready(&l,progress));
    assert(era_tomak_sync_coordinator_request(&l,&progress,false,2000,6000,120,&q));
    q.operation=ERA_TOMAK_SYNC_WIRE_INSTALLED;
    assert(era_tomak_sync_coordinator_reply(&l,&progress,&q,6010));
    q.operation=ERA_TOMAK_SYNC_WIRE_APPLIED;
    assert(!era_tomak_sync_coordinator_reply(&l,&progress,&q,6121)); /* Can't arm after the deadline. */
    assert(l.edge.phase==ERA_TOMAK_EDGE_FAILED);
    assert(!era_tomak_sync_coordinator_ready(&l,progress));
    q.operation=ERA_TOMAK_SYNC_WIRE_ARMED;
    assert(!era_tomak_sync_coordinator_reply(&l,&progress,&q,6122));
    puts("PASS pair ON output barrier, no steady chatter, OFF/new edit serialization, exact receipts and late-arm failure");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-sync-pair-") as directory:
    directory = Path(directory)
    (directory / "matrix.h").write_text(
        "#pragma once\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\n")
    c, exe = directory / "pair.c", directory / "pair.exe"
    c.write_text(cases)
    modules = ("era_tomak_sync_wire.c", "era_tomak_sync_transition.c", "era_tomak_sync_receiver.c")
    subprocess.run(["gcc", "-I", str(directory), "-I", str(root), str(c),
                    *(str(root / "keyboards/era/common/split" / name) for name in modules),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
