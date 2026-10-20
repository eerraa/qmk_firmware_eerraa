#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production frame admission with controlled clock/driver readiness."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "keyboards/era/common/split/era_tomak_sync_runtime.c").read_text()


def function(name):
    start = re.search(r"^(?:bool|int8_t) " + name + r"\(", source, re.M).start()
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


prefix = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "keyboards/era/common/split/era_tomak_sync_receiver.h"
#define ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE 1
static era_tomak_sync_receiver_t receiver;
static uint32_t now;
static unsigned wakes;
static uint32_t timer_read32(void) { return now; }
static void era_split_transport_scheduler_mark_maintenance_due(unsigned flags) { ++wakes; }
"""
cases = r"""
static void prepare(void) {
    memset(&receiver,0,sizeof(receiver)); now=100;
    era_tomak_sync_transition_bind(&receiver.edge,1,2);
    assert(era_tomak_sync_transition_install(&receiver.edge,1,2,1,true,150,100,100,20,100));
    receiver.proof=(era_tomak_sync_wire_t){.owner_epoch=7,.relation_generation=8,.sequence=1,
        .deadline_ms=150,.target=true,.operation=ERA_TOMAK_SYNC_WIRE_INSTALLED};
}
int main(void) {
    assert(era_tomak_sync_runtime_output_gate(false,false)==1); /* No edge, unchanged output path. */
    prepare();
    assert(era_tomak_sync_runtime_output_gate(false,true)==-1); /* Stale pixels discarded. */
    assert(era_tomak_sync_runtime_output_gate(true,true)==0);
    assert(receiver.edge.phase==ERA_TOMAK_EDGE_INSTALLED);
    assert(era_tomak_sync_transition_arm(&receiver.edge,1,2,1,150));
    now=149; assert(era_tomak_sync_runtime_output_gate(true,true)==0);
    now=150; assert(era_tomak_sync_runtime_output_gate(true,true)==1);
    assert(receiver.edge.phase==ERA_TOMAK_EDGE_OUTPUT && !receiver.edge.displayed);
    assert(era_tomak_sync_runtime_output_gate(true,true)==0); /* No overwrite while DMA/PIO owns frame. */
    era_tomak_sync_receiver_output(&receiver,true);
    assert(receiver.edge.displayed);
    assert(era_tomak_sync_runtime_output_gate(true,true)==1);
    prepare(); now=150;
    assert(era_tomak_sync_runtime_output_gate(true,true)==-1); /* No ARM, no guessed output. */
    assert(receiver.proof.operation==ERA_TOMAK_SYNC_WIRE_REJECT);
    prepare(); assert(era_tomak_sync_transition_arm(&receiver.edge,1,2,1,150));
    now=150; assert(era_tomak_sync_runtime_output_gate(true,false)==-1);
    assert(receiver.edge.phase==ERA_TOMAK_EDGE_FAILED); /* Previous DMA still owns output. */
    prepare(); assert(era_tomak_sync_transition_arm(&receiver.edge,1,2,1,150));
    now=153; assert(era_tomak_sync_runtime_output_gate(true,true)==-1);
    assert(receiver.edge.phase==ERA_TOMAK_EDGE_FAILED);
    assert(wakes==3);
    puts("PASS prepared-frame hold/discard, exact launch, output ownership, unarmed/late/driver-busy failures");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-sync-output-gate-") as directory:
    directory = Path(directory)
    (directory / "matrix.h").write_text(
        "#pragma once\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\n")
    c, exe = directory / "gate.c", directory / "gate.exe"
    unit = prefix
    for name in ("era_tomak_sync_runtime_protected", "era_tomak_sync_runtime_launch", "era_tomak_sync_runtime_output_gate"):
        unit += function(name) + "\n"
    c.write_text(unit + cases)
    modules = ("era_tomak_sync_wire.c", "era_tomak_sync_transition.c", "era_tomak_sync_receiver.c")
    subprocess.run(["gcc", "-I", str(directory), "-I", str(root), str(c),
                    *(str(root / "keyboards/era/common/split" / name) for name in modules),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
