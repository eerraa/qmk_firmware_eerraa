// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_communication_core_storage_service.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "hardware/structs/timer.h"
#include "era_split_communication_core_owner.h"
#include "era_split_communication_core_storage.h"
#include "../era_split_transaction_backend.h"
#include "../era_split_transaction_engine.h"
#include "../era_split_wire_frame.h"

static era_split_transaction_failure_t era_split_communication_core_storage_failure_from_access(era_split_communication_core_backend_access_t access) {
    switch (access) {
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OWNER:
            return ERA_SPLIT_TRANSACTION_FAILURE_OWNER;
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_EPOCH:
            return ERA_SPLIT_TRANSACTION_FAILURE_EPOCH;
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_CANCEL:
            return ERA_SPLIT_TRANSACTION_FAILURE_CANCEL;
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_RESET:
            return ERA_SPLIT_TRANSACTION_FAILURE_RESET;
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK:
        default:
            return ERA_SPLIT_TRANSACTION_FAILURE_NONE;
    }
}

static void era_split_communication_core_storage_note_io_failure(era_split_transaction_failure_t failure) {
    if (failure == ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT ||
        failure == ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_CONTRACT) {
        era_split_transaction_backend_clear();
    } else if (failure == ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME ||
               failure == ERA_SPLIT_TRANSACTION_FAILURE_DECODE ||
               failure == ERA_SPLIT_TRANSACTION_FAILURE_PIO_ERROR ||
               failure == ERA_SPLIT_TRANSACTION_FAILURE_IO) {
        era_split_transaction_engine_reset_link_state();
    }
}

static bool era_split_communication_core_storage_send_payload(era_split_wire_direction_t direction, const uint8_t *payload, uint16_t payload_len, era_split_wire_frame_lane_t lane, uint16_t owner_epoch, era_split_transaction_failure_t *failure) {
    if (payload == NULL || failure == NULL) {
        return false;
    }

    uint8_t *wire = era_split_communication_core_storage_wire_frame_scratch();
    uint16_t frame_len = 0;
    bool encoded;
    if (lane == ERA_SPLIT_WIRE_FRAME_LANE_COMPACT) {
        uint8_t compact_len = 0;
        encoded = payload_len <= UINT8_MAX &&
                  era_split_wire_encode_compact_frame(direction, payload, (uint8_t)payload_len, wire, &compact_len);
        frame_len = compact_len;
    } else {
        encoded = lane == ERA_SPLIT_WIRE_FRAME_LANE_BULK_PAGE &&
                  era_split_wire_encode_bulk_page_frame(direction, payload, payload_len, wire, &frame_len);
    }
    if (!encoded) {
        *failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
        return false;
    }

    era_split_transaction_backend_wait_result_t wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
    if (!era_split_transaction_backend_send_owned(wire, frame_len, owner_epoch, &wait_result)) {
        *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_SEND_TIMEOUT);
        if (*failure == ERA_SPLIT_TRANSACTION_FAILURE_NONE) {
            *failure = ERA_SPLIT_TRANSACTION_FAILURE_IO;
        }
        era_split_communication_core_storage_note_io_failure(*failure);
        return false;
    }
    *failure = ERA_SPLIT_TRANSACTION_FAILURE_NONE;
    return true;
}

