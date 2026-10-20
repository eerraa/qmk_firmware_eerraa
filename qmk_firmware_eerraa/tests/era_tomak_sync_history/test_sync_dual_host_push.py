#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify HOST-PEER pull -> DUAL-HOST transition -> first & second keymap push."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

here = Path(__file__).resolve()
root = here.parents[1] / "qmk_firmware_eerraa" if (here.parents[1] / "qmk_firmware_eerraa").is_dir() else here.parents[2]
storage_src = (root / "keyboards/era/common/split/era_host_peer_storage.c").read_text(encoding="utf-8")
scheduler_src = (root / "keyboards/era/common/split/era_split_transport_scheduler.c").read_text(encoding="utf-8")
responder_src = (
    root / "keyboards/era/common/split/communication_core/era_split_communication_core_responder_service.c"
).read_text(encoding="utf-8")
eeprom_sync_src = (root / "keyboards/era/common/split/era_split_eeprom_sync.c").read_text(encoding="utf-8")


def extract_function(source, name):
    match = re.search(r"^\s*(?:static\s+)?[A-Za-z_]\w*[\s*]+" + re.escape(name) + r"\([^;{}]*\)\s*\{", source, re.M)
    if not match:
        raise RuntimeError("Missing function: " + name)
    brace = source.index("{", match.start())
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start() : end]


def extract_case(source, label):
    start = source.index(label)
    end = source.index("break;", start) + len("break;")
    return source[start:end]


host_result_fn = extract_function(storage_src, "era_host_peer_storage_process_host_result")
close_rsp_case = extract_case(host_result_fn, "case ERA_SPLIT_EEPROM_SYNC_OP_CLOSE_RSP:")
push_complete_case = extract_case(host_result_fn, "case ERA_SPLIT_EEPROM_SYNC_STATUS_COMPLETE:")
peer_result_fn = extract_function(storage_src, "era_host_peer_storage_process_peer_result_record")
peer_push_complete_case = extract_case(peer_result_fn, "case ERA_SPLIT_EEPROM_SYNC_STATUS_COMPLETE:")

host_close_fn = extract_function(storage_src, "era_host_peer_storage_host_close")
reset_peer_fn = extract_function(storage_src, "era_host_peer_storage_reset_peer_episode")

sync_restored_fn = extract_function(scheduler_src, "era_split_transport_scheduler_sync_channel_restored")
reset_serial_fn = extract_function(scheduler_src, "era_split_transport_scheduler_reset_serial_for_transport_role")
rotate_relation_fn = extract_function(scheduler_src, "era_split_transport_scheduler_rotate_storage_relation")
force_recovery_fn = extract_function(scheduler_src, "era_split_transport_scheduler_force_storage_recovery")
housekeeping_fn = extract_function(scheduler_src, "era_split_transport_scheduler_housekeeping_body")
sync_start = housekeeping_fn.index("    bool sync_paired =")
sync_block = housekeeping_fn[sync_start : housekeeping_fn.index("\n#endif", sync_start)]

sync_response_fn = extract_function(responder_src, "era_split_communication_core_sync_response")
posted_decl = re.search(
    r"static struct \{\s*bool valid;\s*era_tomak_sync_binding_t binding;\s*era_tomak_sync_wire_t request;\s*\} sync_posted;",
    responder_src,
).group()
indicator_fn = extract_function(eeprom_sync_src, "era_split_eeprom_sync_indicator_visible_advance")

endpoint_unit = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#ifndef ERA_TOMAK_SYNC_ENABLE
#define ERA_TOMAK_SYNC_ENABLE
#endif
#define ERA_HOST_PEER_STORAGE_V1_ENABLE
#include "keyboards/era/common/split/era_split_eeprom_sync.h"
#include "keyboards/era/common/split/era_split_scheduler_session.h"
#include "keyboards/era/common/split/era_tomak_sync_runtime.h"
#include "keyboards/era/common/split/era_tomak_sync_receiver.h"
#include "keyboards/era/common/split/era_split_wire_protocol.h"
#include "keyboards/era/common/system/era_tomak_sync_admission.h"
#include "timer.h"

#define ERA_STORAGE_TIMING_ABORT(path) ((void)0)
#define ERA_SPLIT_TRANSACTION_RESULT_OK 1
#define ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_TOMAK_SYNC 5
#define ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE 1U
#define ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS 1U
#define ERA_SPLIT_MODE_HOST_PEER_HOST 1U
#define ERA_SPLIT_MODE_HOST_PEER_PEER 2U
#define ERA_SPLIT_MODE_DUAL_HOST_LEFT 3U
#define ERA_SPLIT_MODE_DUAL_HOST_RIGHT 4U
#define ERA_HOST_PEER_STORAGE_IDLE_BACKOFF_MS 25U
#define ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING 0x01U
#define ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE 0x02U
#define ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY 0x04U
#define ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TERMINAL_ABORT 0x20U
#define ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TRANSFER_ACTIVE 0x40U

