// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_communication_core_internal.h"
#include "era_split_communication_core_responder_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
/* For the anchor's send-hold correction (R6): the per-chip free-running
   counter both cores read; only differences of it are used. */
#include "hardware/structs/timer.h"
#include "era_split_communication_core_owner.h"
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "era_split_communication_core_storage.h"
#    include "era_split_communication_core_storage_service.h"
#endif
#include "../era_host_peer_transaction.h"
#include "../era_split_transaction_engine.h"
#include "../era_split_wire_payload.h"

#ifndef ERA_SPLIT_RESPONDER_FIRST_BYTE_TIMEOUT_MS
#    define ERA_SPLIT_RESPONDER_FIRST_BYTE_TIMEOUT_MS 60000U
#endif

static bool era_split_communication_core_copy_responder_snapshot(era_split_communication_core_responder_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return false;
    }
    for (uint8_t retry = 0; retry < ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_SNAPSHOT_READ_RETRIES; retry++) {
        uint32_t first_seq = g_era_split_communication_core_responder_snapshot_publish_seq;
        if ((first_seq & 1U) != 0 || !g_era_split_communication_core.responder_snapshot_valid) {
            continue;
        }
        __DMB();
        *snapshot = g_era_split_communication_core_responder_snapshot;
        __DMB();
        uint32_t second_seq = g_era_split_communication_core_responder_snapshot_publish_seq;
        if (first_seq == second_seq && (second_seq & 1U) == 0) {
            return true;
        }
    }
    return false;
}

static bool era_split_communication_core_claim_responder_snapshot(era_split_communication_core_responder_snapshot_t *snapshot) {
    for (uint8_t retry = 0; retry < ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_SNAPSHOT_READ_RETRIES; retry++) {
        if (!era_split_communication_core_copy_responder_snapshot(snapshot)) {
            continue;
        }

        uint32_t publish_seq                                               = g_era_split_communication_core_responder_snapshot_publish_seq;
        g_era_split_communication_core_responder_snapshot_claim_generation = snapshot->snapshot_generation;
        __DMB();
        if (publish_seq == g_era_split_communication_core_responder_snapshot_publish_seq && (publish_seq & 1U) == 0 && g_era_split_communication_core.responder_snapshot_valid && snapshot->snapshot_generation == g_era_split_communication_core_responder_snapshot.snapshot_generation) {
            return true;
        }

        g_era_split_communication_core_responder_snapshot_claim_generation = 0;
        __DMB();
    }
    g_era_split_communication_core.responder_accept_stale_count++;
    return false;
}

static void era_split_communication_core_release_responder_snapshot(void) {
    __DMB();
    g_era_split_communication_core_responder_snapshot_claim_generation = 0;
    __DMB();
    __SEV();
}

static bool era_split_communication_core_responder_snapshot_matches(const era_split_communication_core_responder_snapshot_t *snapshot, uint16_t owner_epoch) {
    return snapshot != NULL && snapshot->owner_gate_ready && snapshot->owner_epoch == owner_epoch && snapshot->relation_generation != 0 && snapshot->snapshot_generation != 0;
}

/* Held out of line deliberately. Dropping the third call site left two, both
   inside era_split_communication_core_responder_service_once(), and at two GCC
   inlines the body into both — which grows that one caller from 1728 to 2128 B
   against the 182 B body it deletes, a net +216 B of the SRAM image on every
   profile for no runtime the two remaining calls did not already have.
   Measured, not assumed, and re-measurable in one build by deleting the
   attribute: it is the two-caller inlining shape this project has been caught
   by before, arriving from the other direction. */