static bool era_split_communication_core_storage_receive_response(uint16_t owner_epoch, uint16_t response_window_ms, era_split_wire_direction_t expected_direction, era_split_wire_frame_t *decoded, era_split_transaction_engine_result_t *result, era_split_transaction_failure_t *failure) {
    if (decoded == NULL || result == NULL || failure == NULL) {
        return false;
    }

    *result  = ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    *failure = ERA_SPLIT_TRANSACTION_FAILURE_NONE;
    uint8_t *wire = era_split_communication_core_storage_decoded_frame_scratch();
    era_split_transaction_backend_wait_result_t wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
    era_split_transaction_backend_response_window_t window;
    if (!era_split_transaction_backend_response_window_begin(owner_epoch, response_window_ms, &window, &wait_result)) {
        *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT);
        era_split_communication_core_storage_note_io_failure(*failure);
        return false;
    }
    if (!era_split_transaction_backend_receive_response_window_until(&window, wire, 1, &wait_result)) {
        *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT);
        *result  = *failure == ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT ? ERA_SPLIT_TRANSACTION_RESULT_MISS : ERA_SPLIT_TRANSACTION_RESULT_FAIL;
        era_split_communication_core_storage_note_io_failure(*failure);
        return false;
    }
    if ((wire[0] & ERA_SPLIT_WIRE_FRAME_MARKER_MASK) != ERA_SPLIT_WIRE_FRAME_MARKER) {
        *result  = ERA_SPLIT_TRANSACTION_RESULT_BAD;
        *failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
        era_split_communication_core_storage_note_io_failure(*failure);
        return false;
    }

    uint16_t frame_len;
    uint8_t compact_payload_len = wire[0] & ERA_SPLIT_WIRE_FRAME_LENGTH_MASK;
    bool decoded_ok;
    if (compact_payload_len != ERA_SPLIT_WIRE_BULK_LENGTH_ESCAPE) {
        if (compact_payload_len > ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN ||
            !era_split_transaction_backend_receive_response_window_until(&window, &wire[1], (size_t)compact_payload_len + 1U, &wait_result)) {
            *failure = compact_payload_len > ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN ?
                           ERA_SPLIT_TRANSACTION_FAILURE_DECODE :
                           era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME);
            *result = compact_payload_len > ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN ? ERA_SPLIT_TRANSACTION_RESULT_BAD : ERA_SPLIT_TRANSACTION_RESULT_FAIL;
            era_split_communication_core_storage_note_io_failure(*failure);
            return false;
        }
        frame_len  = (uint16_t)(compact_payload_len + 2U);
        decoded_ok = era_split_wire_decode_compact_frame(wire, (uint8_t)frame_len, decoded);
    } else {
        /* The one wire-time window this tree computes outside the backend, so
           it is the one that has to say the scale out loud. A 268-byte bulk
           body is 5.8 ms at High and 23.3 ms at Low, against a constant sized
           for the first. */
        uint32_t body_deadline = timer_hw->timerawl +
                                 (ERA_SPLIT_WIRE_BULK_PAGE_BODY_TIMEOUT_MS * 1000U * era_split_transaction_backend_wire_scale());
        if ((int32_t)(body_deadline - window.deadline_us) < 0) {
            window.deadline_us = body_deadline;
        }
        if (!era_split_transaction_backend_receive_response_window_until(&window, &wire[1], 2, &wait_result)) {
            *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME);
            era_split_communication_core_storage_note_io_failure(*failure);
            return false;
        }
        uint16_t bulk_payload_len = era_split_wire_get16(&wire[1]);
        if (bulk_payload_len == 0 || bulk_payload_len > ERA_SPLIT_WIRE_BULK_PAGE_MAX_PAYLOAD_LEN) {
            *result  = ERA_SPLIT_TRANSACTION_RESULT_BAD;
            *failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
            era_split_communication_core_storage_note_io_failure(*failure);
            return false;
        }
        if (!era_split_transaction_backend_receive_response_window_until(&window, &wire[3], (size_t)bulk_payload_len + 4U, &wait_result)) {
            *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME);
            era_split_communication_core_storage_note_io_failure(*failure);
            return false;
        }
        frame_len  = (uint16_t)(bulk_payload_len + 7U);
        decoded_ok = era_split_wire_decode_bulk_page_frame(wire, frame_len, decoded);
    }

    if (!decoded_ok || decoded->direction != expected_direction) {
        *result  = ERA_SPLIT_TRANSACTION_RESULT_BAD;
        *failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
        era_split_communication_core_storage_note_io_failure(*failure);
        return false;
    }
    *result = ERA_SPLIT_TRANSACTION_RESULT_OK;
    return true;
}