typedef enum {{
    ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE = 0,
    ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1 = 1,
}} era_split_communication_core_backend_owner_t;

typedef enum {{
    ERA_SPLIT_TRANSACTION_BACKEND_ROLE_DISABLED = 0,
    ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR = 1,
    ERA_SPLIT_TRANSACTION_BACKEND_ROLE_RESPONDER = 2,
}} era_split_transaction_backend_role_t;

enum {{
    ERA_HOST_PEER_STORAGE_ROLE_NONE = 0,
    ERA_HOST_PEER_STORAGE_ROLE_PEER = 1,
    ERA_HOST_PEER_STORAGE_ROLE_HOST = 2,
}};

enum {{
    ERA_HOST_PEER_STORAGE_RUNTIME_IDLE = 0,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_APPLY,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_COMPLETE,
    ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PINNED,
    ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_DURABLE,
    ERA_HOST_PEER_STORAGE_RUNTIME_HOST_CLOSED,
}};

typedef int era_split_transaction_failure_t;
typedef struct {{ uint32_t now_ms; }} era_host_peer_storage_runtime_context_t;
typedef struct {{
    uint16_t owner_epoch, relation_generation, snapshot_generation;
    bool runtime_push_allowed;
    era_tomak_sync_binding_t sync_binding;
    era_tomak_sync_wire_t sync_proof;
}} era_split_communication_core_responder_snapshot_t;
typedef struct {{
    uint16_t owner_epoch, relation_generation, snapshot_generation;
    uint8_t kind, result;
    era_tomak_sync_binding_t sync_binding;
    era_tomak_sync_wire_t sync_request;
}} era_split_communication_core_responder_result_t;

typedef struct {{
    uint32_t span_rise_ms, span_fall_ms, last_pending_ms, span_count, red_era_count, red_on_ms, red_off_ms, break_ms;
    uint8_t break_count, break_flags, break_state;
    bool red_present, span_open, floor_active;
}} era_split_eeprom_sync_state_t;
static era_split_eeprom_sync_state_t era_split_eeprom_sync_state;

static struct {{
    uint16_t transaction_generation, request_generation, relation_generation, policy_generation;
    uint8_t role, state, domain, flags, last_status;
    uint32_t expected_crc32;
}} g_era_host_peer_storage_runtime;
static struct {{
    uint8_t active_due, idle_due, idle_due_kind, audit_owed;
    uint8_t deferred_summary_domain, probe_pending_mask, push_pending_mask, conflict_pending_mask, peer_changed_mask;
    uint32_t idle_due_deadline_ms;
}} g_era_host_peer_storage_relation = {{ .deferred_summary_domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE }};
static struct {{
    uint32_t complete_count, close_count, abort_count, quiesce_fail_count, restart_count;
}} g_era_host_peer_storage_diagnostics;
static struct {{
    uint16_t core1_initiator_relation_generation;
    uint8_t mode;
    bool communication_core_started;
    bool local_wire_available, local_wire_initiator;
    bool local_status_pending, attach_status_last_tx_valid, next_scheduler_deadline_valid;
    uint32_t dirty_flags, route_due_flags;
}} g_era_split_transport_scheduler = {{
    .core1_initiator_relation_generation = 1,
    .communication_core_started = true,
    .local_wire_available = true,
}};

static uint16_t local_owner_epoch = 1;
static era_split_communication_core_backend_owner_t current_owner = ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE;
static era_split_transaction_backend_role_t current_role = ERA_SPLIT_TRANSACTION_BACKEND_ROLE_DISABLED;
static uint16_t peer_owner_epoch, peer_relation_generation, peer_usb_epoch = 1;
static bool peer_known = true, shared_clock_ready = true;
static bool storage_work_active, peer_pending_mirror;
static bool ring_full;
static era_split_communication_core_responder_result_t posted_event;
static era_tomak_sync_wire_t wire_out, wire_reply;
static bool wire_out_valid;

