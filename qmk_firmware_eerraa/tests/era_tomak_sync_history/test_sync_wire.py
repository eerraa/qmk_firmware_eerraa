#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production transition codec and verify exact tuple receipts."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
cases = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "keyboards/era/common/split/era_tomak_sync_wire.h"
int main(void) {
    era_tomak_sync_wire_t request={.owner_epoch=0x1234,.relation_generation=0x5678,
        .sequence=0x9abc,.deadline_ms=0xfedcba98,.operation=ERA_TOMAK_SYNC_WIRE_OFFER,.target=true};
    uint8_t bytes[13], expected[]={0x79,0x30,0x34,0x12,0x78,0x56,0xbc,0x9a,0x98,0xba,0xdc,0xfe,1};
    era_tomak_sync_wire_t reply;
    assert(era_tomak_sync_wire_encode(&request,0x39,bytes));
    assert(!memcmp(bytes,expected,13));
    assert(era_tomak_sync_wire_decode(bytes,13,&reply));
    assert(era_tomak_sync_wire_matches(&request,&reply,ERA_TOMAK_SYNC_WIRE_OFFER));
    for (unsigned op=0x30;op<=0x37;++op) {
        reply=request; reply.operation=op;
        assert(era_tomak_sync_wire_encode(&reply,1,bytes));
        assert(era_tomak_sync_wire_decode(bytes,13,&reply));
        assert(era_tomak_sync_wire_matches(&request,&reply,op));
    }
    reply.operation=ERA_TOMAK_SYNC_WIRE_APPLIED;
    assert(!era_tomak_sync_wire_matches(&request,&reply,ERA_TOMAK_SYNC_WIRE_INSTALLED));
    reply.owner_epoch++; assert(!era_tomak_sync_wire_matches(&request,&reply,reply.operation)); reply.owner_epoch--;
    reply.relation_generation++; assert(!era_tomak_sync_wire_matches(&request,&reply,reply.operation)); reply.relation_generation--;
    reply.sequence++; assert(!era_tomak_sync_wire_matches(&request,&reply,reply.operation)); reply.sequence--;
    reply.deadline_ms++; assert(!era_tomak_sync_wire_matches(&request,&reply,reply.operation)); reply.deadline_ms--;
    reply.target=false; assert(!era_tomak_sync_wire_matches(&request,&reply,reply.operation));
    assert(era_tomak_sync_wire_encode(&request,1,bytes));
    assert(!era_tomak_sync_wire_decode(bytes,12,&reply));
    assert(!era_tomak_sync_wire_decode(bytes,14,&reply));
    bytes[12]=2; assert(!era_tomak_sync_wire_decode(bytes,13,&reply)); bytes[12]=1;
    bytes[0]=0x40; assert(!era_tomak_sync_wire_decode(bytes,13,&reply));
    bytes[0]=0xc1; assert(!era_tomak_sync_wire_decode(bytes,13,&reply));
    bytes[0]=1; assert(!era_tomak_sync_wire_decode(bytes,13,&reply));
    bytes[0]=0x41; bytes[1]=0x38; assert(!era_tomak_sync_wire_decode(bytes,13,&reply));
    for (unsigned offset=2;offset<=6;offset+=2) {
        assert(era_tomak_sync_wire_encode(&request,1,bytes));
        bytes[offset]=bytes[offset+1]=0;
        assert(!era_tomak_sync_wire_decode(bytes,13,&reply));
    }
    request.deadline_ms=0; /* A wrapped clock deadline is valid, not an idle sentinel. */
    assert(era_tomak_sync_wire_encode(&request,1,bytes));
    assert(era_tomak_sync_wire_decode(bytes,13,&reply));
    assert(!era_tomak_sync_wire_encode(&request,0,bytes));
    assert(!era_tomak_sync_wire_encode(&request,0x81,bytes));
    puts("PASS exact little-endian message, all phases, stale receipt tuple, malformed lengths/flags/identity, deadline wrap");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-sync-wire-") as directory:
    directory = Path(directory)
    # The codec uses no matrix API; the production protocol header only needs
    # these dimensions for its independent compile-time frame-size assertions.
    (directory / "matrix.h").write_text(
        "#pragma once\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\n")
    c = directory / "wire.c"
    exe = directory / "wire.exe"
    c.write_text(cases)
    subprocess.run(["gcc", "-I", str(directory), "-I", str(root), str(c),
                    str(root / "keyboards/era/common/split/era_tomak_sync_wire.c"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