static era_split_transaction_failure_t era_split_communication_core_storage_failure_from_classification(era_split_communication_core_storage_request_classification_t classification) {
    switch (classification) {
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_QUEUE_EXPIRED:
            return ERA_SPLIT_TRANSACTION_FAILURE_QUEUE_EXPIRED;
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_OWNER_STALE:
            return ERA_SPLIT_TRANSACTION_FAILURE_EPOCH;
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_CANCELLED:
            return ERA_SPLIT_TRANSACTION_FAILURE_CANCEL;
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_RESET:
            return ERA_SPLIT_TRANSACTION_FAILURE_RESET;
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_INVALID:
            return ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_RESULT_FULL:
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_RELATION_STALE:
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_GENERATION_STALE:
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_POLICY_STALE:
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_TRANSACTION_STALE:
        default:
            return ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_CONTRACT;
    }
}

bool era_split_communication_core_storage_service_initiator_once(uint16_t owner_epoch) {
    era_split_communication_core_storage_initiator_request_t request;
    if (!era_split_communication_core_storage_claim_initiator_request(&request)) {
        return false;
    }

    era_split_communication_core_storage_initiator_result_t *result =
        era_split_communication_core_storage_begin_initiator_result(request.request_generation);
    if (result == NULL) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
        era_split_communication_core_storage_note_initiator_probe(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_BEGIN, false, NULL);
#endif
        // A failed begin does not own any ready result and must never erase it.
        era_split_communication_core_storage_release_initiator_request(request.request_generation);
        return true;
    }
    memset(result, 0, sizeof(*result));
    result->source_revision       = request.source_revision;
    result->image_crc32           = request.image_crc32;
    result->owner_epoch           = request.owner_epoch;
    result->relation_generation   = request.relation_generation;
    result->request_generation    = request.request_generation;
    result->policy_generation     = request.policy_generation;
    result->transaction_generation = request.transaction_generation;
    result->domain                = request.domain;
    result->schema                = request.schema;
    result->chunk_id              = request.chunk_id;
    result->result                = ERA_SPLIT_TRANSACTION_RESULT_FAIL;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    era_split_communication_core_storage_note_initiator_probe(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_BEGIN, true, result);