static void __attribute__((noinline)) era_split_communication_core_responder_plan_from_snapshot(const era_split_communication_core_responder_snapshot_t *snapshot, era_host_peer_transaction_responder_response_plan_t *plan) {
    memset(plan, 0, sizeof(*plan));
    plan->lock_state_bits = snapshot->lock_state_bits;
    if ((snapshot->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE) != 0) {
        plan->send_lock_state = true;
    }
    if ((snapshot->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC) != 0) {
        plan->send_visual_snapshot                   = true;
        plan->visual_snapshot.reason                 = snapshot->visual_reason;
        memcpy(plan->visual_snapshot.pressed_baseline, snapshot->visual_baseline, sizeof(plan->visual_snapshot.pressed_baseline));
    }
    if ((snapshot->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE) != 0) {
        plan->send_rgb_state = true;
        plan->rgb_state      = snapshot->rgb_state;
    }
    if ((snapshot->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS) != 0) {
        plan->send_storage_news = true;
        plan->storage_news      = snapshot->storage_news;
    }
    if ((snapshot->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_TIME_ANCHOR) != 0) {
        plan->send_time_anchor     = true;
        plan->time_anchor_ms       = snapshot->time_anchor_ms;
        plan->time_anchor_stamp_us = snapshot->time_anchor_stamp_us;
    }
    if ((snapshot->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER) != 0) {
        plan->send_input_layer = true;
        plan->input_layer      = snapshot->input_layer;
    }
    if ((snapshot->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY) != 0) {
        plan->send_authority = true;
        plan->authority      = snapshot->authority;
    }
    if ((snapshot->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY) != 0) {
        plan->send_activity = true;
        plan->activity      = snapshot->activity;
    }
}

/* Reserve capacity, do not fill it. The reserved slot is written once, by the
   publish below, which stores the record and only then advances the write
   index -- and the index is the whole of what lets core0's drain see a slot at
   all (`..._responder_result_pop` returns false while read == write, and this
   function does not move write). A pre-send copy of the same 140-byte record
   was therefore a second store no reader could ever reach, on core1, twice per
   answered request. What a reserved-but-unpublished slot holds now is whatever
   its previous occupant left; nothing reads that either, and a future change
   that wants to read a reserved slot is the change that has to put the store
   back. */
static bool era_split_communication_core_responder_prepare_result_slot(bool source_push, uint32_t *write, uint32_t *next) {
    volatile uint32_t *read_index  = source_push ? &g_era_split_communication_core_responder_source_read : &g_era_split_communication_core_responder_general_read;
    volatile uint32_t *write_index = source_push ? &g_era_split_communication_core_responder_source_write : &g_era_split_communication_core_responder_general_write;
    uint32_t           slots       = source_push ? ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_SOURCE_RESULT_SLOTS : ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_GENERAL_RESULT_SLOTS;
    __DMB();
    *write = *write_index;
    *next  = era_split_communication_core_responder_ring_next(*write, slots);
    if (*next == *read_index) {
        g_era_split_communication_core.responder_slot_full_count++;
        g_era_split_communication_core.responder_noack_count++;
        return false;
    }
    __DMB();

    g_era_split_communication_core.responder_source_push_slot_reserved = source_push ? 1 : 0;
    g_era_split_communication_core.responder_slot_reserve_count++;
    return true;
}

static void era_split_communication_core_responder_publish_result_slot(bool source_push, uint32_t write, uint32_t next, const era_split_communication_core_responder_result_t *result) {
    era_split_communication_core_responder_result_t *slot = source_push ? &g_era_split_communication_core_responder_source_results[write] : &g_era_split_communication_core_responder_general_results[write];
    *slot                                                 = *result;
    __DMB();
    if (source_push) {
        g_era_split_communication_core_responder_source_write              = next;
        g_era_split_communication_core.responder_source_push_slot_reserved = 0;
        g_era_split_communication_core.responder_source_push_result_ready  = 1;
    } else {
        g_era_split_communication_core_responder_general_write = next;
    }
    g_era_split_communication_core.responder_accept_count++;
    __DMB();
}

/* `plan` is the caller's already-built plan, not a second derivation from the
   same snapshot. Both are `plan_from_snapshot()` of one unmodified snapshot, so
   recomputing produced a byte-identical copy on every answered non-source-push
   request — at the 10 ms HOST-PEER / 1 ms DUAL-HOST cadence, and on core1,
   whose stack depth no compile-time construct reports.

   Threading it is the fix rather than skipping the second call under the
   suppression flag: that would make plan validity depend on a decision taken
   hundreds of lines away, and a plan left unbuilt where the caller still
   commits it retires the sent-state shadows against zeroed values — which is
   the failure class D3 had just closed. One producer, one consumer, no
   condition. */
