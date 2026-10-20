#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the real Core1 handoff with a bounded mocked result ring/snapshot."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "keyboards/era/common/split/communication_core/era_split_communication_core_responder_service.c").read_text()
name = "era_split_communication_core_sync_response"
start = re.search(r"^static bool " + name + r"\(", source, re.M).start()
brace = source.index("{", start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
posted = re.search(r"static struct \{\s*bool valid;\s*era_tomak_sync_binding_t binding;\s*era_tomak_sync_wire_t request;\s*\} sync_posted;", source).group()
prefix = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "keyboards/era/common/split/era_split_wire_protocol.h"
#include "keyboards/era/common/split/era_tomak_sync_receiver.h"
#define ERA_SPLIT_TRANSACTION_RESULT_OK 1
#define ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_TOMAK_SYNC 5
typedef int era_split_transaction_failure_t;
typedef struct {
    uint16_t owner_epoch, relation_generation, snapshot_generation;
    bool runtime_push_allowed;
    era_tomak_sync_binding_t sync_binding;
    era_tomak_sync_wire_t sync_proof;
} era_split_communication_core_responder_snapshot_t;
typedef struct {
    uint16_t owner_epoch, relation_generation, snapshot_generation;
    uint8_t kind, result;
    era_tomak_sync_binding_t sync_binding;
    era_tomak_sync_wire_t sync_request;
} era_split_communication_core_responder_result_t;
static bool ring_full, send_ok;
static unsigned posts, sends, commits;
static era_split_communication_core_responder_result_t event;
static era_tomak_sync_wire_t last_reply;
static bool era_split_communication_core_responder_prepare_result_slot(bool source, uint32_t *write, uint32_t *next) {
    *write=0; *next=1; return !ring_full;
}
static void era_split_communication_core_responder_publish_result_slot(bool source, uint32_t write, uint32_t next,
    const era_split_communication_core_responder_result_t *result) {
    assert(!ring_full); ring_full=true; event=*result; ++posts;
}
static void __SEV(void) {}
static void era_split_transaction_engine_commit_received_frame(const era_split_wire_frame_t *f) { ++commits; }
static bool era_split_transaction_engine_prepare_control(bool ext,uint8_t *control,uint8_t *seq) {
    *control=1; *seq=1; return true;
}
static int era_split_transaction_engine_send_compact_response_owned(int direction, uint8_t *payload,
    unsigned length, unsigned seq, unsigned owner, era_split_transaction_failure_t *failure) {
    assert(era_tomak_sync_wire_decode(payload,length,&last_reply)); ++sends; return send_ok;
}
"""
cases = r"""
int main(void) {
    era_split_communication_core_responder_snapshot_t s={
        .owner_epoch=1,.relation_generation=2,.snapshot_generation=3,.runtime_push_allowed=true,
        .sync_binding={.local_owner=1,.local_relation=2,.peer_owner=7,.peer_relation=8,.peer_usb_epoch=9}};
    era_tomak_sync_wire_t q={.owner_epoch=7,.relation_generation=8,.sequence=1,.deadline_ms=1050,
        .operation=ERA_TOMAK_SYNC_WIRE_OFFER,.target=true};
    era_split_wire_frame_t f={.payload_len=ERA_TOMAK_SYNC_WIRE_BYTES};
    assert(era_tomak_sync_wire_encode(&q,1,f.payload));
    send_ok=false; /* Lost PENDING must not cancel the Core0 event. */
    assert(era_split_communication_core_sync_response(1,&f,&s));
    assert(posts==1 && event.sync_request.sequence==1 && event.result==ERA_SPLIT_TRANSACTION_RESULT_OK);
    assert(last_reply.operation==ERA_TOMAK_SYNC_WIRE_PENDING);
    for(unsigned i=0;i<20;++i) era_split_communication_core_sync_response(1,&f,&s);
    assert(posts==1); /* Stalled Core0 cannot be flooded with duplicates. */
    ring_full=false; /* Core0 consumed it but has not published proof yet. */
    era_split_communication_core_sync_response(1,&f,&s);
    assert(posts==1 && last_reply.operation==ERA_TOMAK_SYNC_WIRE_PENDING);
    s.sync_proof=q; s.sync_proof.operation=ERA_TOMAK_SYNC_WIRE_INSTALLED;
    era_split_communication_core_sync_response(1,&f,&s);
    assert(posts==1 && last_reply.operation==ERA_TOMAK_SYNC_WIRE_INSTALLED);
    q.operation=ERA_TOMAK_SYNC_WIRE_ARM;
    assert(era_tomak_sync_wire_encode(&q,1,f.payload));
    era_split_communication_core_sync_response(1,&f,&s);
    assert(posts==2 && last_reply.operation==ERA_TOMAK_SYNC_WIRE_PENDING); /* OFFER proof isn't ARM proof. */
    ring_full=false; s.sync_proof.operation=ERA_TOMAK_SYNC_WIRE_ARMED;
    era_split_communication_core_sync_response(1,&f,&s);
    assert(posts==2 && last_reply.operation==ERA_TOMAK_SYNC_WIRE_ARMED);
    s.sync_proof.operation=ERA_TOMAK_SYNC_WIRE_APPLIED;
    era_split_communication_core_sync_response(1,&f,&s);
    assert(last_reply.operation==ERA_TOMAK_SYNC_WIRE_APPLIED);
    q.operation=ERA_TOMAK_SYNC_WIRE_APPLIED;
    assert(era_tomak_sync_wire_encode(&q,1,f.payload));
    era_split_communication_core_sync_response(1,&f,&s);
    assert(posts==3 && last_reply.operation==ERA_TOMAK_SYNC_WIRE_PENDING);
    /* Existing unilateral APPLIED proof cannot certify bilateral readiness. */
    ring_full=false;
    s.sync_proof.operation=ERA_TOMAK_SYNC_WIRE_PAIR_READY;
    era_split_communication_core_sync_response(1,&f,&s);
    assert(last_reply.operation==ERA_TOMAK_SYNC_WIRE_PAIR_READY && posts==3);
    era_split_communication_core_sync_response(1,&f,&s); /* Lost final response, no repost. */
    assert(last_reply.operation==ERA_TOMAK_SYNC_WIRE_PAIR_READY && posts==3);
    q.operation=ERA_TOMAK_SYNC_WIRE_ARM;
    assert(era_tomak_sync_wire_encode(&q,1,f.payload));
    era_split_communication_core_sync_response(1,&f,&s);
    assert(last_reply.operation==ERA_TOMAK_SYNC_WIRE_APPLIED && posts==3);
    ++q.deadline_ms; assert(era_tomak_sync_wire_encode(&q,1,f.payload));
    era_split_communication_core_sync_response(1,&f,&s);
    assert(last_reply.operation==ERA_TOMAK_SYNC_WIRE_REJECT && posts==3);
    ++s.sync_binding.peer_usb_epoch; s.sync_binding.peer_owner=10;
    era_split_communication_core_sync_response(1,&f,&s);
    assert(last_reply.operation==ERA_TOMAK_SYNC_WIRE_REJECT);
    s.sync_binding.peer_owner=7; s.runtime_push_allowed=false;
    era_split_communication_core_sync_response(1,&f,&s);
    assert(last_reply.operation==ERA_TOMAK_SYNC_WIRE_REJECT);
    assert(sends==commits);
    puts("PASS Core0-stall PENDING, one-event dedup, lost response, phase proofs, changed tuple and session fencing");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-sync-handoff-") as directory:
    directory = Path(directory)
    (directory / "matrix.h").write_text(
        "#pragma once\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\n")
    c, exe = directory / "handoff.c", directory / "handoff.exe"
    c.write_text(prefix + posted + "\n" + source[start:end] + "\n" + cases)
    modules = ("era_tomak_sync_wire.c", "era_tomak_sync_transition.c", "era_tomak_sync_receiver.c")
    subprocess.run(["gcc", "-I", str(directory), "-I", str(root), str(c),
                    *(str(root / "keyboards/era/common/split" / name) for name in modules),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