static void era_split_transport_scheduler_ensure_initialized(void) {{}}
static uint16_t era_split_communication_core_owner_epoch(void) {{ return local_owner_epoch; }}
static bool era_split_communication_core_owner_core1_role_is_live(era_split_transaction_backend_role_t role) {{
    return current_owner == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1 && current_role == role;
}}
static void era_split_communication_core_note_core1_serviced(void) {{}}
static bool era_split_transport_scheduler_cancel_core1_initiator(void) {{ return true; }}
static bool era_split_communication_core_owner_transfer_role(
    era_split_communication_core_backend_owner_t owner, era_split_transaction_backend_role_t role) {{
    local_owner_epoch++;
    current_owner = owner;
    current_role = role;
    return true;
}}
static void era_split_communication_core_storage_capacity_init(void) {{}}
static bool era_split_transport_scheduler_drain_communication_core_responder_results(void) {{ return false; }}
static bool era_split_transport_scheduler_stop_communication_core_for_flash_write(void) {{
    return era_split_communication_core_owner_transfer_role(
        ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE, ERA_SPLIT_TRANSACTION_BACKEND_ROLE_DISABLED);
}}
static void era_split_transport_scheduler_rotate_core1_relation(void) {{
    g_era_split_transport_scheduler.core1_initiator_relation_generation++;
    if (g_era_split_transport_scheduler.core1_initiator_relation_generation == 0) {{
        g_era_split_transport_scheduler.core1_initiator_relation_generation = 1;
    }}
}}
static void era_host_peer_transaction_force_responder_lock_state_response(void) {{}}
static void era_split_transport_scheduler_mark_dirty(uint32_t f) {{ g_era_split_transport_scheduler.dirty_flags |= f; }}
static void era_split_transport_scheduler_mark_route_due(uint32_t f) {{ g_era_split_transport_scheduler.route_due_flags |= f; }}

static uint32_t era_host_peer_storage_probe_pacing_ms(uint8_t domain, bool aborted) {{
    (void)domain; (void)aborted; return ERA_HOST_PEER_STORAGE_IDLE_BACKOFF_MS;
}}
static void era_host_peer_storage_arm_summary_refresh(void) {{}}
static void era_host_peer_storage_indicator_retire_provisional_domain(era_split_eeprom_sync_domain_t d) {{ (void)d; }}
static void era_host_peer_storage_note_changed_shadow(era_split_eeprom_sync_domain_t d, bool changed) {{ (void)d; (void)changed; }}
static void era_host_peer_storage_note_domain_converged(era_split_eeprom_sync_domain_t d, uint32_t crc) {{
    (void)d; (void)crc;
}}
static void era_host_peer_storage_note_reject_status(uint8_t s) {{ (void)s; }}
static void era_host_peer_storage_peer_retry_or_abort(uint32_t now_ms, bool timeout) {{ (void)now_ms; (void)timeout; }}
static bool era_host_peer_storage_advertised_pending(void) {{
    return storage_work_active || era_tomak_sync_admission_pending();
}}
static uint8_t era_host_peer_storage_indicator_diag(void) {{
    return peer_pending_mirror ? 0x02U : 0x00U;
}}
static bool era_host_peer_storage_indicator_pending(void) {{
    return era_host_peer_storage_advertised_pending() || peer_pending_mirror;
}}
bool era_split_scheduler_session_peer_known(void) {{ return peer_known; }}
void era_split_scheduler_session_get_diagnostics_snapshot(era_split_scheduler_session_diagnostics_t *s) {{
    *s = (era_split_scheduler_session_diagnostics_t){{
        .peer_usb_epoch = peer_usb_epoch,
        .peer_host_open_generation = 1,
        .peer_host_close_generation = 1,
    }};
}}
bool era_split_scheduler_session_sync_peer_channel(uint16_t *o, uint16_t *r) {{
    *o = peer_owner_epoch;
    *r = peer_relation_generation;
    return peer_known;
}}
bool era_host_peer_transaction_time_anchor_adopted(void) {{ return shared_clock_ready; }}
static bool era_split_transport_scheduler_submit_sync(const era_tomak_sync_wire_t *msg) {{
    wire_out = *msg;
    wire_out_valid = true;
    return true;
}}

static bool era_split_communication_core_responder_prepare_result_slot(bool source, uint32_t *write, uint32_t *next) {{
    (void)source; *write = 0; *next = 1; return !ring_full;
}}
static void era_split_communication_core_responder_publish_result_slot(
    bool source, uint32_t write, uint32_t next, const era_split_communication_core_responder_result_t *result) {{
    (void)source; (void)write; (void)next;
    ring_full = true;
    posted_event = *result;
}}
static void __SEV(void) {{}}
static void era_split_transaction_engine_commit_received_frame(const era_split_wire_frame_t *f) {{ (void)f; }}
static bool era_split_transaction_engine_prepare_control(bool ext, uint8_t *control, uint8_t *seq) {{
    (void)ext; *control = 1; *seq = 1; return true;
}}
static int era_split_transaction_engine_send_compact_response_owned(
    int direction, uint8_t *payload, unsigned length, unsigned seq, unsigned owner, era_split_transaction_failure_t *failure) {{
    (void)direction; (void)seq; (void)owner; (void)failure;
    assert(era_tomak_sync_wire_decode(payload, length, &wire_reply));
    return ERA_SPLIT_TRANSACTION_RESULT_OK;
}}