#endif

    era_split_communication_core_backend_access_t access = era_split_communication_core_owner_backend_access(owner_epoch);
    era_split_communication_core_storage_execution_context_t context = {
        .now_us                 = timer_hw->timerawl,
        .owner_epoch            = owner_epoch,
        .relation_generation    = request.relation_generation,
        .request_generation     = request.request_generation,
        .policy_generation      = request.policy_generation,
        .transaction_generation = request.transaction_generation,
        .cancel_requested       = access == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_CANCEL,
        .reset_requested        = access == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_RESET,
    };
    era_split_communication_core_storage_request_classification_t classification =
        era_split_communication_core_storage_classify_request(&request, &context);
    if (access != ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK || classification != ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_READY) {
        result->result  = classification == ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_INVALID ? ERA_SPLIT_TRANSACTION_RESULT_BAD : ERA_SPLIT_TRANSACTION_RESULT_FAIL;
        result->failure = access != ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK ?
                              era_split_communication_core_storage_failure_from_access(access) :
                              era_split_communication_core_storage_failure_from_classification(classification);
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
        era_split_communication_core_storage_note_initiator_probe(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_BEGIN, false, result);
#endif
        (void)era_split_communication_core_storage_publish_initiator_result(result);
        return true;
    }

    uint8_t control = 0;
    uint8_t tx_seq  = 0;
    uint8_t compact_payload[ERA_SPLIT_COMMUNICATION_CORE_STORAGE_COMPACT_PAYLOAD_BYTES];
    uint8_t *payload = compact_payload;
    uint16_t payload_len = 0;
    era_split_wire_frame_lane_t request_lane = ERA_SPLIT_WIRE_FRAME_LANE_COMPACT;
    bool encoded = era_split_transaction_engine_prepare_control(true, &control, &tx_seq);
    if (encoded) {
        if (request.operation == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ) {
            /* The one bulk request: built in the decoded-frame scratch from
             * the initiator's own published image; the scratch is free for
             * RX again once send_payload has framed it into the wire
             * scratch. */
            request_lane = ERA_SPLIT_WIRE_FRAME_LANE_BULK_PAGE;
            payload      = era_split_communication_core_storage_decoded_frame_scratch();
            encoded      = era_split_communication_core_storage_build_push_chunk_payload(control, &request, payload, &payload_len);
        } else {
            encoded = era_split_communication_core_storage_encode_request_payload(control, &request, payload, &payload_len);
        }
    }
    if (!encoded) {
        result->result  = ERA_SPLIT_TRANSACTION_RESULT_BAD;
        result->failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
        era_split_communication_core_storage_note_initiator_probe(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_ENCODE, false, result);
#endif
        (void)era_split_communication_core_storage_publish_initiator_result(result);
        return true;
    }

    era_split_transaction_failure_t failure = ERA_SPLIT_TRANSACTION_FAILURE_NONE;
    if (!era_split_communication_core_storage_send_payload(ERA_SPLIT_WIRE_DIRECTION_PRIMARY_TO_SECONDARY, payload, payload_len, request_lane, owner_epoch, &failure)) {
        result->failure = failure;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
        era_split_communication_core_storage_note_initiator_probe(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_TX, false, result);
#endif
        (void)era_split_communication_core_storage_publish_initiator_result(result);
        return true;
    }
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    era_split_communication_core_storage_note_initiator_probe(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_TX, true, result);
#endif
    era_split_transaction_engine_commit_prepared_tx(tx_seq);

    era_split_wire_frame_t *response = era_split_communication_core_storage_decoded_semantic_frame_scratch();
    memset(response, 0, sizeof(*response));
    era_split_transaction_engine_result_t transaction_result = ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    if (!era_split_communication_core_storage_receive_response(owner_epoch, request.response_window_ms, ERA_SPLIT_WIRE_DIRECTION_SECONDARY_TO_PRIMARY, response, &transaction_result, &failure)) {
        result->result  = transaction_result;
        result->failure = failure;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
        era_split_communication_core_storage_note_initiator_probe(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_RX, false, result);
#endif
        (void)era_split_communication_core_storage_publish_initiator_result(result);
        return true;
    }
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    era_split_communication_core_storage_note_initiator_probe(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_RX, true, result);
#endif
    if (response->kind != ERA_SPLIT_WIRE_PAYLOAD_EEPROM_SYNC || response->ack_seq != tx_seq ||
        !era_split_communication_core_storage_decode_response_payload(&request, response->payload, response->payload_len, response->lane, tx_seq, result)) {
        result->result  = ERA_SPLIT_TRANSACTION_RESULT_BAD;
        result->failure = ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_CONTRACT;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
        era_split_communication_core_storage_note_initiator_probe(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_CONTRACT, false, result);
#endif
        era_split_communication_core_storage_note_io_failure((era_split_transaction_failure_t)result->failure);
    } else {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
        era_split_communication_core_storage_note_initiator_probe(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_CONTRACT, true, result);
#endif
        era_split_transaction_engine_commit_received_frame(response);
    }
    if (!era_split_communication_core_storage_publish_initiator_result(result)) {
        era_split_communication_core_storage_cancel_initiator_result(request.request_generation);
    }
    era_split_transaction_engine_publish_diagnostics_snapshot();
    return true;
}

