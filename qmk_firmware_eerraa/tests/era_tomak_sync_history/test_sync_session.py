#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production SESSION codec: explicit channel binding and legacy frame sizes."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "keyboards/era/common/split/era_split_wire_payload.c").read_text()


def function(name):
    start = re.search(r"^(?:static )?bool " + re.escape(name) + r"\(", source, re.M).start()
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
#include "keyboards/era/common/split/era_split_wire_protocol.h"
#define ERA_INPUT_SESSION 0x17
"""
cases = r"""
int main(void) {
    era_split_wire_session_status_t s={.accepted_no_host=true,.usb_epoch=1};
    uint8_t bytes[15], length;
    assert(era_split_wire_encode_session_status(1,&s,bytes,&length));
    assert(length==9 && era_split_wire_validate_session_status_payload(bytes,length));
#ifdef ERA_TOMAK_SYNC_ENABLE
    s.sync_owner_epoch=0x1234; s.sync_relation_generation=0xabcd;
    assert(era_split_wire_encode_session_status(1,&s,bytes,&length));
    assert(length==13 && bytes[9]==0x34 && bytes[10]==0x12 && bytes[11]==0xcd && bytes[12]==0xab);
    assert(era_split_wire_validate_session_status_payload(bytes,length));
    era_split_wire_frame_t frame={.kind=ERA_SPLIT_WIRE_PAYLOAD_SESSION_STATUS,.payload_len=13};
    memcpy(frame.payload,bytes,13);
    era_split_wire_session_status_t decoded={0};
    assert(era_split_wire_decode_session_status(&frame,&decoded));
    assert(decoded.sync_owner_epoch==s.sync_owner_epoch && decoded.sync_relation_generation==s.sync_relation_generation);
    frame.payload_len=9;
    assert(era_split_wire_decode_session_status(&frame,&decoded));
    assert(decoded.sync_owner_epoch==0 && decoded.sync_relation_generation==0);
    assert(era_split_wire_validate_session_status_payload(bytes,9)); /* INPUT's 9-byte SESSION-shaped prefix */
    assert(!era_split_wire_validate_session_status_payload(bytes,10));
    assert(!era_split_wire_validate_session_status_payload(bytes,11));
    assert(!era_split_wire_validate_session_status_payload(bytes,12));
    assert(!era_split_wire_validate_session_status_payload(bytes,14));
    bytes[9]=bytes[10]=0;
    assert(!era_split_wire_validate_session_status_payload(bytes,13));
    s.sync_owner_epoch=0;
    assert(!era_split_wire_encode_session_status(1,&s,bytes,&length));
#else
    assert(!era_split_wire_validate_session_status_payload(bytes,13));
#endif
    puts("PASS SESSION channel binding, legacy 9-byte form, input prefix compatibility, malformed channel refusal");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-sync-session-") as directory:
    directory = Path(directory)
    (directory / "matrix.h").write_text(
        "#pragma once\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\n")
    for enabled in (False, True):
        unit = ("#define ERA_TOMAK_SYNC_ENABLE\n" if enabled else "") + prefix
        for name in ("era_split_wire_validate_session_status_payload",
                     "era_split_wire_encode_session_status", "era_split_wire_decode_session_status"):
            unit += function(name) + "\n"
        c, exe = directory / "session.c", directory / "session.exe"
        c.write_text(unit + cases)
        subprocess.run(["gcc", "-I", str(directory), "-I", str(root), str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