static bool era_split_communication_core_prepare_responder_response_payload(const era_split_communication_core_responder_snapshot_t *snapshot, const era_host_peer_transaction_responder_response_plan_t *plan, uint8_t control, bool core0_lane_request, uint8_t *payload, uint8_t *payload_len, uint8_t *section_mask) {
    /* One carrier (D3). A response section rides the standing exchange's answer
       and nothing else, so an answer to core0's matrix push is the ordinary
       one-byte ACK below.
     *
     * The initiator has two arrival paths for one section set -- core1's
     * standing decode, which caches what it received and reports only an edge,
     * and the lane result, which reached core0's appliers without touching that
     * cache. Since each value crosses the wire exactly once (the sent-state
     * shadows), a value taken on the second path left the first path's cache
     * holding the previous one, and the next legitimate re-assertion of that
     * previous value was filtered as "not news" and never applied -- while the
     * record kept republishing the stale one over a correctly applied value on
     * every unrelated edge. Device-confirmed 2026-08-10 on the Caps indicator,
     * whose two-value space makes the re-assertion certain: three arms,
     * left-half-only typing (source-push 0.14 % of answered requests) clean at
     * the highest response-section load of the three, right-half-only (20.1 %)
     * and both-halves (12.5 %) both failing.
     *
     * D1 sealed that seam for one section by folding the arrival back into the
     * cache. This removes the second carrier instead, which is the same end
     * state reached by deletion: with no section able to arrive on the push
     * lane, the cache has one writer and one reader and no fold has to be
     * remembered for the next section or the next lane.
     *
     * Nothing is lost by suppressing here, and the sent-state discipline is why:
     * the shadow retires from the wire's own section byte below, so a section
     * this answer does not carry stays due and goes out on the next standing
     * answer -- one poll period, 10 ms in HOST-PEER. Suppressing on the
     * *receiving* side would have been the silent-loss version of the same
     * idea.
     *
     * Provably inert in DUAL-HOST: that relation's push cell carries no matrix
     * (_Static_assert, era_split_wire_protocol.h), so the caller's flag cannot
     * be true there. */
    if (core0_lane_request) {
        *section_mask = 0;
        payload[0]    = control;
        *payload_len  = 1;
        return true;
    }

    *section_mask = snapshot->response_section_mask;
    payload[0]    = control;
    *payload_len  = 1;
    if (*section_mask == 0) {
        return true;
    }

    /* Bodies append in ascending marker-bit order, which since Slice 11
       includes the lock value: it is a one-byte body at marker 0x08 rather
       than three bits inside byte2, so it is emitted before the visual
       section instead of merged into the marker byte. Every append is
       bounds-checked against the frame buffer rather than trusting the plan,
       because the buffer is a core1 stack array and the compile-time budget
       asserts cover the combinations the plan promises, not a plan bug. */
    payload[0]   = (uint8_t)(control | ERA_SPLIT_WIRE_CONTROL_EXT);
    payload[1]   = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP;
    payload[2]   = 0;
    *payload_len = 3;

#define ERA_SPLIT_RESPONDER_SECTION_ROOM(bytes) ((uint16_t)(*payload_len) + (uint16_t)(bytes) <= ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN)

    if (plan->send_input_layer) {
        if (!ERA_SPLIT_RESPONDER_SECTION_ROOM(ERA_SPLIT_WIRE_INPUT_LAYER_BYTES)) {
            return false;
        }
        payload[2] |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER;
        payload[(*payload_len)++] = plan->input_layer;
    }
    if (plan->send_activity) {
        if (!ERA_SPLIT_RESPONDER_SECTION_ROOM(ERA_SPLIT_WIRE_ACTIVITY_BYTES)) {
            return false;
        }
        payload[2] |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY;
        era_split_wire_encode_activity_body(&plan->activity, &payload[*payload_len]);
        *payload_len = (uint8_t)(*payload_len + ERA_SPLIT_WIRE_ACTIVITY_BYTES);
    }
    if (plan->send_authority) {
        if (!ERA_SPLIT_RESPONDER_SECTION_ROOM(ERA_SPLIT_WIRE_AUTHORITY_BYTES)) {
            return false;
        }
        payload[2] |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY;
        era_split_wire_encode_authority_body(&plan->authority, &payload[*payload_len]);
        *payload_len = (uint8_t)(*payload_len + ERA_SPLIT_WIRE_AUTHORITY_BYTES);
    }
    if (plan->send_lock_state) {
        if (!ERA_SPLIT_RESPONDER_SECTION_ROOM(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_BYTES)) {
            return false;
        }
        payload[2] |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE;
        payload[(*payload_len)++] = (uint8_t)(plan->lock_state_bits & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_VALUE_MASK);
    }
    if (plan->send_visual_snapshot) {
        if (!ERA_SPLIT_RESPONDER_SECTION_ROOM(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_FULL_BYTES)) {
            return false;
        }
        payload[2] |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC;
        payload[(*payload_len)++] = plan->visual_snapshot.reason;
        memcpy(&payload[*payload_len], plan->visual_snapshot.pressed_baseline, sizeof(plan->visual_snapshot.pressed_baseline));
        *payload_len = (uint8_t)(*payload_len + sizeof(plan->visual_snapshot.pressed_baseline));
    }
    if (plan->send_rgb_state) {
        if (!ERA_SPLIT_RESPONDER_SECTION_ROOM(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES) ||
            !era_host_peer_transaction_encode_rgb_state_body(&plan->rgb_state, &payload[*payload_len])) {
            return false;
        }
        payload[2] |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE;
        *payload_len = (uint8_t)(*payload_len + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES);
    }
    if (plan->send_storage_news) {
        if (!ERA_SPLIT_RESPONDER_SECTION_ROOM(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_BYTES)) {
            return false;
        }
        payload[2] |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS;
        payload[(*payload_len)++] = plan->storage_news;
    }
    if (plan->send_time_anchor) {
        if (!ERA_SPLIT_RESPONDER_SECTION_ROOM(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_TIME_ANCHOR_BYTES)) {
            return false;
        }
        /* The send-side correction (R6), the mirror of the receive-side one
           R2 built. Core0 stamped `timerawl` beside the reading; the elapsed
           since is the time the anchor sat in the published snapshot waiting
           for this poll -- up to a poll period, measured at a worst 9 ms step
           against the then 20 ms period before the stamp existed -- and adding
           it here makes the wire carry
           the value the anchor would have read now. The subtraction is its own
           instrument: `ahold` records what the gap actually is, which is what
           R6's drift measurement needs separated out. */
        uint32_t hold_us   = timer_hw->timerawl - plan->time_anchor_stamp_us;
        uint32_t anchor_ms = plan->time_anchor_ms + hold_us / 1000U;
        g_era_split_communication_core.responder_anchor_hold_last_us = hold_us;
        if (hold_us > g_era_split_communication_core.responder_anchor_hold_max_us) {
            g_era_split_communication_core.responder_anchor_hold_max_us = hold_us;
        }
        payload[2] |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_TIME_ANCHOR;
        era_split_wire_put32(&payload[*payload_len], anchor_ms);
        *payload_len = (uint8_t)(*payload_len + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_TIME_ANCHOR_BYTES);
    }

#undef ERA_SPLIT_RESPONDER_SECTION_ROOM

    /* The wire's own section byte is the authority on what was sent, not the
       snapshot's advertised mask: a section the plan dropped here must not be
       reported as sent, or the sent-state shadow retires a value that never
       crossed. */
    *section_mask = payload[2];
    return *payload_len <= ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN;
}

