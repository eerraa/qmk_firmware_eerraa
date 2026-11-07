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
        request->lane <= ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_INVALID || request->lane >= ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_COUNT ||
        request->request_direction > ERA_SPLIT_WIRE_DIRECTION_SECONDARY_TO_PRIMARY || request->expected_kind == ERA_SPLIT_WIRE_PAYLOAD_INVALID ||
        request->response_window_ms == 0) {
        return false;
    }
#ifdef ERA_TOMAK_SYNC_ENABLE
    if (request->lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_TOMAK_SYNC) {
        const era_tomak_sync_wire_t *sync = &request->semantic.sync;
        uint8_t payload[ERA_TOMAK_SYNC_WIRE_BYTES];
        return (sync->operation == ERA_TOMAK_SYNC_WIRE_OFFER || sync->operation == ERA_TOMAK_SYNC_WIRE_ARM ||
                sync->operation == ERA_TOMAK_SYNC_WIRE_APPLIED) &&
               sync->owner_epoch == request->owner_epoch && sync->relation_generation == request->relation_generation &&
               request->expected_kind == ERA_SPLIT_WIRE_PAYLOAD_TOMAK_SYNC &&
               request->alternate_expected_kind == ERA_SPLIT_WIRE_PAYLOAD_INVALID &&
               era_tomak_sync_wire_encode(sync, 1, payload);
    }
#endif
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
#ifdef ERA_TOMAK_SYNC_ENABLE
        case ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_TOMAK_SYNC:
            *payload_len = ERA_TOMAK_SYNC_WIRE_BYTES;
            return era_tomak_sync_wire_encode(&request->semantic.sync, control, payload);
#endif
        case ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS:
#ifdef ERA_TOMAK_SYNC_ENABLE
        {
            era_split_wire_session_status_t status = request->semantic.session_status;
            status.sync_owner_epoch = request->owner_epoch;
            status.sync_relation_generation = request->relation_generation;
            return era_split_wire_encode_session_status(control, &status, payload, payload_len);
        }
#else
            return era_split_wire_encode_session_status(control, &request->semantic.session_status, payload, payload_len);
#endif
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
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    if (failure == ERA_SPLIT_TRANSACTION_FAILURE_QUEUE_EXPIRED) {
        g_era_split_communication_core.initiator_queue_expired_count++;
    }
#else
    (void)failure;
#endif
}

static bool era_split_communication_core_initiator_decode(const era_split_communication_core_initiator_request_t *request, const era_split_wire_frame_t *response, era_split_communication_core_initiator_result_t *result) {
#ifdef ERA_TOMAK_SYNC_ENABLE
    if (request->lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_TOMAK_SYNC) {
        if (response->kind != ERA_SPLIT_WIRE_PAYLOAD_TOMAK_SYNC ||
            !era_tomak_sync_wire_decode(response->payload, response->payload_len, &result->decoded.sync)) {
            return false;
        }
        const era_tomak_sync_wire_t *reply = &result->decoded.sync;
        bool operation = reply->operation == ERA_TOMAK_SYNC_WIRE_REJECT ||
                         reply->operation == ERA_TOMAK_SYNC_WIRE_PENDING ||
                         (request->semantic.sync.operation == ERA_TOMAK_SYNC_WIRE_OFFER &&
                          reply->operation == ERA_TOMAK_SYNC_WIRE_INSTALLED) ||
                         (request->semantic.sync.operation == ERA_TOMAK_SYNC_WIRE_ARM &&
                          (reply->operation == ERA_TOMAK_SYNC_WIRE_ARMED ||
                           reply->operation == ERA_TOMAK_SYNC_WIRE_APPLIED)) ||
                         (request->semantic.sync.operation == ERA_TOMAK_SYNC_WIRE_APPLIED &&
                          reply->operation == ERA_TOMAK_SYNC_WIRE_PAIR_READY);
        return operation && era_tomak_sync_wire_matches(&request->semantic.sync, reply, reply->operation);
    }
#endif
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

static void era_split_communication_core_initiator_result_full(void) {
    g_era_split_communication_core.initiator_pending = 0;
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
            era_split_communication_core_standing_input_blocked(request->owner_epoch, request->relation_generation)) {
            result.failure = ERA_SPLIT_TRANSACTION_FAILURE_QUEUE_EXPIRED;
            goto publish;
        }
    }

    era_split_wire_frame_t response;
    memset(&response, 0, sizeof(response));
    bool                            request_sent = false;
    era_split_transaction_failure_t failure      = ERA_SPLIT_TRANSACTION_FAILURE_NONE;
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
    if (result.result == ERA_SPLIT_TRANSACTION_RESULT_OK) {
        result.response_kind         = (uint8_t)response.kind;
        result.response_payload_len  = (uint8_t)response.payload_len;
        result.response_section_byte = response.kind == ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER_HOST_SOURCE_RSP && response.payload_len > 2 ? response.payload[2] : 0;
        if (!era_split_communication_core_initiator_decode(request, &response, &result)) {
            result.result  = ERA_SPLIT_TRANSACTION_RESULT_BAD;
            result.failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
        } else {
            era_split_transaction_engine_commit_received_frame(&response);
        }
    }

publish:
    era_split_communication_core_initiator_note_failure((era_split_transaction_failure_t)result.failure);

    era_split_communication_core_queue_record_t result_record = {
        .kind                   = ERA_SPLIT_COMMUNICATION_CORE_QUEUE_RECORD_INITIATOR_RESULT,
        .data.initiator_result = result,
    };
    if (!era_split_communication_core_result_push(&result_record)) {
        era_split_communication_core_initiator_result_full();
        return;
    }

    g_era_split_communication_core.initiator_result_ready = 1;
    __DMB();
    __SEV();
}

bool era_split_communication_core_enqueue_initiator(const era_split_communication_core_initiator_request_t *request, uint32_t queue_window_us) {
    if (queue_window_us == 0 || !era_split_communication_core_initiator_request_valid(request)) {
        return false;
    }

    era_split_communication_core_init();
    if (g_era_split_communication_core.initiator_pending || g_era_split_communication_core.initiator_result_ready ||
        era_split_communication_core_request_level() != 0 || era_split_communication_core_result_level() != 0) {
        return false;
    }
    if (!era_split_communication_core_owner_ensure_core1()) {
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
        return false;
    }

    era_split_communication_core_wake();
    return true;
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
    __DMB();
    return true;
}

bool era_split_communication_core_initiator_pending(void) {
    return g_era_split_communication_core.initiator_pending != 0;
}

bool era_split_communication_core_initiator_result_ready(void) {
    return g_era_split_communication_core.initiator_result_ready != 0;
}

void era_split_communication_core_initiator_queue_flushed(void) {
    g_era_split_communication_core.initiator_pending      = 0;
    g_era_split_communication_core.initiator_result_ready = 0;
}
