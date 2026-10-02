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
        const era_split_input_request_t *input = &request->semantic.source_push.input;
        if (!input->generation || !input->epoch) return false;
        if (input->op == ERA_INPUT_FENCE || input->op == ERA_INPUT_BIND) return input->length == 7;
        uint8_t payload[15], length;
        if (!era_split_input_encode(1, input, payload, &length)) return false;
    }
    return true;
}

/* Input/control bodies are immutable Core0 publications. Core1 owns control
 * sequence construction; input retries retain logical identity, not TX seq. */
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
            const era_split_input_request_t *input = &request->semantic.source_push.input;
            if (input->op == ERA_INPUT_FENCE || input->op == ERA_INPUT_BIND) {
                payload[0] = control | ERA_SPLIT_WIRE_CONTROL_EXT; payload[1] = input->op;
                memcpy(payload + 2, input->body, 7); *payload_len = 9;
                if (input->op == ERA_INPUT_BIND) { era_split_wire_put16(payload + 9, input->epoch); *payload_len = 11; }
                return true;
            }
            return era_split_input_encode(control, input, payload, payload_len);
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

    const era_split_input_request_t *input = &request->semantic.source_push.input;
    if (input->op == ERA_INPUT_FENCE || input->op == ERA_INPUT_BIND) {
        bool bind = input->op == ERA_INPUT_BIND;
        if (response->kind != (bind ? ERA_SPLIT_WIRE_PAYLOAD_INPUT_BIND_RSP : ERA_SPLIT_WIRE_PAYLOAD_INPUT_FENCE_RSP) ||
            response->payload_len != (bind ? 11 : 9) ||
            !(response->payload[2] & ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_HOST_OPEN)) return false;
        return input->op != ERA_INPUT_BIND || era_split_wire_get16(response->payload + 9) == input->epoch;
    }
    if (response->kind != ERA_SPLIT_WIRE_PAYLOAD_INPUT_RSP ||
        era_split_wire_get16(response->payload + 2) != input->epoch ||
        era_split_wire_get16(response->payload + 4) != input->record) return false;
    result->input_status = response->payload[1];
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
    if (request->lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH) result.input = request->semantic.source_push.input;
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

    if (request->lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH) {
        const era_split_input_request_t *input = &request->semantic.source_push.input;
        /* A due HP standing slot wins once at a frame boundary. A/B remains
         * contiguous, and SESSION/storage retain their existing priority. */
        if (input->op != ERA_INPUT_FENCE && input->op != ERA_INPUT_BIND)
            (void)era_split_communication_core_standing_service_once(request->owner_epoch);

    }

    uint8_t payload[ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN];
    uint8_t payload_len = 0;
    if (!era_split_communication_core_initiator_encode(request, payload, &payload_len, &result.tx_seq)) {
        result.result  = ERA_SPLIT_TRANSACTION_RESULT_BAD;
        result.failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
        goto publish;
    }

    if (request->lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH) {
        const era_split_input_request_t *input = &request->semantic.source_push.input;
        if (!era_split_input_source_tx_allowed(input, request->owner_epoch, request->relation_generation, timer_hw->timerawl) ||
            era_split_communication_core_standing_stop_generation(request->owner_epoch, request->relation_generation)) {
            result.failure = ERA_SPLIT_TRANSACTION_FAILURE_QUEUE_EXPIRED;
            goto publish;
        }
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
        result.response_section_byte = response.kind == ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER_HOST_SOURCE_RSP && response.payload_len > 2 ? response.payload[2] : 0;
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