static bool era_split_communication_core_responder_send(uint16_t owner_epoch, bool session, bool core0_lane_request, const era_split_communication_core_responder_snapshot_t *snapshot, era_split_communication_core_responder_result_t *result) {
    uint8_t payload[ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN];
    uint8_t payload_len = 0;
    uint8_t control     = 0;
    uint8_t response_tx_seq = 0;
    if (!era_split_transaction_engine_prepare_control(session, &control, &response_tx_seq)) {
        result->result = ERA_SPLIT_TRANSACTION_RESULT_BAD;
        return false;
    }

    bool encoded;
    if (session) {
        encoded = era_split_wire_encode_session_status(control, &snapshot->local_session_status, payload, &payload_len);
    } else {
        encoded = era_split_communication_core_prepare_responder_response_payload(snapshot, &result->response_plan, control, core0_lane_request, payload, &payload_len, &result->response_section_mask);
    }
    if (!encoded) {
        result->result = ERA_SPLIT_TRANSACTION_RESULT_BAD;
        g_era_split_communication_core.responder_response_prepare_fail_count++;
        return false;
    }

    g_era_split_communication_core.responder_response_prepare_count++;
    g_era_split_communication_core.responder_last_response_payload_len = payload_len;
    era_split_transaction_failure_t failure = ERA_SPLIT_TRANSACTION_FAILURE_NONE;
    result->result        = era_split_transaction_engine_send_compact_response_owned(ERA_SPLIT_WIRE_DIRECTION_SECONDARY_TO_PRIMARY, payload, payload_len, response_tx_seq, owner_epoch, &failure);
    result->response_sent = result->result == ERA_SPLIT_TRANSACTION_RESULT_OK ? 1 : 0;
    return result->response_sent != 0;
}

