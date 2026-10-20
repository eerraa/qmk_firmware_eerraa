#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual admission adapter and slot with mocked host/session/output boundaries."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "keyboards/era/common/system/era_tomak_sync_via.c").read_text()
source = "\n".join(line for line in source.splitlines() if not line.startswith("#include"))
via = (root / "quantum/via.h").read_text()
enums = "\n".join(re.search(r"enum " + name + r" \{.*?\};", via, re.S).group()
                  for name in ("via_command_id", "via_keyboard_value_id"))
prefix = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "keyboards/era/common/system/era_tomak_sync_admission.h"
#include "keyboards/era/common/system/era_wire.h"
#define ERA_AUTH_USB_HOST_OPEN 1
#define USB_DEVICE_STATE_CONFIGURED 1
#define ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE 1
#define ERA_SPLIT_SYNC_POLICY_FIELD_EEPROM 0
typedef struct { bool valid; unsigned usb_state; uint16_t usb_epoch; } era_authority_snapshot_t;
static era_authority_snapshot_t auth={true,1,7};
static uint32_t now;
static bool paired=true, ready, failed, configured=true;
static bool policy=true;
static bool era_split_sync_policy_get_requested(unsigned field, bool *requested) { *requested=policy; return true; }
static unsigned executions, replies, preemptions;
static uint8_t executed[32], response[32];
static uint32_t timer_read32(void) { return now; }
static void era_split_authority_reducer_get_snapshot(era_authority_snapshot_t *out) { *out=auth; }
static unsigned era_usb_session_configure_state(void) { return configured ? 1 : 0; }
static bool era_tomak_sync_runtime_paired(void) { return paired; }
static bool era_tomak_sync_runtime_pair_ready(void) { return ready; }
static bool era_tomak_sync_runtime_failed(void) { return failed; }
static void era_tomak_sync_runtime_preempt(void) { ++preemptions; }
static void era_split_transport_scheduler_mark_maintenance_due(unsigned flags) {}
static void raw_hid_send(uint8_t *data, unsigned length) { assert(length==32); memcpy(response,data,32); ++replies; }
static void via_command_execute(uint8_t *data, unsigned length) {
    assert(ready && !failed); assert(length==32); memcpy(executed,data,32); ++executions;
    raw_hid_send(data,length);
}
"""
cases = r"""
static void reset(void) {
    era_tomak_sync_admission_cancel(); now=100; paired=true; ready=false; failed=false;
    configured=policy=true; auth=(era_authority_snapshot_t){true,1,7}; executions=replies=preemptions=0;
}
int main(void) {
    uint8_t q[32]={id_custom_set_value,ERA_WIRE_CHANNELS_EERRAA_MACRO_REPLACE,ERA_WIRE_MACRO_REPLACE_BEGIN};
    reset();
    assert(era_tomak_sync_via_admit(q,32));
    assert(era_tomak_sync_admission_pending() && !executions && !replies);
    era_tomak_sync_via_task(); assert(!executions && !replies);
    uint8_t other[32]={id_dynamic_keymap_macro_reset};
    assert(era_tomak_sync_via_admit(other,32));
    assert(replies==1 && response[0]==id_unhandled && era_tomak_sync_admission_pending());
    ready=true; era_tomak_sync_via_task();
    assert(executions==1 && !memcmp(executed,q,32));
    era_tomak_sync_via_task(); assert(executions==1);
    assert(!era_tomak_sync_via_admit(q,32)); /* Normal ON path unchanged. */
    reset(); assert(era_tomak_sync_via_admit(q,32));
    ++auth.usb_epoch; ready=true; era_tomak_sync_via_task();
    assert(!executions && !replies && !era_tomak_sync_admission_pending());
    reset(); assert(era_tomak_sync_via_admit(q,32));
    configured=false; era_tomak_sync_via_task();
    assert(!executions && !replies && !era_tomak_sync_admission_pending());
    reset(); assert(era_tomak_sync_via_admit(q,32));
    now=600; era_tomak_sync_via_task();
    assert(!executions && replies==1 && response[31]==ERA_WIRE_MACRO_REPLACE_RESULT_BUSY);
    assert(!memcmp(response,q,31)); /* Original ERA echo retained. */
    reset();
    q[3]=17;
    assert(era_tomak_sync_via_admit(q,32));
    uint8_t abort_request[32];
    memcpy(abort_request,q,32); abort_request[2]=ERA_WIRE_MACRO_REPLACE_ABORT;
    abort_request[3]=18;
    assert(!era_tomak_sync_via_admit(abort_request,32));
    assert(era_tomak_sync_admission_pending() && !replies);
    abort_request[3]=17;
    ++auth.usb_epoch;
    assert(!era_tomak_sync_via_admit(abort_request,32));
    assert(era_tomak_sync_admission_pending());
    --auth.usb_epoch;
    assert(era_tomak_sync_via_admit(abort_request,32));
    assert(!era_tomak_sync_admission_pending() && replies==1 &&
           response[31]==ERA_WIRE_MACRO_REPLACE_RESULT_OK);
    ready=true; era_tomak_sync_via_task(); assert(!executions);
    reset(); other[0]=id_dynamic_keymap_macro_reset;
    assert(era_tomak_sync_via_admit(other,32));
    assert(!era_tomak_sync_via_admit(abort_request,32));
    assert(era_tomak_sync_admission_pending() && !replies);
    reset(); failed=true;
    assert(!era_tomak_sync_via_admit(abort_request,32));
    assert(!era_tomak_sync_admission_pending() && !replies);
    reset(); assert(era_tomak_sync_via_admit(q,32)); failed=true; era_tomak_sync_via_task();
    assert(!executions && replies==1);
    reset();
    for (unsigned command=1;command<=0x15;++command) {
        uint8_t read[32]={command};
        if (command==id_get_protocol_version || command==id_get_keyboard_value ||
            command==id_dynamic_keymap_get_keycode || command==id_custom_get_value ||
            command==id_dynamic_keymap_macro_get_count || command==id_dynamic_keymap_macro_get_buffer_size ||
            command==id_dynamic_keymap_macro_get_buffer || command==id_dynamic_keymap_get_layer_count ||
            command==id_dynamic_keymap_get_buffer || command==id_dynamic_keymap_get_encoder) {
            assert(!era_tomak_sync_via_admit(read,32));
        }
    }
    uint8_t cap[32]={id_custom_get_value,ERA_WIRE_CHANNELS_EERRAA_MACRO_REPLACE,ERA_WIRE_MACRO_REPLACE_STATUS};
    assert(!era_tomak_sync_via_admit(cap,32));
    reset(); assert(era_tomak_sync_via_admit(q,32));
    uint8_t boot[32]={id_bootloader_jump};
    assert(!era_tomak_sync_via_admit(boot,32) && preemptions==1);
    assert(!era_tomak_sync_admission_pending());
    reset(); paired=false; assert(!era_tomak_sync_via_admit(q,32));
    reset(); policy=false; assert(!era_tomak_sync_via_admit(q,32));
    assert(!era_tomak_sync_admission_pending() && !replies);
    reset(); ready=true;
    uint8_t system[32]={id_custom_set_value,ERA_WIRE_CHANNELS_EERRAA_SYSTEM,ERA_WIRE_SYSTEM_EERRAA_INPUT_SYNC,1};
    assert(!era_tomak_sync_via_admit(system,32) && preemptions==0 && ready);
    system[2]=0xff;
    assert(!era_tomak_sync_via_admit(system,32) && preemptions==0);
    assert(!era_tomak_sync_via_admit(q,32)); /* Normal ON writer still admitted. */
    system[2]=ERA_WIRE_SYSTEM_BOOTLOADER; system[3]=0;
    assert(!era_tomak_sync_via_admit(system,32) && preemptions==0);
    system[3]=1;
    assert(!era_tomak_sync_via_admit(system,32) && preemptions==1);
    reset(); assert(!era_tomak_sync_via_admit(q,31));
    puts("PASS unchanged requests/replies, one execution, no early ACK, overwrite rejection, session/timeout/failure, reads/reset/standalone");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-sync-via-") as directory:
    c, exe = Path(directory) / "via.c", Path(directory) / "via.exe"
    c.write_text(prefix + enums + "\n" + source + "\n" + cases)
    subprocess.run(["gcc", "-I", str(root), str(c),
                    str(root / "keyboards/era/common/system/era_tomak_sync_admission.c"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
