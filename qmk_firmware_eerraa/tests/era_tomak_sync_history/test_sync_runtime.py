#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual runtime adapter at both endpoints, with clocks/session/render mocks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "keyboards/era/common/split/era_tomak_sync_runtime.c").read_text()
source = "\n".join(line for line in source.splitlines() if not line.startswith("#include"))
prefix = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "keyboards/era/common/split/era_tomak_sync_runtime.h"
#define ERA_SPLIT_PEER_RESPONSE_WINDOW_MS 20
#define ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE 1
typedef struct { uint16_t peer_usb_epoch,peer_host_open_generation,peer_host_close_generation; } era_split_scheduler_session_diagnostics_t;
static unsigned side;
static uint32_t now;
static uint16_t owner[2]={1,7}, relation[2]={2,8}, usb[2]={3,9};
static bool anchors=true;
static uint32_t timer_read32(void) { return now+(side?8000:4000); }
static uint32_t sync_timer_read32(void) { return now; }
static void era_split_scheduler_session_get_diagnostics_snapshot(era_split_scheduler_session_diagnostics_t *s) {
    *s=(era_split_scheduler_session_diagnostics_t){usb[1-side],1,1};
}
static bool era_split_scheduler_session_sync_peer_channel(uint16_t *o,uint16_t *r) {
    *o=owner[1-side]; *r=relation[1-side]; return true;
}
static bool era_host_peer_transaction_time_anchor_adopted(void) { return anchors; }
static void era_split_transport_scheduler_mark_maintenance_due(unsigned flags) {}
"""
cases = r"""
static struct {
    era_tomak_sync_receiver_t endpoint;
    uint8_t progress;
    bool paired;
} saved[2];
static void select_side(unsigned next) {
    saved[side].endpoint=receiver; saved[side].progress=peer_progress; saved[side].paired=pair_available;
    side=next;
    receiver=saved[side].endpoint; peer_progress=saved[side].progress; pair_available=saved[side].paired;
}
static void bind(void) {
    era_tomak_sync_runtime_bind(owner[side],relation[side]); era_tomak_sync_runtime_relation(true);
}
static void cycle(bool target) {
    select_side(0);
    era_tomak_sync_wire_t q,p;
    era_tomak_sync_binding_t b;
    uint32_t start=now;
    assert(era_tomak_sync_runtime_request(target,&q));
    assert(q.operation==ERA_TOMAK_SYNC_WIRE_OFFER);
    assert(era_tomak_sync_runtime_transition_pending());
    select_side(1); era_tomak_sync_runtime_snapshot(&b,&p);
    era_tomak_sync_runtime_receive(&b,&q);
    era_tomak_sync_runtime_snapshot(&b,&p);
    assert(p.operation==ERA_TOMAK_SYNC_WIRE_INSTALLED);
    select_side(0); era_tomak_sync_runtime_reply(&p);
    assert(era_tomak_sync_runtime_request(target,&q) && q.operation==ERA_TOMAK_SYNC_WIRE_ARM);
    assert(era_tomak_sync_runtime_transition_pending());
    select_side(1); era_tomak_sync_runtime_receive(&b,&q);
    era_tomak_sync_runtime_snapshot(&b,&p);
    select_side(0); era_tomak_sync_runtime_reply(&p);
    assert(!era_tomak_sync_runtime_pair_ready());
    now=start+119;
    assert(era_tomak_sync_runtime_output_gate(true,true)==0);
    select_side(1); assert(era_tomak_sync_runtime_output_gate(true,true)==0);
    now=start+120;
    assert(era_tomak_sync_runtime_output_gate(true,true)==1);
    assert(era_tomak_sync_runtime_output_pending());
    select_side(0); assert(era_tomak_sync_runtime_output_gate(true,true)==1);
    assert(!era_tomak_sync_runtime_pair_ready());
    now+=4; /* Mock completion includes DMA, PIO and reset-low. */
    era_tomak_sync_runtime_output(true);
    select_side(1); era_tomak_sync_runtime_output(true);
    assert(!era_tomak_sync_runtime_pair_ready());
    era_tomak_sync_runtime_snapshot(&b,&p);
    select_side(0); era_tomak_sync_runtime_reply(&p);
    assert(era_tomak_sync_runtime_transition_pending());
    assert(!era_tomak_sync_runtime_pair_ready());
    assert(era_tomak_sync_runtime_request(target,&q) && q.operation==ERA_TOMAK_SYNC_WIRE_APPLIED);
    select_side(1); era_tomak_sync_runtime_receive(&b,&q);
    assert(era_tomak_sync_runtime_pair_ready()==target);
    era_tomak_sync_runtime_snapshot(&b,&p);
    select_side(0); era_tomak_sync_runtime_reply(&p);
    assert(era_tomak_sync_runtime_pair_ready()==target);
    assert(!era_tomak_sync_runtime_request(target,&q)); /* Stable level needs no poll. */
    assert(!era_tomak_sync_runtime_transition_pending());
}
int main(void) {
    now=1000; bind(); select_side(1); bind();
    cycle(true);
    cycle(false);
    cycle(true);
    select_side(1); era_tomak_sync_runtime_preempt();
    assert(!era_tomak_sync_runtime_pair_ready() && era_tomak_sync_runtime_failed());
    era_tomak_sync_runtime_relation(false);
    assert(!era_tomak_sync_runtime_controls_output());
    ++usb[0]; ++relation[0]; ++relation[1];
    select_side(0); bind(); select_side(1); bind();
    /* A retained physical ON cannot authorize this relation without new proof. */
    assert(!era_tomak_sync_runtime_pair_ready());
    cycle(true);
    select_side(0); anchors=false;
    era_tomak_sync_wire_t q;
    assert(!era_tomak_sync_runtime_request(false,&q));
    puts("PASS real runtime paired ON/OFF/ON, bilateral output barrier, steady idle, preemption, relation replacement and clock readiness");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-sync-runtime-") as directory:
    directory = Path(directory)
    (directory / "matrix.h").write_text(
        "#pragma once\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\n")
    c, exe = directory / "runtime.c", directory / "runtime.exe"
    c.write_text(prefix + source + "\n" + cases)
    modules = ("era_tomak_sync_wire.c", "era_tomak_sync_transition.c", "era_tomak_sync_receiver.c")
    subprocess.run(["gcc", "-I", str(directory), "-I", str(root), str(c),
                    *(str(root / "keyboards/era/common/split" / name) for name in modules),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
