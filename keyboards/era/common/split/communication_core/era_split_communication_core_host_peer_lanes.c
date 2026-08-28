// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_communication_core_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "hardware/structs/timer.h"

#include "era_split_communication_core_owner.h"
#include "era_split_communication_core_standing.h"
#include "../era_host_peer_transaction.h"
#include "../era_split_matrix_frame.h"
#include "../era_split_transaction_backend.h"
#include "../era_split_transaction_engine.h"
#include "../era_split_wire_payload.h"

static bool era_split_communication_core_initiator_deadline_expired(uint32_t deadline_us) {
    return (int32_t)(timer_hw->timerawl - deadline_us) >= 0;
}

static era_split_transaction_failure_t era_split_communication_core_initiator_access_failure(uint16_t owner_epoch) {
    switch (era_split_communication_core_owner_backend_access(owner_epoch)) {
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK:
            return ERA_SPLIT_TRANSACTION_FAILURE_NONE;
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_EPOCH:
            return ERA_SPLIT_TRANSACTION_FAILURE_EPOCH;
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_CANCEL:
            return ERA_SPLIT_TRANSACTION_FAILURE_CANCEL;
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_RESET:
            return ERA_SPLIT_TRANSACTION_FAILURE_RESET;
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OWNER:
        default:
            return ERA_SPLIT_TRANSACTION_FAILURE_OWNER;
    }
}

static bool era_split_communication_core_initiator_request_valid(const era_split_communication_core_initiator_request_t *request) {
    if (request == NULL || request->owner_epoch == 0 || request->relation_generation == 0 || request->request_generation == 0 ||
        request->lane <= ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_INVALID || request->lane > ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH ||
        request->request_direction > ERA_SPLIT_WIRE_DIRECTION_SECONDARY_TO_PRIMARY || request->expected_kind == ERA_SPLIT_WIRE_PAYLOAD_INVALID ||
        request->response_window_ms == 0) {
        return false;
    }
    if (request->lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH) {
        uint8_t sections = request->semantic.source_push.sections;
        /* Exact equality since D3, where it used to be "a non-empty subset of
           the push id space". This lane is core0's, and what distinguishes a
           core0-queued request from core1's own standing exchange on the
           responder is precisely its section content -- the responder answers
           the former with a bare ACK so the section set has one carrier
           (era_split_communication_core_responder_service.c). A subset test
           lets a future enqueuer add a section here without noticing that it
           has re-opened the second carrier; the equality test makes that a
           build-visible decision. */
        if (sections != ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_CORE0_LANE_SECTIONS) {
            return false;
        }
        /* The frame budget, checked before the encoder writes rather than
           after, and it stays although the equality test above already admits
           one value. The encoder fills a 15-byte core1 stack array and compares
           the length only after writing; the arithmetic that made that safe --
           MATRIX plus INPUT_LAYER is 13 on the widest board -- is not general,
           because MATRIX plus AUTHORITY is 17 on tomak79h and 19 on TOMAK.
           The equality test compares against the constant, so it cannot catch
           the constant changing; this can, and since the sum moved into
           era_split_wire_source_push_projected_len() it catches a widening to
           any section rather than to the three this arm had been written for. */
        if (era_split_wire_source_push_projected_len(sections) > ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN) {
            return false;
        }
        if ((sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MATRIX) != 0 &&
            !era_split_wire_matrix_reserved_bits_clear(request->semantic.source_push.packed_rows)) {
            return false;
        }
    }
    return true;
}

/* The encoder serializes exactly what core0's lane may carry, and the validator
   above admits exactly one value for that, so there is one body arm. The assert
   is what makes widening the constant a build decision rather than a runtime
   malformed frame: without an arm for a new section the encoder would set its
   marker bit and emit no body, and the receiver's layout walk would refuse the
   frame. The INPUT_LAYER and AUTHORITY arms that stood here were the retired
   planners' (R2), reachable through neither validator call site since D3. */
_Static_assert(ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_CORE0_LANE_SECTIONS ==
                   ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MATRIX,
               "Widening core0's lane needs its encoder arm back (era_split_communication_core_host_peer_lanes.c history).");