{sync_restored_fn}
{force_recovery_fn}
{reset_serial_fn}
{rotate_relation_fn}
{host_close_fn}
{reset_peer_fn}
{posted_decl}
{sync_response_fn}
{indicator_fn}

void ep_configure_role(uint8_t mode, bool initiator) {{
    g_era_split_transport_scheduler.mode = mode;
    g_era_split_transport_scheduler.local_wire_available = true;
    g_era_split_transport_scheduler.local_wire_initiator = initiator;
    assert(era_split_transport_scheduler_reset_serial_for_transport_role(true, initiator, true));
}}
void ep_set_peer_channel(uint16_t owner, uint16_t relation, uint16_t usb) {{
    peer_known = true;
    peer_owner_epoch = owner;
    peer_relation_generation = relation;
    peer_usb_epoch = usb;
    g_era_split_transport_scheduler.local_status_pending = false;
}}
void ep_get_local_channel(uint16_t *owner, uint16_t *relation) {{
    *owner = era_split_communication_core_owner_epoch();
    *relation = g_era_split_transport_scheduler.core1_initiator_relation_generation;
}}
void ep_set_storage_work(bool work, bool peer_pending) {{
    storage_work_active = work;
    peer_pending_mirror = peer_pending;
}}
void ep_housekeeping_sync(bool *has_tx, era_tomak_sync_wire_t *tx) {{
    wire_out_valid = false;
    {sync_block}
    *has_tx = wire_out_valid;
    if (wire_out_valid) *tx = wire_out;
}}
void ep_responder_receive_sync(const era_tomak_sync_wire_t *req, era_tomak_sync_wire_t *rep) {{
    era_split_communication_core_responder_snapshot_t snap = {{
        .owner_epoch = era_split_communication_core_owner_epoch(),
        .relation_generation = g_era_split_transport_scheduler.core1_initiator_relation_generation,
        .snapshot_generation = 1,
        .runtime_push_allowed = true,
    }};
    era_tomak_sync_runtime_snapshot(&snap.sync_binding, &snap.sync_proof);
    era_split_wire_frame_t frame = {{ .payload_len = ERA_TOMAK_SYNC_WIRE_BYTES }};
    assert(era_tomak_sync_wire_encode(req, 1, frame.payload));
    assert(era_split_communication_core_sync_response(snap.owner_epoch, &frame, &snap));
    if (ring_full) {{
        ring_full = false;
        era_tomak_sync_runtime_receive(&posted_event.sync_binding, &posted_event.sync_request);
        era_tomak_sync_runtime_snapshot(&snap.sync_binding, &snap.sync_proof);
        assert(era_split_communication_core_sync_response(snap.owner_epoch, &frame, &snap));
    }}
    *rep = wire_reply;
}}
void ep_host_complete_pull(uint32_t now_ms) {{
    const era_host_peer_storage_runtime_context_t ctx = {{ .now_ms = now_ms }};
    const era_host_peer_storage_runtime_context_t *context = &ctx;
    struct {{ uint8_t operation, status; }} result = {{
        .operation = ERA_SPLIT_EEPROM_SYNC_OP_CLOSE_RSP,
        .status = ERA_SPLIT_EEPROM_SYNC_STATUS_COMPLETE,
    }};
    g_era_host_peer_storage_runtime.state = ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PINNED;
    g_era_host_peer_storage_runtime.domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP;
    g_era_host_peer_storage_runtime.flags = ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TRANSFER_ACTIVE;
    switch (result.operation) {{
        {close_rsp_case}
    }}
}}
void ep_host_complete_push(uint32_t now_ms) {{
    const era_host_peer_storage_runtime_context_t ctx = {{ .now_ms = now_ms }};
    const era_host_peer_storage_runtime_context_t *context = &ctx;
    struct {{ uint8_t status; }} result = {{
        .status = ERA_SPLIT_EEPROM_SYNC_STATUS_COMPLETE,
    }};
    g_era_host_peer_storage_runtime.state = ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_DURABLE;
    g_era_host_peer_storage_runtime.domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP;
    g_era_host_peer_storage_runtime.flags = ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TRANSFER_ACTIVE;
    switch ((era_split_eeprom_sync_status_t)result.status) {{
        {push_complete_case}
    }}
}}
void ep_peer_complete_push(uint32_t now_ms) {{
    const era_host_peer_storage_runtime_context_t ctx = {{ .now_ms = now_ms }};
    const era_host_peer_storage_runtime_context_t *context = &ctx;
    typedef struct {{ uint8_t status; }} peer_res_t;
    const peer_res_t res = {{ .status = ERA_SPLIT_EEPROM_SYNC_STATUS_COMPLETE }};
    const peer_res_t *result = &res;
    g_era_host_peer_storage_runtime.state = ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_COMPLETE;
    g_era_host_peer_storage_runtime.domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP;
    g_era_host_peer_storage_runtime.flags = ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TRANSFER_ACTIVE;
    switch ((era_split_eeprom_sync_status_t)result->status) {{
        {peer_push_complete_case}
    }}
}}
"""

harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ERA_TOMAK_SYNC_ENABLE
#define DYNAMIC_KEYMAP_LAYER_COUNT 4
#include "keyboards/era/common/split/era_tomak_sync_wire.h"
#include "keyboards/era/common/system/era_tomak_sync_via.h"
#include "keyboards/era/common/split/era_split_authority_reducer.h"
#include "keyboards/era/common/split/era_split_sync_policy.h"

#define ERA_SPLIT_MODE_HOST_PEER_HOST 1U
#define ERA_SPLIT_MODE_HOST_PEER_PEER 2U
#define ERA_SPLIT_MODE_DUAL_HOST_LEFT 3U
#define ERA_SPLIT_MODE_DUAL_HOST_RIGHT 4U

#define DEFINE_EP(p) \
    void p##_ep_configure_role(uint8_t mode, bool initiator); \
    void p##_ep_set_peer_channel(uint16_t owner, uint16_t relation, uint16_t usb); \
    void p##_ep_get_local_channel(uint16_t *owner, uint16_t *relation); \
    void p##_ep_set_storage_work(bool work, bool peer_pending); \
    void p##_ep_housekeeping_sync(bool *has_tx, era_tomak_sync_wire_t *tx); \
    void p##_ep_responder_receive_sync(const era_tomak_sync_wire_t *req, era_tomak_sync_wire_t *rep); \
    void p##_ep_host_complete_pull(uint32_t now_ms); \
    void p##_ep_host_complete_push(uint32_t now_ms); \
    void p##_ep_peer_complete_push(uint32_t now_ms); \
    void p##_era_tomak_sync_runtime_reply(const era_tomak_sync_wire_t *reply); \
    int8_t p##_era_tomak_sync_runtime_output_gate(bool matching_frame, bool driver_ready); \
    bool p##_era_tomak_sync_runtime_output_pending(void); \
    void p##_era_tomak_sync_runtime_output(bool succeeded); \
    bool p##_era_tomak_sync_runtime_pair_ready(void); \
    bool p##_era_tomak_sync_runtime_failed(void); \
    bool p##_era_tomak_sync_runtime_transition_pending(void); \
    bool p##_era_split_eeprom_sync_indicator_visible_advance(void); \
    bool p##_era_tomak_sync_via_admit(uint8_t *request, uint8_t length); \
    void p##_era_tomak_sync_via_task(void);

DEFINE_EP(left)
DEFINE_EP(right)

static uint32_t now_ms = 1000;
static uint8_t hid_tx[32];
static unsigned hid_tx_count, set_keycode_calls;
static uint16_t last_keycode;

uint32_t timer_read32(void) { return now_ms; }
uint32_t timer_elapsed32(uint32_t last) { return now_ms - last; }
uint32_t sync_timer_read32(void) { return now_ms; }
bool timer_expired32(uint32_t current, uint32_t future) { return (int32_t)(current - future) >= 0; }
bool era_split_sync_policy_get_requested(era_split_sync_policy_field_t field, bool *requested) {
    (void)field;
    *requested = true;
    return true;
}
void era_split_authority_reducer_get_snapshot(era_authority_snapshot_t *out) {
    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->usb_state = ERA_AUTH_USB_HOST_OPEN;
    out->usb_epoch = 1;
}
unsigned era_usb_session_configure_state(void) { return 5; /* USB_DEVICE_STATE_CONFIGURED */ }
void era_split_transport_scheduler_mark_maintenance_due(uint8_t f) { (void)f; }
void raw_hid_send(uint8_t *data, uint8_t length) {
    assert(length == 32);
    memcpy(hid_tx, data, 32);
    hid_tx_count++;
}
void via_command_execute(uint8_t *data, uint8_t length) {
    assert(length == 32);
    if (data[0] == 5) { /* id_dynamic_keymap_set_keycode */
        set_keycode_calls++;
        last_keycode = ((uint16_t)data[4] << 8) | data[5];
    }
    raw_hid_send(data, length);
}

static void bind_session(void) {
    uint16_t lo, lr, ro, rr;
    bool has_tx = false;
    era_tomak_sync_wire_t dummy = {0};
    left_ep_get_local_channel(&lo, &lr);
    right_ep_get_local_channel(&ro, &rr);
    left_ep_set_peer_channel(ro, rr, 1);
    right_ep_set_peer_channel(lo, lr, 1);
    left_ep_housekeeping_sync(&has_tx, &dummy);
    right_ep_housekeeping_sync(&has_tx, &dummy);
}

static void drive_sync_transition(bool left_is_initiator, bool expected_target) {
    for (unsigned step = 0; step < 8; ++step) {
        bool has_tx = false, dummy_tx = false;
        era_tomak_sync_wire_t req = {0}, rep = {0}, dummy = {0};
        if (left_is_initiator) {
            left_ep_housekeeping_sync(&has_tx, &req);
            right_ep_housekeeping_sync(&dummy_tx, &dummy);
            if (has_tx) {
                right_ep_responder_receive_sync(&req, &rep);
                left_era_tomak_sync_runtime_reply(&rep);
            }
        } else {
            right_ep_housekeeping_sync(&has_tx, &req);
            left_ep_housekeeping_sync(&dummy_tx, &dummy);
            if (has_tx) {
                left_ep_responder_receive_sync(&req, &rep);
                right_era_tomak_sync_runtime_reply(&rep);
            }
        }
        if (req.operation == ERA_TOMAK_SYNC_WIRE_ARM &&
            (rep.operation == ERA_TOMAK_SYNC_WIRE_ARMED || rep.operation == ERA_TOMAK_SYNC_WIRE_APPLIED)) {
            now_ms += 120;
            assert(left_era_tomak_sync_runtime_output_gate(true, true) == 1);
            assert(right_era_tomak_sync_runtime_output_gate(true, true) == 1);
            assert(left_era_tomak_sync_runtime_output_pending());
            assert(right_era_tomak_sync_runtime_output_pending());
            left_era_tomak_sync_runtime_output(true);
            right_era_tomak_sync_runtime_output(true);
            if (left_is_initiator) {
                right_ep_responder_receive_sync(&req, &rep);
                left_era_tomak_sync_runtime_reply(&rep);
            } else {
                left_ep_responder_receive_sync(&req, &rep);
                right_era_tomak_sync_runtime_reply(&rep);
            }
        }
        if (left_is_initiator ? !left_era_tomak_sync_runtime_transition_pending()
                              : !right_era_tomak_sync_runtime_transition_pending()) {
            break;
        }
    }
}

int main(void) {
    /* 1. HOST-PEER: Left is Host (Responder), Right is Peer (Initiator). */
    left_ep_configure_role(ERA_SPLIT_MODE_HOST_PEER_HOST, false);
    right_ep_configure_role(ERA_SPLIT_MODE_HOST_PEER_PEER, true);
    bind_session();

    uint8_t hp_edit[32] = {5, 0, 0, 1, 0, 4};
    assert(left_era_tomak_sync_via_admit(hp_edit, 32));
    right_ep_set_storage_work(false, true);
    drive_sync_transition(false, true);
    assert(left_era_tomak_sync_runtime_pair_ready() && right_era_tomak_sync_runtime_pair_ready());
    assert(left_era_split_eeprom_sync_indicator_visible_advance() && right_era_split_eeprom_sync_indicator_visible_advance());
    left_era_tomak_sync_via_task();
    assert(hid_tx_count == 1 && hid_tx[0] == 5 && set_keycode_calls == 1);

    /* Right pulls DYNAMIC_KEYMAP from Left and closes with CLOSE_RSP COMPLETE. */
    left_ep_host_complete_pull(now_ms);
    left_ep_set_storage_work(false, false);
    right_ep_set_storage_work(false, false);
    drive_sync_transition(false, false);
    assert(!left_era_split_eeprom_sync_indicator_visible_advance() && !right_era_split_eeprom_sync_indicator_visible_advance());

    /* 2. Connect Right USB -> transition to DUAL-HOST (Left = Initiator, Right = Responder). */
    now_ms += 1000;
    right_ep_configure_role(ERA_SPLIT_MODE_DUAL_HOST_RIGHT, false);
    left_ep_configure_role(ERA_SPLIT_MODE_DUAL_HOST_LEFT, true);
    bind_session();

    /* 3. First keymap edit on Left in DUAL-HOST (5 0 1 1 0 30). */
    uint8_t dh_edit1[32] = {5, 0, 1, 1, 0, 30};
    assert(left_era_tomak_sync_via_admit(dh_edit1, 32));
    right_ep_set_storage_work(false, true);
    drive_sync_transition(true, true);
    assert(left_era_tomak_sync_runtime_pair_ready() && right_era_tomak_sync_runtime_pair_ready());
    assert(left_era_split_eeprom_sync_indicator_visible_advance() && right_era_split_eeprom_sync_indicator_visible_advance());
    left_era_tomak_sync_via_task();
    assert(hid_tx_count == 2 && hid_tx[0] == 5 && set_keycode_calls == 2 && last_keycode == 30);

    /* Left pushes DYNAMIC_KEYMAP to Right and completes in HOST_PUSH_DURABLE / PEER_PUSH_COMPLETE. */
    right_ep_host_complete_push(now_ms);
    left_ep_peer_complete_push(now_ms);
    left_ep_set_storage_work(false, false);
    right_ep_set_storage_work(false, false);

    drive_sync_transition(true, false);
    now_ms += 500;
    uint8_t dh_edit2[32] = {5, 0, 7, 3, 0, 39};
#ifdef MUTATE_ROTATE_ON_PUSH_COMPLETE
    /* Negative control: responder relation rotation on PUSH COMPLETE causes
     * OFF REJECT, locks both indicators ON, and rejects the next edit with 255. */
    assert(left_era_tomak_sync_runtime_failed() && !right_era_tomak_sync_runtime_failed());
    assert(left_era_split_eeprom_sync_indicator_visible_advance() && right_era_split_eeprom_sync_indicator_visible_advance());
    assert(left_era_tomak_sync_via_admit(dh_edit2, 32));
    assert(hid_tx_count == 3 && hid_tx[0] == 255 && hid_tx[2] == 7 && hid_tx[3] == 3 && hid_tx[5] == 39 && set_keycode_calls == 2);
#else
    /* Indicator must negotiate OFF cleanly without REJECT or FAILED lockup. */
    assert(!left_era_tomak_sync_runtime_failed() && !right_era_tomak_sync_runtime_failed());
    assert(!left_era_split_eeprom_sync_indicator_visible_advance() && !right_era_split_eeprom_sync_indicator_visible_advance());

    /* 4. Second keymap edit on Left in DUAL-HOST (5 0 7 3 0 39) must succeed, never 255. */
    assert(left_era_tomak_sync_via_admit(dh_edit2, 32));
    assert(hid_tx_count == 2); /* Held in admission, not rejected with 255. */
    right_ep_set_storage_work(false, true);
    drive_sync_transition(true, true);
    assert(left_era_tomak_sync_runtime_pair_ready() && right_era_tomak_sync_runtime_pair_ready());
    left_era_tomak_sync_via_task();
    assert(hid_tx_count == 3 && hid_tx[0] == 5 && set_keycode_calls == 3 && last_keycode == 39);

    right_ep_host_complete_push(now_ms);
    left_ep_peer_complete_push(now_ms);
    left_ep_set_storage_work(false, false);
    right_ep_set_storage_work(false, false);
    drive_sync_transition(true, false);
    assert(!left_era_tomak_sync_runtime_failed() && !right_era_tomak_sync_runtime_failed());
    assert(!left_era_split_eeprom_sync_indicator_visible_advance() && !right_era_split_eeprom_sync_indicator_visible_advance());

    puts("PASS HOST-PEER edit -> DUAL-HOST transition -> first keymap push turns indicator OFF -> second keymap edit succeeds");
#endif
    return 0;
}
"""

