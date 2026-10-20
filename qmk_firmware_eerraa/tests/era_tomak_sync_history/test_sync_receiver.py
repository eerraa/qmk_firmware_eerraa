#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production Core0 consumer: session fencing, phase proof and output receipt."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
cases = r"""
#include <assert.h>
#include <stdio.h>
#include "keyboards/era/common/split/era_tomak_sync_receiver.h"
static bool accept(era_tomak_sync_receiver_t *r, era_tomak_sync_binding_t *b, era_tomak_sync_wire_t *q) {
    return era_tomak_sync_receiver_accept(r,b,q,1000,5000,20,100);
}
int main(void) {
    era_tomak_sync_binding_t b={.local_owner=1,.local_relation=2,.peer_owner=7,.peer_relation=8,
        .peer_usb_epoch=3,.peer_host_open=4,.peer_host_close=5};
    era_tomak_sync_receiver_t r={0};
    era_tomak_sync_wire_t q={.owner_epoch=7,.relation_generation=8,.sequence=1,
        .deadline_ms=1050,.operation=ERA_TOMAK_SYNC_WIRE_OFFER,.target=true};
    era_tomak_sync_receiver_bind(&r,&b);
    assert(accept(&r,&b,&q)); /* Independent endpoints' channel numbers need not equal. */
    assert(r.proof.operation==ERA_TOMAK_SYNC_WIRE_INSTALLED);
    assert(!r.edge.displayed && r.edge.phase==ERA_TOMAK_EDGE_INSTALLED);
    uint32_t local=r.edge.local_ms;
    assert(accept(&r,&b,&q) && r.edge.local_ms==local);
    q.operation=ERA_TOMAK_SYNC_WIRE_ARM;
    assert(accept(&r,&b,&q));
    assert(r.proof.operation==ERA_TOMAK_SYNC_WIRE_ARMED);
    era_tomak_sync_receiver_output(&r,true); /* Not launched: no fabricated APPLIED. */
    assert(r.proof.operation==ERA_TOMAK_SYNC_WIRE_ARMED);
    assert(era_tomak_sync_transition_launch(&r.edge,5050,2));
    assert(r.proof.operation==ERA_TOMAK_SYNC_WIRE_ARMED); /* DMA submission only. */
    era_tomak_sync_receiver_output(&r,true);
    assert(r.edge.displayed && r.proof.operation==ERA_TOMAK_SYNC_WIRE_APPLIED);
    assert(accept(&r,&b,&q) && r.proof.operation==ERA_TOMAK_SYNC_WIRE_APPLIED);
    q.operation=ERA_TOMAK_SYNC_WIRE_OFFER;
    assert(accept(&r,&b,&q) && r.proof.operation==ERA_TOMAK_SYNC_WIRE_APPLIED);
    q.target=false; assert(!accept(&r,&b,&q)); q.target=true;
    era_tomak_sync_binding_t captured=b;
    ++b.peer_usb_epoch;
    era_tomak_sync_receiver_bind(&r,&b);
    assert(r.edge.displayed && !r.proof.sequence && !r.edge.applied_sequence);
    assert(!accept(&r,&captured,&q)); /* Enqueued in the old USB session. */
    assert(accept(&r,&b,&q));
    ++b.local_relation;
    era_tomak_sync_receiver_bind(&r,&b);
    assert(!accept(&r,&captured,&q));
    ++b.peer_owner;
    era_tomak_sync_receiver_bind(&r,&b);
    assert(!accept(&r,&b,&q)); /* Known session, wrong sender channel. */
    q.owner_epoch=b.peer_owner;
    q.operation=ERA_TOMAK_SYNC_WIRE_ARM;
    assert(!accept(&r,&b,&q)); /* ARM without this relation's OFFER. */
    q.operation=ERA_TOMAK_SYNC_WIRE_OFFER;
    assert(accept(&r,&b,&q));
    q.operation=ERA_TOMAK_SYNC_WIRE_ARM; assert(accept(&r,&b,&q));
    assert(era_tomak_sync_transition_launch(&r.edge,5050,2));
    era_tomak_sync_receiver_output(&r,false);
    assert(r.proof.operation==ERA_TOMAK_SYNC_WIRE_REJECT);
    assert(!accept(&r,&b,&q));
    puts("PASS independent endpoint identities, duplicate phases, real output proof, session/lease fencing and failed output");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-sync-receiver-") as directory:
    directory = Path(directory)
    (directory / "matrix.h").write_text(
        "#pragma once\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\n")
    c, exe = directory / "receiver.c", directory / "receiver.exe"
    c.write_text(cases)
    sources = ["era_tomak_sync_receiver.c", "era_tomak_sync_transition.c", "era_tomak_sync_wire.c"]
    subprocess.run(["gcc", "-I", str(directory), "-I", str(root), str(c),
                    *(str(root / "keyboards/era/common/split" / name) for name in sources),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