static bool era_split_communication_core_initiator_encode(const era_split_communication_core_initiator_request_t *request, uint8_t *payload, uint8_t *payload_len, uint8_t *tx_seq) {
    if (request == NULL || payload == NULL || payload_len == NULL || tx_seq == NULL) {
        return false;
    }

    /* Every live lane is an extended frame. The one compact-control lane was
       HEARTBEAT, which retired with core0's heartbeat enqueuer. */
    bool    ext     = true;
    uint8_t control = 0;
    if (!era_split_transaction_engine_prepare_control(ext, &control, tx_seq)) {
        return false;
    }

    switch ((era_split_communication_core_initiator_lane_t)request->lane) {
        case ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS:
            return era_split_wire_encode_session_status(control, &request->semantic.session_status, payload, payload_len);
        case ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH: {
            /* Bodies append in ascending marker-bit order, exactly as the
               response direction does. Core1 serializes the published mask and
               the copied bodies and decides nothing about either. */
            uint8_t sections = request->semantic.source_push.sections;
            payload[0]       = (uint8_t)(control | ERA_SPLIT_WIRE_CONTROL_EXT);
            payload[1]       = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH;
            payload[2]       = sections;
            uint8_t len      = 3;
            if ((sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MATRIX) != 0) {
                memcpy(&payload[len], request->semantic.source_push.packed_rows, ERA_SPLIT_WIRE_HALF_MATRIX_BYTES);
                len = (uint8_t)(len + ERA_SPLIT_WIRE_HALF_MATRIX_BYTES);
            }
            *payload_len = len;
            return len <= ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN;
        }
        default:
            return false;
    }
}

static void era_split_communication_core_initiator_note_failure(era_split_transaction_failure_t failure) {
    switch (failure) {
        case ERA_SPLIT_TRANSACTION_FAILURE_QUEUE_EXPIRED:
            g_era_split_communication_core.initiator_queue_expired_count++;
            break;
        case ERA_SPLIT_TRANSACTION_FAILURE_OWNER:
            g_era_split_communication_core.initiator_owner_count++;
            break;
        case ERA_SPLIT_TRANSACTION_FAILURE_EPOCH:
            g_era_split_communication_core.initiator_epoch_count++;
            break;
        case ERA_SPLIT_TRANSACTION_FAILURE_CANCEL:
            g_era_split_communication_core.initiator_cancel_count++;
            break;
        case ERA_SPLIT_TRANSACTION_FAILURE_RESET:
            g_era_split_communication_core.initiator_reset_count++;
            break;
        case ERA_SPLIT_TRANSACTION_FAILURE_PIO_ERROR:
            g_era_split_communication_core.initiator_pio_error_count++;
            break;
        case ERA_SPLIT_TRANSACTION_FAILURE_SEND_TIMEOUT:
            g_era_split_communication_core.initiator_send_timeout_count++;
            break;
        case ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT:
            g_era_split_communication_core.initiator_response_timeout_count++;
            break;
        case ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME:
            g_era_split_communication_core.initiator_partial_frame_count++;
            break;
        case ERA_SPLIT_TRANSACTION_FAILURE_IO:
            g_era_split_communication_core.initiator_io_count++;
            break;
        case ERA_SPLIT_TRANSACTION_FAILURE_DECODE:
            g_era_split_communication_core.initiator_decode_count++;
            break;
        case ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_CONTRACT:
            g_era_split_communication_core.initiator_response_contract_count++;
            break;
        default:
            break;
    }
}

/* The one gate every per-lane write goes through. An id outside the lane set
   is the only way to reach a record that is not a lane's, so it is refused
   here and nowhere else -- which is what lets every call site below be a plain
   subscript. */
static volatile era_split_communication_core_lane_state_t *era_split_communication_core_lane(uint8_t lane) {
    if (lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_INVALID ||
        lane >= ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_COUNT) {
        return NULL;
    }
    return &g_era_split_communication_core.lane[lane];
}

static void era_split_communication_core_initiator_note_lane_result(uint8_t lane, era_split_transaction_engine_result_t result) {
    volatile era_split_communication_core_lane_state_t *state = era_split_communication_core_lane(lane);
    if (state == NULL) {
        return;
    }

    switch (result) {
        case ERA_SPLIT_TRANSACTION_RESULT_OK:
            state->ok_count++;
            break;
        case ERA_SPLIT_TRANSACTION_RESULT_MISS:
            state->miss_count++;
            break;
        case ERA_SPLIT_TRANSACTION_RESULT_BAD:
            state->bad_count++;
            break;
        case ERA_SPLIT_TRANSACTION_RESULT_FAIL:
            state->fail_count++;
            break;
        default:
            break;
    }
}

static void era_split_communication_core_initiator_note_transaction(uint8_t lane) {
    volatile era_split_communication_core_lane_state_t *state = era_split_communication_core_lane(lane);
    if (state == NULL) {
        return;
    }
    state->transaction_count++;
    if (lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH) {
        g_era_split_communication_core.source_push_rx_wait_mode = (uint8_t)ERA_SPLIT_COMMUNICATION_CORE_RX_WAIT_MODE_RESPONSE_WINDOW_POLL;
    }
}