static void era_split_communication_core_responder_note_receive_failure(era_split_transaction_engine_result_t transaction_result, era_split_transaction_failure_t failure, uint16_t owner_epoch) {
    if (transaction_result == ERA_SPLIT_TRANSACTION_RESULT_NONE) {
        return;
    }
    g_era_split_communication_core.responder_noack_count++;
    if (failure == ERA_SPLIT_TRANSACTION_FAILURE_PIO_ERROR || failure == ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME || failure == ERA_SPLIT_TRANSACTION_FAILURE_DECODE) {
        /* Counted before the access check, not inside it: the arrival is a
           fact about the wire whether or not this epoch may still touch the
           backend, and the link lane's dwell window wants every one. The
           reset below is what needs the access. */
        g_era_split_communication_core.responder_undecodable_rx_count++;
        if (era_split_communication_core_owner_backend_access(owner_epoch) == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK) {
            era_split_transaction_engine_reset_link_state();
        }
    }
}

bool era_split_communication_core_responder_service_once(uint16_t owner_epoch) {
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    era_split_wire_frame_t *frame = era_split_communication_core_storage_decoded_semantic_frame_scratch();
    /* The storage build receives with bulk-page capacity so the
     * initiator-sent PUSH_CHUNK_REQ is admissible; the raw bytes land in
     * the static decoded-frame scratch, which is free until the response
     * build reuses it after the semantic decode. */
    uint8_t *rx_wire         = era_split_communication_core_storage_decoded_frame_scratch();
    uint16_t rx_wire_capacity = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_WIRE_FRAME_BYTES;
#else
    era_split_wire_frame_t frame_storage;
    era_split_wire_frame_t *frame = &frame_storage;
    uint8_t  rx_wire_storage[ERA_SPLIT_WIRE_COMPACT_MAX_FRAME_LEN];
    uint8_t *rx_wire         = rx_wire_storage;
    uint16_t rx_wire_capacity = sizeof(rx_wire_storage);
#endif
    era_split_transaction_failure_t       receive_failure = ERA_SPLIT_TRANSACTION_FAILURE_NONE;
    era_split_transaction_engine_result_t receive_result  = era_split_transaction_engine_receive_idle_owned(ERA_SPLIT_WIRE_DIRECTION_PRIMARY_TO_SECONDARY, ERA_SPLIT_RESPONDER_FIRST_BYTE_TIMEOUT_MS, ERA_SPLIT_PEER_RESPONSE_WINDOW_MS, owner_epoch, rx_wire, rx_wire_capacity, frame, &receive_failure);
    if (receive_result != ERA_SPLIT_TRANSACTION_RESULT_OK) {
        era_split_communication_core_responder_note_receive_failure(receive_result, receive_failure, owner_epoch);
        return receive_result != ERA_SPLIT_TRANSACTION_RESULT_NONE;
    }

    // Wire-anchored relation liveness: every frame the transaction engine
    // accepts is peer-liveness evidence for the cold silence watch,
    // regardless of which lane consumes it below.
    g_era_split_communication_core.responder_accepted_rx_count++;

#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    if (frame->kind == ERA_SPLIT_WIRE_PAYLOAD_EEPROM_SYNC) {
        return era_split_communication_core_storage_service_responder_frame(owner_epoch, frame);
    }
