#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute accepted SESSION dispatch and current-zero standing restatement."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
split = root / "keyboards/era/common/split"
responder = (split / "era_host_peer_responder.c").read_text()
payload = (split / "era_split_wire_payload.c").read_text()
dispatch = (split / "scheduler/era_split_transport_scheduler_responder.c").read_text()


def function(source, name):
    start = re.search(r"^(?:static )?(?:bool|void) " + name + r"\(", source, re.M).start()
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


prefix = r"""
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "keyboards/era/common/split/era_host_peer_transaction.h"
#include "keyboards/era/common/split/era_split_wire_payload.h"
#include "keyboards/era/common/split/communication_core/era_split_communication_core_responder.h"
#include "keyboards/era/common/split/era_split_sync_policy.h"
#define ERA_HOST_PEER_STORAGE_V1_ENABLE
#define ATOMIC_BLOCK_RESTORESTATE for (bool once=true; once; once=false)
static uint32_t now;
static uint32_t timer_read32(void) { return now; }
static uint32_t timer_elapsed32(uint32_t since) { return now-since; }
static uint32_t sync_timer_read32(void) { return now; }
static struct { uint32_t timerawl; } fake_timer;
#define timer_hw (&fake_timer)
typedef uint8_t layer_state_t;
#define ERA_SPLIT_WIRE_SECTION_DIRECTION_PUSH 0
static struct {
    uint8_t mode;
    bool responder_snapshot_publish_due;
    unsigned host_peer_input_layer_apply_count;
    unsigned host_peer_rgb_state_rx_count;
    unsigned host_peer_visual_snapshot_rx_count;
} g_era_split_transport_scheduler;
static unsigned sessions;
static void era_split_scheduler_session_note_peer_status(const era_split_wire_session_status_t *status) {
    assert(status->accepted_host_open && status->status_response_requested);
    ++sessions;
}
/* Other consumers are outside this SESSION path. None may synthesize the
   responder force edge or change the responder's sent shadows. */
uint8_t era_split_wire_eligible_sections(uint8_t mode, uint8_t direction) { assert(0); return 0; }
static bool era_host_peer_matrix_link_accept_source_push_packed(const uint8_t *matrix) { assert(0); return false; }
bool era_split_sync_policy_get_requested(era_split_sync_policy_field_t field, bool *requested) { assert(0); return false; }
static bool era_split_peer_layer_apply(layer_state_t layer) { assert(0); return false; }
static void era_host_peer_storage_note_peer_pending(bool pending) { assert(0); }
static void era_split_restart_agreement_note_peer_arm(uint8_t act, uint8_t param, uint32_t deadline) { assert(0); }
static void era_split_restart_agreement_note_peer_authority(const era_split_wire_authority_section_t *authority) { assert(0); }
static bool era_split_scheduler_session_note_peer_authority(const era_split_wire_authority_section_t *authority) { assert(0); return false; }
void era_split_communication_core_refresh_responder_snapshot(void) {}
bool era_host_peer_transaction_apply_rgb_state(const era_host_peer_rgb_state_t *state, bool sleep) { assert(0); return false; }
void era_host_peer_transaction_apply_activity(const era_split_wire_activity_section_t *activity) { assert(0); }
void era_host_peer_transaction_apply_visual_snapshot(const era_host_peer_visual_snapshot_t *visual) { assert(0); }
static void era_split_transport_scheduler_commit_communication_core_host_response(const era_split_communication_core_responder_result_t *result) { assert(0); }
static void era_split_responder_projection_note_result(bool session, bool push, bool sent, uint8_t sections, uint8_t result) {
    assert(session && !push && sent && result==ERA_SPLIT_TRANSACTION_RESULT_OK);
}
"""

cases = r"""
static era_host_peer_transaction_responder_response_plan_t prepare(uint8_t news) {
    era_host_peer_transaction_responder_response_plan_t plan;
    era_host_peer_transaction_prepare_responder_response(0, news, 0, NULL, NULL,
        ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS, &plan);
    return plan;
}
static void sent(const era_host_peer_transaction_responder_response_plan_t *plan) {
    era_host_peer_transaction_responder_response_t response = {
        .result=ERA_SPLIT_TRANSACTION_RESULT_OK, .storage_news_sent=true,
    };
    era_host_peer_transaction_commit_responder_response(plan, &response);
}
int main(void) {
    uint8_t received = 0;
    era_host_peer_transaction_responder_response_plan_t plan = prepare(0x80);
    assert(plan.send_storage_news && plan.storage_news==0x80);
    sent(&plan);
    received=plan.storage_news;

    /* Aborted fresh staging never advanced low7. The falling response leaves
       the sender successfully but is lost before the initiator receives it. */
    now=10;
    plan=prepare(0);
    assert(plan.send_storage_news && plan.storage_news==0);
    sent(&plan);
    now=10000;
    plan=prepare(0);
    assert(!plan.send_storage_news && received==0x80);

    /* Feed the actual accepted SESSION dispatch, not its desired force helper.
       The next standing answer must restate zero on the same relation. */
    era_split_communication_core_responder_result_t session = {
        .kind=ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_SESSION,
        .result=ERA_SPLIT_TRANSACTION_RESULT_OK, .response_sent=1,
        .owner_epoch=7, .relation_generation=9,
        .peer_status={.accepted_host_open=true, .status_response_requested=true},
    };
    era_split_transport_scheduler_apply_communication_core_responder_result(&session);
    assert(sessions==1);
    plan=prepare(0);
    assert(plan.send_storage_news && plan.storage_news==0);
    sent(&plan);
    received=plan.storage_news;
    assert(received==0);
    plan=prepare(0);
    assert(!plan.send_storage_news);
    puts("PASS accepted SESSION restates lost falling STORAGE_NEWS zero on standing resume");
}
"""

with tempfile.TemporaryDirectory(prefix="tomak-news-revalidation-") as directory:
    directory = Path(directory)
    (directory / "matrix.h").write_text(
        "#pragma once\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\n"
    )
    unit = prefix
    for name in ("era_split_wire_authority_equal", "era_split_wire_activity_equal"):
        unit += "\n" + function(payload, name)
    # Compile the complete production responder body, with only hardware and
    # clock includes replaced by deterministic boundaries above.
    unit += "\n" + responder[responder.index("static bool     g_era_host_peer_transaction_responder_lock_state_sent_valid;"):]
    unit += "\n" + function(dispatch, "era_split_transport_scheduler_apply_communication_core_responder_result")
    c, exe = directory / "news.c", directory / "news.exe"
    c.write_text(unit + cases)
    subprocess.run(["gcc", "-I", str(directory), "-I", str(root), str(c),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