static bool era_split_communication_core_initiator_decode(const era_split_communication_core_initiator_request_t *request, const era_split_wire_frame_t *response, era_split_communication_core_initiator_result_t *result) {
    if (request->lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS) {
        result->decoded.session.valid = era_split_wire_decode_session_status(response, &result->decoded.session.status) ? 1 : 0;
        return result->decoded.session.valid != 0;
    }

    /* One walk of the response section mask fills every section. A response
       that carries no section at all is the ordinary one-byte ACK, so a false
       return here is "nothing to extract", not a decode failure. */
    (void)era_host_peer_transaction_extract_sections(response, &result->decoded.host_peer);
    return true;
}

static void era_split_communication_core_initiator_note_response(const era_split_communication_core_initiator_result_t *result) {
    if (result->lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH) {
        g_era_split_communication_core.lane[result->lane].last_response_kind  = result->response_kind;
        g_era_split_communication_core.source_push_last_response_payload_len  = result->response_payload_len;
        g_era_split_communication_core.source_push_last_response_section_byte = result->response_section_byte;
        g_era_split_communication_core.source_push_last_lock_state_valid      = result->decoded.host_peer.host_source_lock_state_valid;
        g_era_split_communication_core.source_push_last_lock_state            = result->decoded.host_peer.host_source_lock_state;
        g_era_split_communication_core.source_push_last_visual_snapshot_valid = result->decoded.host_peer.host_source_visual_snapshot_valid;
        g_era_split_communication_core.source_push_last_rgb_state_valid       = result->decoded.host_peer.host_source_rgb_state_valid;
        g_era_split_communication_core.source_push_last_storage_news_valid    = result->decoded.host_peer.host_source_storage_news_valid;
        g_era_split_communication_core.source_push_last_storage_news          = result->decoded.host_peer.host_source_storage_news;
        if (result->response_kind == ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER_HOST_SOURCE_RSP) {
            g_era_split_communication_core.source_push_hsrsp_count++;
            g_era_split_communication_core.source_push_response_section_or |= result->response_section_byte;
        }
        if (result->decoded.host_peer.host_source_visual_snapshot_valid) {
            g_era_split_communication_core.source_push_visual_snapshot_count++;
        }
        if (result->decoded.host_peer.host_source_rgb_state_valid) {
            g_era_split_communication_core.source_push_rgb_state_count++;
        }
        if (result->decoded.host_peer.host_source_storage_news_valid) {
            g_era_split_communication_core.source_push_storage_news_count++;
        }
    }
}

static void era_split_communication_core_initiator_result_full(uint8_t lane) {
    g_era_split_communication_core.initiator_pending = 0;
    volatile era_split_communication_core_lane_state_t *state = era_split_communication_core_lane(lane);
    if (state == NULL) {
        return;
    }
    state->pending = 0;
    state->result_full_count++;
}