#endif

    era_split_communication_core_responder_snapshot_t snapshot;
    if (!era_split_communication_core_claim_responder_snapshot(&snapshot)) {
        era_split_communication_core_release_responder_snapshot();
        g_era_split_communication_core.responder_noack_count++;
        return true;
    }
    if (!era_split_communication_core_responder_snapshot_matches(&snapshot, owner_epoch)) {
        era_split_communication_core_release_responder_snapshot();
        g_era_split_communication_core.responder_accept_stale_count++;
        g_era_split_communication_core.responder_noack_count++;
        return true;
    }

    era_split_communication_core_responder_result_t result;
    memset(&result, 0, sizeof(result));
    result.owner_epoch         = owner_epoch;
    result.relation_generation = snapshot.relation_generation;
    result.snapshot_generation = snapshot.snapshot_generation;
    result.result              = ERA_SPLIT_TRANSACTION_RESULT_FAIL;

    bool source_push = false;
    if (frame->kind == ERA_SPLIT_WIRE_PAYLOAD_SESSION_STATUS) {
        result.kind = ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_SESSION;
        /* The role-bit exclusivity is the decoder's, not this arm's: the
           payload validator it runs first admits the flags byte only when
           `flags & AUTHORITY_MASK` is exactly HOST_OPEN or exactly NO_HOST, so
           the two decoded bools cannot come back equal. Re-testing it here
           read as a third pass over one rule and could only ever be false. */
        if (!snapshot.session_allowed || !era_split_wire_decode_session_status(frame, &result.peer_status) || !result.peer_status.status_response_requested) {
            era_split_communication_core_release_responder_snapshot();
            g_era_split_communication_core.responder_noack_count++;
            return true;
        }
    } else if (frame->kind == ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK) {
        /* The heartbeat gate is `heartbeat_allowed` alone since Slice 11. It
           used to also require host_matrix_admitted, which is a HOST-PEER
           matrix question; the DUAL-HOST Right forwards no matrix and answers
           the initiator's runtime slot on the same one-byte request, so core0
           now resolves which relations admit a heartbeat and publishes the
           answer rather than core1 re-deriving it from a matrix fact. */
        result.kind = ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_HEARTBEAT;
        if (!snapshot.heartbeat_allowed) {
            era_split_communication_core_release_responder_snapshot();
            g_era_split_communication_core.responder_noack_count++;
            return true;
        }
        era_split_communication_core_responder_plan_from_snapshot(&snapshot, &result.response_plan);
    } else if (frame->kind == ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER) {
        /* The accept-clip. The classifier already proved the frame's own
           section walk, so what is left is a relation question: does this
           half's relation admit the sections this frame carries? A section
           outside the eligible mask is refused here whatever the rest of the
           admission would have said, which is the property that lets the
           gates read one table instead of hunting for absent symbols. */
        era_split_wire_section_layout_t push_layout;
        if (!era_split_wire_layout_source_push(frame->payload, frame->payload_len, &push_layout) ||
            (push_layout.sections & (uint8_t)~snapshot.eligible_push_sections) != 0) {
            era_split_communication_core_release_responder_snapshot();
            g_era_split_communication_core.responder_noack_count++;
            return true;
        }

        /* The matrix section, not the op id, is what claims the dedicated
           source-push result slot. Every other section mask on this envelope
           takes the general slot, which is how "source-push capacity stays
           reserved from session/heartbeat traffic" survives a frame shape
           where the op alone no longer identifies a matrix.

           Since D3 the same test answers a second question -- whether this
           request came from the peer's *core0* lane rather than from its core1
           standing exchange -- so it is spelled with the constant that names
           that, and `source_push` is what suppresses the response section set
           below. The two questions have one answer by construction: core1's
           standing service composes no matrix and core0's only push enqueuer
           composes nothing else. */
        source_push = era_split_wire_section_present(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_CORE0_LANE_SECTIONS);
        result.kind = source_push ? ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_SOURCE_PUSH
                                  : ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_RUNTIME_PUSH;

        /* `runtime_push_allowed`, not `runtime_allowed`: the second also gates
           the heartbeat answer and is DUAL-HOST-only, so reading it here
           refused the HOST-PEER HOST its PEER's AUTHORITY push -- no ACK, a
           failed transaction, and a relation reset on every authority edge. */
        bool admitted = source_push ? (snapshot.host_matrix_admitted && snapshot.source_push_slot_available)
                                    : (snapshot.runtime_push_allowed != 0);
        if (!admitted) {
            era_split_communication_core_release_responder_snapshot();
            g_era_split_communication_core.responder_noack_count++;
            return true;
        }

        if (source_push) {
            memcpy(result.packed_matrix,
                   &frame->payload[era_split_wire_section_offset(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MATRIX)],
                   sizeof(result.packed_matrix));
            g_era_split_communication_core.responder_last_matrix_seq = frame->tx_seq;
        }
        if (era_split_wire_section_present(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER)) {
            result.peer_input_layer       = frame->payload[era_split_wire_section_offset(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER)];
            result.peer_input_layer_valid = 1;
        }
        if (era_split_wire_section_present(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING)) {
            result.peer_storage_pending =
                (uint8_t)(frame->payload[era_split_wire_section_offset(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING)] &
                          ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_FLAG_MASK);
            result.peer_storage_pending_valid = 1;
        }
        if (era_split_wire_section_present(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM)) {
            const uint8_t *arm_body       = &frame->payload[era_split_wire_section_offset(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM)];
            result.peer_restart_act       = (uint8_t)((arm_body[0] & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_MASK) >> ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_SHIFT);
            result.peer_restart_param     = (uint8_t)(arm_body[0] & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_PARAM_MASK);
            result.peer_restart_commit_ms = era_split_wire_get32(&arm_body[1]);
            result.peer_restart_valid     = 1;
        }
        if (era_split_wire_section_present(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY)) {
            era_split_wire_decode_authority_body(&frame->payload[era_split_wire_section_offset(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY)],
                                                 &result.peer_authority);
            result.peer_authority_valid = 1;
        }
        if (era_split_wire_section_present(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE)) {
            result.peer_rgb_state_valid =
                era_host_peer_transaction_decode_rgb_state_body(&frame->payload[era_split_wire_section_offset(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE)],
                                                                &result.peer_rgb_state)
                    ? 1
                    : 0;
        }
        if (era_split_wire_section_present(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY)) {
            era_split_wire_decode_activity_body(&frame->payload[era_split_wire_section_offset(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY)],
                                                &result.peer_activity);
            result.peer_activity_valid = 1;
        }
        if (era_split_wire_section_present(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL)) {
            const uint8_t *visual_body = &frame->payload[era_split_wire_section_offset(&push_layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL)];
            result.peer_visual_snapshot.reason                 = visual_body[0];
            memcpy(result.peer_visual_snapshot.pressed_baseline,
                   &visual_body[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_BYTES],
                   ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES);
            result.peer_visual_valid = 1;
        }
        era_split_communication_core_responder_plan_from_snapshot(&snapshot, &result.response_plan);
    } else {
        era_split_communication_core_release_responder_snapshot();
        g_era_split_communication_core.responder_noack_count++;
        return true;
    }

    /* Reserve only for a response that will publish a result (Slice 11.7).
     *
     * The invariant is that a success response handing core0 work may not be
     * sent with nowhere to put the work. A bare ACK to a quiet poll hands core0
     * nothing -- the publish below already skips it -- so reserving a slot and
     * releasing it unused was pure overhead, and it was the second way a
     * responder went mute: a core0 that cannot drain fills three slots in
     * 150 ms, and core1 then refuses frames it is perfectly able to answer.
     * Device-measured as `full` and `noack` moving together, +22 and +23 in one
     * session, on a half whose own core0 was inside a flash write.
     *
     * The question is asked of the *snapshot*, before the send, because that is
     * what decides the section byte. A heartbeat whose snapshot plans no
     * section can produce no section, so the only thing that could still want a
     * result is a failed send -- and a failed bare ACK tells core0 nothing it
     * can act on that its own counters do not already carry. */
    bool result_slot_needed = result.kind != ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_HEARTBEAT ||
                              snapshot.response_section_mask != 0;
    uint32_t write = 0;
    uint32_t next  = 0;
    if (result_slot_needed &&
        !era_split_communication_core_responder_prepare_result_slot(source_push, &write, &next)) {
        era_split_communication_core_release_responder_snapshot();
        return true;
    }

    era_split_transaction_engine_commit_received_frame(frame);
    if (!era_split_communication_core_responder_send(owner_epoch, result.kind == ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_SESSION, source_push, &snapshot, &result)) {
        g_era_split_communication_core.responder_noack_count++;
    }
    era_split_transaction_engine_publish_diagnostics_snapshot();

    /* Publish the result only when core0 has something to do with it.
     *
     * A heartbeat answered OK with an empty section byte is the whole of a
     * quiet DUAL-HOST poll, and it asks core0 for nothing: no peer status to
     * note, no matrix to accept, no peer layer to apply, and no sent-state
     * shadow to advance, because the wire carried no section to retire one.
     * What is left is counters, and counters do not need a wake.
     *
     * Publishing them did. The ring going non-empty is what
     * era_split_transport_scheduler_housekeeping_event_due()
     * (split/era_split_transport_scheduler.c) reads, so every quiet
     * poll cost core0 one full housekeeping pass -- device-measured 2026-08-01
     * at 218.7 per second on a DUAL-HOST Right against a 198.7 Hz poll, at
     * about 61 us each. That is the same per-exchange cost the standing
     * exchange removed on the initiator, arriving through the responder.
     *
     * The reservation above still happens either way, so the reserve-before-
     * response invariant is untouched: what changes is whether the reserved
     * slot is committed, not whether capacity was proven first.
     *
     * And liveness does not ride this. The responder-silence stale watch is
     * wire-anchored to responder_accepted_rx_count, incremented on every
     * accepted frame far above this point, so a skipped publish still refreshes
     * the watch (era_invariants.md: the watch has no exemptions). */
    bool core0_action_required = result_slot_needed &&
                                 (result.kind != ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_HEARTBEAT ||
                                  result.response_section_mask != 0 ||
                                  result.result != ERA_SPLIT_TRANSACTION_RESULT_OK);
    if (core0_action_required) {
        era_split_communication_core_responder_publish_result_slot(source_push, write, next, &result);
    } else {
        g_era_split_communication_core.responder_quiet_count++;
    }
    era_split_communication_core_release_responder_snapshot();
    return true;
}

uint32_t era_split_communication_core_responder_accepted_rx_count(void) {
    return g_era_split_communication_core.responder_accepted_rx_count;
}

uint32_t era_split_communication_core_responder_undecodable_rx_count(void) {
    return g_era_split_communication_core.responder_undecodable_rx_count;
}

uint32_t era_split_communication_core_responder_quiet_count(void) {
    return g_era_split_communication_core.responder_quiet_count;
}