symbols = [
    "era_tomak_sync_wire_encode",
    "era_tomak_sync_wire_decode",
    "era_tomak_sync_wire_matches",
    "era_tomak_sync_transition_bind",
    "era_tomak_sync_transition_install",
    "era_tomak_sync_transition_arm",
    "era_tomak_sync_transition_launch",
    "era_tomak_sync_transition_output",
    "era_tomak_sync_transition_cancel",
    "era_tomak_sync_binding_equal",
    "era_tomak_sync_receiver_bind",
    "era_tomak_sync_receiver_accept",
    "era_tomak_sync_receiver_output",
    "era_tomak_sync_coordinator_request",
    "era_tomak_sync_coordinator_reply",
    "era_tomak_sync_coordinator_ready",
    "era_tomak_sync_receiver_ready",
    "era_tomak_sync_runtime_relation",
    "era_tomak_sync_runtime_paired",
    "era_tomak_sync_runtime_failed",
    "era_tomak_sync_runtime_bind",
    "era_tomak_sync_runtime_snapshot",
    "era_tomak_sync_runtime_receive",
    "era_tomak_sync_runtime_launch",
    "era_tomak_sync_runtime_output",
    "era_tomak_sync_runtime_protected",
    "era_tomak_sync_runtime_request",
    "era_tomak_sync_runtime_transition_pending",
    "era_tomak_sync_runtime_reply",
    "era_tomak_sync_runtime_pair_ready",
    "era_tomak_sync_runtime_controls_output",
    "era_tomak_sync_runtime_target",
    "era_tomak_sync_runtime_output_pending",
    "era_tomak_sync_runtime_preempt",
    "era_tomak_sync_runtime_output_gate",
    "era_tomak_sync_admission_hold",
    "era_tomak_sync_admission_pending",
    "era_tomak_sync_admission_cancel",
    "era_tomak_sync_admission_cancel_matching",
    "era_tomak_sync_admission_take",
    "era_tomak_sync_via_admit",
    "era_tomak_sync_via_task",
    "era_split_eeprom_sync_indicator_visible_advance",
    "era_split_scheduler_session_peer_known",
    "era_split_scheduler_session_get_diagnostics_snapshot",
    "era_split_scheduler_session_sync_peer_channel",
    "era_host_peer_transaction_time_anchor_adopted",
    "era_split_transport_scheduler_force_storage_recovery",
    "era_split_transport_scheduler_rotate_storage_relation",
    "ep_configure_role",
    "ep_set_peer_channel",
    "ep_get_local_channel",
    "ep_set_storage_work",
    "ep_housekeeping_sync",
    "ep_responder_receive_sync",
    "ep_host_complete_pull",
    "ep_host_complete_push",
    "ep_peer_complete_push",
]