void era_split_communication_core_process_initiator(const era_split_communication_core_queue_record_t *record) {
    if (record == NULL || record->kind != ERA_SPLIT_COMMUNICATION_CORE_QUEUE_RECORD_INITIATOR_REQUEST) {
        return;
    }

    const era_split_communication_core_initiator_request_t *request = &record->data.initiator_request;
    era_split_communication_core_initiator_result_t result;
    memset(&result, 0, sizeof(result));
    result.owner_epoch          = request->owner_epoch;
    result.relation_generation  = request->relation_generation;
    result.request_generation   = request->request_generation;
    result.lane                 = request->lane;
    result.route_kind           = request->route_kind;
    result.route_reason         = request->route_reason;
    result.matrix_seq           = request->lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH ? request->semantic.source_push.matrix_seq : 0;
    result.request_sections     = request->lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH ? request->semantic.source_push.sections : 0;
    result.result               = ERA_SPLIT_TRANSACTION_RESULT_FAIL;

    if (!era_split_communication_core_initiator_request_valid(request)) {
        result.result  = ERA_SPLIT_TRANSACTION_RESULT_BAD;
        result.failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
        goto publish;
    }
    if (era_split_communication_core_initiator_deadline_expired(request->not_after_us)) {
        result.failure = ERA_SPLIT_TRANSACTION_FAILURE_QUEUE_EXPIRED;
        goto publish;
    }

    result.failure = era_split_communication_core_initiator_access_failure(request->owner_epoch);
    if (result.failure != ERA_SPLIT_TRANSACTION_FAILURE_NONE) {
        goto publish;
    }

    uint8_t payload[ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN];
    uint8_t payload_len = 0;
    if (!era_split_communication_core_initiator_encode(request, payload, &payload_len, &result.tx_seq)) {
        result.result  = ERA_SPLIT_TRANSACTION_RESULT_BAD;
        result.failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
        goto publish;
    }

    era_split_wire_frame_t response;
    memset(&response, 0, sizeof(response));
    bool                            request_sent = false;
    era_split_transaction_failure_t failure      = ERA_SPLIT_TRANSACTION_FAILURE_NONE;
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transaction_engine_timing_begin_route(result.route_kind, result.route_reason);
#endif
    era_split_communication_core_initiator_note_transaction(request->lane);
    result.result = (uint8_t)era_split_transaction_engine_transact_compact_owned((era_split_wire_direction_t)request->request_direction,
                                                                                 payload,
                                                                                 payload_len,
                                                                                 result.tx_seq,
                                                                                 (era_split_wire_payload_kind_t)request->expected_kind,
                                                                                 (era_split_wire_payload_kind_t)request->alternate_expected_kind,
                                                                                 request->response_window_ms,
                                                                                 request->owner_epoch,
                                                                                 &response,
                                                                                 &request_sent,
                                                                                 &failure);
    result.request_sent = request_sent ? 1 : 0;
    result.failure      = (uint8_t)failure;
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transaction_engine_timing_end_route();
#endif
    if (result.result == ERA_SPLIT_TRANSACTION_RESULT_OK) {
        result.response_kind         = (uint8_t)response.kind;
        result.response_payload_len  = (uint8_t)response.payload_len;
        result.response_section_byte = response.payload_len > 2 ? response.payload[2] : 0;
        if (!era_split_communication_core_initiator_decode(request, &response, &result)) {
            result.result  = ERA_SPLIT_TRANSACTION_RESULT_BAD;
            result.failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
        } else {
            era_split_transaction_engine_commit_received_frame(&response);
            /* D1's cross-lane fold retired here with the second carrier (D3):
               a response on this lane carries no section for it to fold. The
               decode above stays, and so do its arrival counters -- they are
               what says the suppression is in force, because a non-zero
               `hsrsp`/`secor` on this lane after D3 means a section crossed
               where none may. */
            era_split_communication_core_initiator_note_response(&result);
        }
    }

publish:
    era_split_communication_core_initiator_note_failure((era_split_transaction_failure_t)result.failure);
    era_split_communication_core_initiator_note_lane_result(result.lane, (era_split_transaction_engine_result_t)result.result);

    era_split_communication_core_queue_record_t result_record = {
        .kind                   = ERA_SPLIT_COMMUNICATION_CORE_QUEUE_RECORD_INITIATOR_RESULT,
        .data.initiator_result = result,
    };
    era_split_transaction_engine_publish_diagnostics_snapshot();
    if (!era_split_communication_core_result_push(&result_record)) {
        era_split_communication_core_initiator_result_full(result.lane);
        return;
    }

    g_era_split_communication_core.initiator_result_ready = 1;
    volatile era_split_communication_core_lane_state_t *published = era_split_communication_core_lane(result.lane);
    if (published != NULL) {
        published->result_ready = 1;
    }
    __DMB();
    __SEV();
}

static void era_split_communication_core_initiator_note_submit(uint8_t lane) {
    volatile era_split_communication_core_lane_state_t *state = era_split_communication_core_lane(lane);
    if (state != NULL) {
        state->submit_count++;
    }
}

static void era_split_communication_core_initiator_note_full(uint8_t lane) {
    volatile era_split_communication_core_lane_state_t *state = era_split_communication_core_lane(lane);
    if (state != NULL) {
        state->full_count++;
    }
}

static void era_split_communication_core_initiator_note_start_fail(uint8_t lane) {
    volatile era_split_communication_core_lane_state_t *state = era_split_communication_core_lane(lane);
    if (state != NULL) {
        state->start_fail_count++;
    }
}

static void era_split_communication_core_initiator_note_accept(const era_split_communication_core_initiator_request_t *request) {
    g_era_split_communication_core.initiator_pending = 1;
    volatile era_split_communication_core_lane_state_t *state = era_split_communication_core_lane(request->lane);
    if (state == NULL) {
        return;
    }
    state->pending          = 1;
    state->generation       = request->request_generation;
    state->accept_count++;
    state->last_route_kind   = request->route_kind;
    state->last_route_reason = request->route_reason;
    if (request->lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH) {
        g_era_split_communication_core.source_push_last_matrix_seq = request->semantic.source_push.matrix_seq;
    }
}

