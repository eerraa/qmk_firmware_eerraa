#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Real VIA admission/runtime/receiver integration; wire, session and LED are mocks."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
split = root / "keyboards/era/common/split"
system = root / "keyboards/era/common/system"


def implementation(path):
    return "\n".join(line for line in path.read_text().splitlines()
                     if not line.startswith("#include"))


via_header = (root / "quantum/via.h").read_text()
enums = "\n".join(re.search(r"enum " + name + r" \{.*?\};", via_header, re.S).group()
                  for name in ("via_command_id", "via_keyboard_value_id"))
prefix = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ERA_TOMAK_SYNC_TIMING
#define uprintf printf
#include "keyboards/era/common/split/era_tomak_sync_runtime.h"
#include "keyboards/era/common/system/era_tomak_sync_admission.h"
#include "keyboards/era/common/system/era_wire.h"
#define ERA_SPLIT_PEER_RESPONSE_WINDOW_MS 20
#define ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE 1
#define ERA_SPLIT_SYNC_POLICY_FIELD_EEPROM 0
#define ERA_AUTH_USB_HOST_OPEN 1
#define USB_DEVICE_STATE_CONFIGURED 1
typedef struct { bool valid; unsigned usb_state; uint16_t usb_epoch; } era_authority_snapshot_t;
typedef struct { uint16_t peer_usb_epoch,peer_host_open_generation,peer_host_close_generation; } era_split_scheduler_session_diagnostics_t;
static unsigned side, executions, replies, output_count;
static uint32_t now;
static bool channel_known, anchor;
static uint8_t response[32];
static uint16_t owner[2]={1,7}, relation[2]={2,8};
static uint32_t timer_read32(void) { return now; }
static uint32_t sync_timer_read32(void) { return now; }
static bool sync_timer_is_time_source(void) { return side==1; }
static uint8_t era_host_peer_storage_indicator_diag(void) {
    return 4U | (era_tomak_sync_admission_pending() ? 1U : 0U);
}
static bool era_host_peer_storage_route_exclusive(void) { return false; }
static bool era_host_peer_transaction_time_anchor_adopted(void) { return anchor; }
static void era_split_scheduler_session_get_diagnostics_snapshot(era_split_scheduler_session_diagnostics_t *s) {
    *s=(era_split_scheduler_session_diagnostics_t){3,1,1};
}
static bool era_split_scheduler_session_sync_peer_channel(uint16_t *o,uint16_t *r) {
    *o=channel_known?owner[1-side]:0; *r=channel_known?relation[1-side]:0;
    return channel_known;
}
static void era_split_transport_scheduler_mark_maintenance_due(unsigned flags) {}
static bool era_split_sync_policy_get_requested(unsigned field, bool *value) { *value=true; return true; }
static void era_split_authority_reducer_get_snapshot(era_authority_snapshot_t *a) {
    *a=(era_authority_snapshot_t){true,ERA_AUTH_USB_HOST_OPEN,3};
}
static unsigned era_usb_session_configure_state(void) { return USB_DEVICE_STATE_CONFIGURED; }
static void raw_hid_send(uint8_t *q,unsigned length) {
    assert(length==32); memcpy(response,q,32); ++replies;
}
static void via_command_execute(uint8_t *q,unsigned length) {
    assert(era_tomak_sync_runtime_pair_ready()); ++executions;
    q[31]=ERA_WIRE_MACRO_REPLACE_RESULT_OK; raw_hid_send(q,length);
}
"""
cases = r"""
static struct { era_tomak_sync_receiver_t endpoint; uint8_t progress; bool paired; } saved[2];
static void select_side(unsigned next) {
    saved[side].endpoint=receiver; saved[side].progress=peer_progress; saved[side].paired=pair_available;
    side=next; receiver=saved[side].endpoint; peer_progress=saved[side].progress; pair_available=saved[side].paired;
}
static void reset(bool known,bool anchored) {
    era_tomak_sync_admission_cancel();
    memset(saved,0,sizeof(saved)); memset(&receiver,0,sizeof(receiver));
    peer_progress=0; pair_available=false; side=0; now=1000;
    channel_known=known; anchor=anchored; executions=replies=output_count=0;
    era_tomak_sync_runtime_bind(owner[0],relation[0]); era_tomak_sync_runtime_relation(true);
    select_side(1); era_tomak_sync_runtime_bind(owner[1],relation[1]); era_tomak_sync_runtime_relation(true);
    select_side(0);
}
static void command(uint8_t op) {
    uint8_t q[32]={id_custom_set_value,ERA_WIRE_CHANNELS_EERRAA_MACRO_REPLACE,op};
    assert(era_tomak_sync_via_admit(q,32)); assert(!executions);
}
static void exchange(void) {
    era_tomak_sync_wire_t q,p; era_tomak_sync_binding_t b;
    assert(side==0);
    if(!era_tomak_sync_runtime_request(era_tomak_sync_admission_pending(),&q)) return;
    select_side(1); era_tomak_sync_runtime_snapshot(&b,&p);
    era_tomak_sync_runtime_receive(&b,&q); era_tomak_sync_runtime_snapshot(&b,&p);
    select_side(0); era_tomak_sync_runtime_reply(&p);
}
static void render(void) {
    for(unsigned i=0;i<2;++i) {
        select_side(i);
        if(era_tomak_sync_runtime_output_gate(true,true)==1 && era_tomak_sync_runtime_output_pending()) {
            ++output_count; era_tomak_sync_runtime_output(true);
        }
    }
    select_side(0);
}
static void missing_prerequisite(bool known,bool anchored) {
    reset(known,anchored); command(ERA_WIRE_MACRO_REPLACE_BEGIN);
    for(unsigned i=0;i<500;++i) {
        exchange(); render(); era_tomak_sync_via_task(); ++now;
    }
    era_tomak_sync_via_task();
    assert(replies==1 && response[31]==ERA_WIRE_MACRO_REPLACE_RESULT_BUSY && !executions && !output_count);
    uint8_t abort_request[32]={id_custom_set_value,ERA_WIRE_CHANNELS_EERRAA_MACRO_REPLACE,ERA_WIRE_MACRO_REPLACE_ABORT};
    assert(!era_tomak_sync_via_admit(abort_request,32)); /* Existing nonce handler owns cleanup. */
    assert(!era_tomak_sync_admission_pending() && replies==1 && !executions);
}
int main(void) {
    missing_prerequisite(false,true);
    puts("PASS missing peer channel: BEGIN BUSY at 500ms, ABORT not presentation-gated, no LED launch");
    missing_prerequisite(true,false);
    puts("PASS missing clock anchor: BEGIN BUSY, ABORT not presentation-gated, no LED launch");
    reset(true,true); command(ERA_WIRE_MACRO_REPLACE_BEGIN);
    for(unsigned i=0;i<500 && !executions;++i) {
        exchange(); render(); era_tomak_sync_via_task(); ++now;
    }
    assert(executions==1 && replies==1 && response[31]==ERA_WIRE_MACRO_REPLACE_RESULT_OK);
    assert(output_count==2 && !era_tomak_sync_admission_pending());
    puts("PASS valid channel+clock: actual BEGIN executes once only after both output callbacks and PAIR_READY");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-sync-begin-pipeline-") as directory:
    directory = Path(directory)
    (directory / "matrix.h").write_text(
        "#pragma once\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\n")
    c, exe = directory / "pipeline.c", directory / "pipeline.exe"
    c.write_text(prefix + enums + "\n" +
                 implementation(split / "era_tomak_sync_runtime.c") + "\n" +
                 implementation(system / "era_tomak_sync_via.c") + "\n" + cases)
    subprocess.run(["gcc", "-I", str(directory), "-I", str(root), str(c),
                    str(system / "era_tomak_sync_admission.c"),
                    *(str(split / name) for name in
                      ("era_tomak_sync_wire.c", "era_tomak_sync_transition.c", "era_tomak_sync_receiver.c")),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
