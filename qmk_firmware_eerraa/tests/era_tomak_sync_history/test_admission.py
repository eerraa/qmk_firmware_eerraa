#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the actual one-slot admission implementation, without QMK mocks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
cases = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "keyboards/era/common/system/era_tomak_sync_admission.h"
int main(void) {
    uint8_t first[32], second[32], out[32];
    for (unsigned i=0;i<32;++i) { first[i]=i; second[i]=255-i; }
    memset(out, 0x55, sizeof(out));
    assert(era_tomak_sync_admission_take(out,1,0,false)==ERA_TOMAK_ADMISSION_EMPTY);
    assert(era_tomak_sync_admission_hold(first,1,100,50));
    assert(era_tomak_sync_admission_pending());
    first[0]=77; /* Request is owned, not a pointer into the HID receive buffer. */
    assert(!era_tomak_sync_admission_hold(second,1,100,50));
    assert(era_tomak_sync_admission_take(out,1,149,false)==ERA_TOMAK_ADMISSION_WAITING);
    assert(era_tomak_sync_admission_pending());
    for (unsigned i=0;i<32;++i) assert(out[i]==0x55);
    assert(era_tomak_sync_admission_take(out,1,149,true)==ERA_TOMAK_ADMISSION_EXECUTE);
    assert(out[0]==0);
    for (unsigned i=1;i<32;++i) assert(out[i]==i);
    assert(!era_tomak_sync_admission_pending());
    assert(era_tomak_sync_admission_take(out,1,149,true)==ERA_TOMAK_ADMISSION_EMPTY);
    assert(era_tomak_sync_admission_hold(second,1,100,50));
    assert(era_tomak_sync_admission_take(out,1,150,false)==ERA_TOMAK_ADMISSION_EXPIRED);
    assert(!memcmp(out,second,32)); /* Owner can produce the original failure reply. */
    assert(era_tomak_sync_admission_hold(first,1,100,50));
    memset(out,0x55,32);
    assert(era_tomak_sync_admission_take(out,2,101,false)==ERA_TOMAK_ADMISSION_CANCELLED);
    for (unsigned i=0;i<32;++i) assert(out[i]==0x55);
    assert(era_tomak_sync_admission_hold(first,2,100,50));
    era_tomak_sync_admission_cancel();
    assert(era_tomak_sync_admission_take(out,2,101,true)==ERA_TOMAK_ADMISSION_EMPTY);
    assert(era_tomak_sync_admission_hold(second,2,UINT32_MAX-10,20));
    assert(era_tomak_sync_admission_take(out,2,8,true)==ERA_TOMAK_ADMISSION_EXECUTE);
    assert(era_tomak_sync_admission_hold(second,2,UINT32_MAX-10,20));
    assert(era_tomak_sync_admission_take(out,2,9,true)==ERA_TOMAK_ADMISSION_EXPIRED);
    puts("PASS owned single slot, overwrite refusal, one take, expiration, session cancellation, wrap");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-admission-") as directory:
    c = Path(directory) / "admission.c"
    exe = Path(directory) / "admission.exe"
    c.write_text(cases)
    subprocess.run(["gcc", "-I", str(root), str(c),
                    str(root / "keyboards/era/common/system/era_tomak_sync_admission.c"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
