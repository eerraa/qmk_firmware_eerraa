#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production single-transition logic; two clocks and actual output events."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
cases = r"""
#include <assert.h>
#include <stdio.h>
#include "keyboards/era/common/split/era_tomak_sync_transition.h"
typedef era_tomak_sync_transition_t Edge;
static bool install(Edge *e, unsigned sequence, bool target, uint32_t wire, uint32_t shared, uint32_t local) {
    return era_tomak_sync_transition_install(e,1,2,sequence,target,wire,shared,local,20,100);
}
static bool arm(Edge *e, unsigned sequence, uint32_t wire) {
    return era_tomak_sync_transition_arm(e,1,2,sequence,wire);
}
int main(void) {
    Edge l={0}, r={0};
    era_tomak_sync_transition_bind(&l,1,2);
    era_tomak_sync_transition_bind(&r,1,2);
    assert(!install(&l,1,true,1010,1000,2000)); /* insufficient lead */
    assert(!install(&l,1,true,1200,1000,2000)); /* unreasonable future */
    assert(!install(&l,1,true,999,1000,2000));  /* already missed */
    assert(!era_tomak_sync_transition_install(&l,9,2,1,true,1050,1000,2000,20,100));
    assert(install(&l,1,true,1050,1000,2000));
    assert(install(&r,1,true,1050,1010,5010)); /* same real edge, different local clocks */
    assert(l.local_ms==2050 && r.local_ms==5050);
    assert(!install(&l,2,false,1060,1000,2000));
    assert(!install(&l,1,false,1050,1000,2000));
    assert(!install(&l,1,true,1060,1000,2000));
    /* Clock correction on duplicate delivery cannot move an accepted edge. */
    assert(install(&l,1,true,1050,9000,2100) && l.local_ms==2050);
    assert(!arm(&l,2,1050));
    assert(!arm(&l,1,1051));
    assert(!era_tomak_sync_transition_launch(&l,2050,2)); /* not confirmed */
    assert(arm(&l,1,1050) && arm(&r,1,1050));
    assert(!era_tomak_sync_transition_launch(&l,2049,2));
    assert(era_tomak_sync_transition_launch(&l,2050,2));
    assert(era_tomak_sync_transition_launch(&r,5050,2));
    assert(!era_tomak_sync_transition_launch(&l,2050,2)); /* exactly one launch */
    assert(!l.displayed && !l.applied_sequence); /* submission is not application */
    assert(!install(&l,2,false,1100,1050,2050));
    era_tomak_sync_transition_output(&l,true);
    era_tomak_sync_transition_output(&r,true);
    assert(l.displayed && r.displayed && l.applied_sequence==1);
    assert(install(&l,1,true,1050,1100,2100)); /* repeat receipt, never replay */
    assert(arm(&l,1,1050));
    assert(!era_tomak_sync_transition_launch(&l,2100,2));
    assert(install(&l,2,false,1150,1100,2100));
    assert(arm(&l,2,1150));
    assert(era_tomak_sync_transition_launch(&l,2150,2));
    era_tomak_sync_transition_output(&l,false);
    assert(l.displayed && l.applied_sequence==1); /* failed OFF isn't successful clear */
    assert(!install(&l,2,false,1150,1100,2100));
    era_tomak_sync_transition_bind(&l,1,3);
    assert(l.displayed); /* rebinding does not claim physical LED clearing */
    assert(!arm(&l,2,1150)); /* previous relation cannot arm */
    era_tomak_sync_transition_bind(&l,1,2);
    assert(install(&l,1,false,1050,1000,2000));
    assert(arm(&l,1,1050));
    assert(!era_tomak_sync_transition_launch(&l,2053,2));
    assert(l.phase==ERA_TOMAK_EDGE_FAILED);
    era_tomak_sync_transition_bind(&l,2,2);
    assert(era_tomak_sync_transition_install(&l,2,2,1,false,9,UINT32_MAX-40,UINT32_MAX-20,20,100));
    assert(l.local_ms==29);
    assert(era_tomak_sync_transition_arm(&l,2,2,1,9));
    assert(era_tomak_sync_transition_launch(&l,29,2));
    era_tomak_sync_transition_output(&l,true);
    assert(!l.displayed);
    l.sequence=UINT16_MAX; /* relation must rotate before sequence wraps */
    assert(!era_tomak_sync_transition_install(&l,2,2,1,true,100,50,50,20,100));
    era_tomak_sync_transition_cancel(&r);
    assert(r.displayed && r.phase==ERA_TOMAK_EDGE_FAILED);
    puts("PASS paired clocks, stable deadlines, identity, serialized edges, output receipt, missed edge, failure, wraps");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-transition-") as directory:
    c = Path(directory) / "transition.c"
    exe = Path(directory) / "transition.exe"
    c.write_text(cases)
    subprocess.run(["gcc", "-I", str(root), str(c),
                    str(root / "keyboards/era/common/split/era_tomak_sync_transition.c"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