bool era_split_communication_core_enqueue_initiator(const era_split_communication_core_initiator_request_t *request, uint32_t queue_window_us) {
    if (queue_window_us == 0 || !era_split_communication_core_initiator_request_valid(request)) {
        return false;
    }

    era_split_communication_core_init();
    era_split_communication_core_initiator_note_submit(request->lane);
    if (g_era_split_communication_core.initiator_pending || g_era_split_communication_core.initiator_result_ready ||
        era_split_communication_core_request_level() != 0 || era_split_communication_core_result_level() != 0) {
        era_split_communication_core_initiator_note_full(request->lane);
        return false;
    }
    if (!era_split_communication_core_owner_ensure_core1()) {
        era_split_communication_core_initiator_note_start_fail(request->lane);
        return false;
    }

    era_split_communication_core_queue_record_t record = {
        .kind                   = ERA_SPLIT_COMMUNICATION_CORE_QUEUE_RECORD_INITIATOR_REQUEST,
        .data.initiator_request = *request,
    };
    /* Stamp after owner readiness and immediately before the shared ring
     * publication. Semantic capture and a first Core1 launch therefore consume
     * none of the queue-freshness budget. Timer wrap is valid: expiry uses the
     * signed delta, so zero is an ordinary absolute deadline. */
    record.data.initiator_request.not_after_us = timer_hw->timerawl + queue_window_us;
    if (!era_split_communication_core_request_push(&record, &g_era_split_communication_core.initiator_pending)) {
        era_split_communication_core_initiator_note_full(request->lane);
        return false;
    }

    era_split_communication_core_initiator_note_accept(&record.data.initiator_request);
    era_split_communication_core_wake();
    return true;
}

static void era_split_communication_core_initiator_clear_lane_flags(uint8_t lane) {
    volatile era_split_communication_core_lane_state_t *state = era_split_communication_core_lane(lane);
    if (state != NULL) {
        state->pending      = 0;
        state->result_ready = 0;
    }
}

bool era_split_communication_core_poll_initiator_result(era_split_communication_core_initiator_result_t *result) {
    if (result == NULL || !g_era_split_communication_core.initiator_result_ready) {
        return false;
    }

    era_split_communication_core_queue_record_t record;
    if (!era_split_communication_core_result_pop(&record)) {
        return false;
    }
    __DMB();
    g_era_split_communication_core.initiator_result_ready = 0;
    g_era_split_communication_core.initiator_pending      = 0;
    if (record.kind != ERA_SPLIT_COMMUNICATION_CORE_QUEUE_RECORD_INITIATOR_RESULT) {
        return false;
    }

    *result = record.data.initiator_result;
    era_split_communication_core_initiator_clear_lane_flags(result->lane);
    volatile era_split_communication_core_lane_state_t *drained = era_split_communication_core_lane(result->lane);
    if (drained != NULL) {
        drained->last_result_generation = result->request_generation;
        drained->result_count++;
        drained->last_result       = result->result;
        drained->last_request_sent = result->request_sent;
        /* SESSION_STATUS is the only lane whose response is decoded into a
           semantic value, so it is the only one with a failure class and a
           decoded-validity flag worth keeping per attempt. */
        if (result->lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS) {
            drained->last_response_kind = result->response_kind;
            g_era_split_communication_core.session_last_failure           = result->failure;
            g_era_split_communication_core.session_last_peer_status_valid = result->decoded.session.valid;
        }
    }
    __DMB();
    return true;
}

bool era_split_communication_core_initiator_pending(void) {
    return g_era_split_communication_core.initiator_pending != 0;
}

bool era_split_communication_core_initiator_result_ready(void) {
    return g_era_split_communication_core.initiator_result_ready != 0;
}

void era_split_communication_core_note_initiator_result_stale(uint8_t lane) {
    volatile era_split_communication_core_lane_state_t *state = era_split_communication_core_lane(lane);
    if (state != NULL) {
        state->result_stale_count++;
    }
}

/* The three per-lane apply counters retired with the lane apply they counted
   (D3). Their arrival twins -- `visn`, `rgbn`, `mskn`, `hsrsp` and `secor` --
   deliberately stay: a counter that can no longer move is dead weight, but a
   counter that must now read zero is an instrument, and those five are what
   say the response-section suppression is actually in force on this lane. */

void era_split_communication_core_initiator_queue_flushed(void) {
    g_era_split_communication_core.initiator_pending      = 0;
    g_era_split_communication_core.initiator_result_ready = 0;
    for (uint8_t lane = 0; lane < ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_COUNT; lane++) {
        g_era_split_communication_core.lane[lane].pending      = 0;
        g_era_split_communication_core.lane[lane].result_ready = 0;
    }
}