cc = shutil.which("cc") or "gcc"
with tempfile.TemporaryDirectory(prefix="tomak-dual-host-push-") as directory:
    directory = Path(directory)
    (directory / "matrix.h").write_text("#pragma once\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\n", encoding="utf-8")
    (directory / "timer.h").write_text(
        "#pragma once\n#include <stdint.h>\n#include <stdbool.h>\nuint32_t timer_read32(void);\nuint32_t timer_elapsed32(uint32_t);\nbool timer_expired32(uint32_t, uint32_t);\n",
        encoding="utf-8",
    )
    (directory / "sync_timer.h").write_text(
        "#pragma once\n#include <stdint.h>\nuint32_t sync_timer_read32(void);\n",
        encoding="utf-8",
    )
    (directory / "usb_device_state.h").write_text("#pragma once\n#define USB_DEVICE_STATE_CONFIGURED 5\n", encoding="utf-8")
    (directory / "via.h").write_text(
        "#pragma once\nenum { id_set_keyboard_value = 2, id_dynamic_keymap_set_keycode = 5, id_dynamic_keymap_reset = 6,"
        " id_eeprom_reset = 8, id_bootloader_jump = 11, id_custom_set_value = 7, id_custom_save = 9,"
        " id_dynamic_keymap_macro_set_buffer = 14, id_dynamic_keymap_macro_reset = 15, id_dynamic_keymap_set_buffer = 19,"
        " id_dynamic_keymap_set_encoder = 21, id_unhandled = 255 };\n"
        "enum { id_layout_options = 2 };\n"
        "void via_command_execute(uint8_t *data, uint8_t length);\n",
        encoding="utf-8",
    )
    (directory / "raw_hid.h").write_text("#pragma once\n#include <stdint.h>\nvoid raw_hid_send(uint8_t *data, uint8_t length);\n", encoding="utf-8")

    for mutate in (False, True):
        ep_c = directory / "endpoint.c"
        ep_text = endpoint_unit
        if mutate:
            target = "era_host_peer_storage_host_close(context->now_ms, false, false);"
            assert ep_text.count(target) >= 2
            ep_text = ep_text.replace(
                "switch ((era_split_eeprom_sync_status_t)result.status) {\n        " + push_complete_case,
                "switch ((era_split_eeprom_sync_status_t)result.status) {\n        "
                + push_complete_case.replace(
                    target,
                    target + "\n                        (void)era_split_transport_scheduler_rotate_storage_relation();",
                    1,
                ),
            )
        ep_c.write_text(ep_text, encoding="utf-8")
        modules = [
            ep_c,
            root / "keyboards/era/common/split/era_tomak_sync_wire.c",
            root / "keyboards/era/common/split/era_tomak_sync_transition.c",
            root / "keyboards/era/common/split/era_tomak_sync_receiver.c",
            root / "keyboards/era/common/split/era_tomak_sync_runtime.c",
            root / "keyboards/era/common/system/era_tomak_sync_admission.c",
            root / "keyboards/era/common/system/era_tomak_sync_via.c",
        ]
        objs = []
        for side in ("left", "right"):
            defs = [f"-D{sym}={side}_{sym}" for sym in symbols]
            for idx, mod in enumerate(modules):
                obj = directory / f"{side}_{idx}.o"
                subprocess.run(
                    [cc, "-std=gnu11", "-DERA_TOMAK_SYNC_ENABLE", "-DDYNAMIC_KEYMAP_LAYER_COUNT=4", "-I", str(directory), "-I", str(root), *defs, "-c", str(mod), "-o", str(obj)],
                    check=True,
                )
                objs.append(str(obj))

        main_c = directory / "main.c"
        main_c.write_text(harness, encoding="utf-8")
        exe = directory / ("test_dual_host_push_mut.exe" if mutate else "test_dual_host_push.exe")
        extra = ["-DMUTATE_ROTATE_ON_PUSH_COMPLETE"] if mutate else []
        subprocess.run([cc, "-std=gnu11", *extra, "-I", str(directory), "-I", str(root), str(main_c), *objs, "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