bool era_split_communication_core_storage_service_responder_frame(uint16_t owner_epoch, const era_split_wire_frame_t *frame) {
    if (frame == NULL || frame->kind != ERA_SPLIT_WIRE_PAYLOAD_EEPROM_SYNC ||
        (frame->lane != ERA_SPLIT_WIRE_FRAME_LANE_COMPACT &&
         !(frame->lane == ERA_SPLIT_WIRE_FRAME_LANE_BULK_PAGE && frame->payload_len >= 2U &&
           frame->payload[1] == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ))) {
        return false;
    }

    era_split_communication_core_storage_responder_snapshot_t snapshot;
    if (!era_split_communication_core_storage_claim_responder_snapshot(&snapshot)) {
        return true;
    }
    if (snapshot.owner_epoch != owner_epoch ||
        era_split_communication_core_owner_backend_access(owner_epoch) != ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK) {
        era_split_communication_core_storage_release_responder_snapshot(snapshot.snapshot_generation);
        return true;
    }

    era_split_communication_core_storage_initiator_request_t request;
    if (!era_split_communication_core_storage_decode_request_payload(frame->payload, frame->payload_len, frame->lane, &request)) {
        era_split_communication_core_storage_release_responder_snapshot(snapshot.snapshot_generation);
        return true;
    }
    request.owner_epoch         = owner_epoch;
    request.relation_generation = snapshot.relation_generation;
    request.request_generation  = snapshot.snapshot_generation;

    if (!era_split_communication_core_storage_reserve_responder_result(snapshot.snapshot_generation)) {
        era_split_communication_core_storage_note_responder_full();
        era_split_communication_core_storage_release_responder_snapshot(snapshot.snapshot_generation);
        return true;
    }

    era_split_communication_core_storage_responder_result_t previous;
    era_split_communication_core_storage_responder_result_t *previous_ptr =
        era_split_communication_core_storage_copy_previous_responder_result(&previous) ? &previous : NULL;
    era_split_communication_core_storage_responder_result_t result;
    if (!era_split_communication_core_storage_plan_responder(&snapshot, &request, previous_ptr, &result)) {
        era_split_communication_core_storage_cancel_responder_result(snapshot.snapshot_generation);
        return true;
    }
    if (result.replayed) {
        era_split_communication_core_storage_note_replay();
    }
    if (!result.replayed && request.operation == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ &&
        result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER) {
        /* The plan accepted this chunk for staging: write it into the
         * pinned image before answering, refusing on any publication
         * instability so a target change never half-lands. */
        if (!era_split_communication_core_storage_stage_push_chunk(&snapshot, result.chunk_id,
                                                                   &frame->payload[12], result.data_length)) {
            result.status      = ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED;
            result.chunk_id    = request.chunk_id;
            result.data_length = 0;
        }
    }

    era_split_transaction_engine_commit_received_frame(frame);
    uint8_t control = 0;
    uint8_t tx_seq  = 0;
    uint8_t *payload = era_split_communication_core_storage_decoded_frame_scratch();
    uint16_t payload_len = 0;
    era_split_wire_frame_lane_t lane = ERA_SPLIT_WIRE_FRAME_LANE_COMPACT;
    if (!era_split_transaction_engine_prepare_control(true, &control, &tx_seq) ||
        !era_split_communication_core_storage_prepare_responder_payload(control, &snapshot, &result, payload, &payload_len, &lane)) {
        result.result  = ERA_SPLIT_TRANSACTION_RESULT_BAD;
        result.failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
    } else {
        era_split_transaction_failure_t failure = ERA_SPLIT_TRANSACTION_FAILURE_NONE;
        if (era_split_communication_core_storage_send_payload(ERA_SPLIT_WIRE_DIRECTION_SECONDARY_TO_PRIMARY, payload, payload_len, lane, owner_epoch, &failure)) {
            era_split_transaction_engine_commit_prepared_tx(tx_seq);
            result.response_sent   = 1;
            result.result          = ERA_SPLIT_TRANSACTION_RESULT_OK;
            result.failure         = ERA_SPLIT_TRANSACTION_FAILURE_NONE;
        } else {
            result.result  = ERA_SPLIT_TRANSACTION_RESULT_FAIL;
            result.failure = failure;
        }
    }

    if (result.response_sent && request.operation == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ &&
        request.detail == (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_COMPLETE &&
        result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY) {
        /* The provisional answer to a repeated complete poll publishes
         * nothing: core0's drain of it is an explicit no-op, every poll is
         * re-planned from the published snapshot (the replay guard refuses
         * to latch it), and the previous-result seat must keep the apply
         * trigger's answer rather than be clobbered by the poll's. Releasing
         * the reservation unused keeps the slot free while the applying
         * core0 sits inside its own flash stalls — holding it there muted
         * this responder into one initiator timeout per stalled poll. The
         * DURABLE flip (status COMPLETE), every refusal, and a failed send
         * still publish. */
        era_split_communication_core_storage_cancel_responder_result(snapshot.snapshot_generation);
    } else if (!era_split_communication_core_storage_publish_responder_result(&result)) {
        era_split_communication_core_storage_cancel_responder_result(snapshot.snapshot_generation);
    }
    era_split_transaction_engine_publish_diagnostics_snapshot();
    return true;
}
