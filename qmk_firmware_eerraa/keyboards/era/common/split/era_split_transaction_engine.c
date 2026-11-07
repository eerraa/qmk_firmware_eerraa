// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_transaction_engine.h"

#include <string.h>

#include "hal.h"
#include "era_split_transaction_backend.h"
#include "era_split_transaction_io.h"
#include "era_split_wire_frame.h"
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
#    include "diagnostics/era_split_wire_cause.h"
#endif

typedef struct {
    bool     initialized;
    uint8_t  tx_seq;
    uint8_t  ack_seq;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    volatile uint32_t compact_tx_count;
    volatile uint32_t compact_rx_count;
    volatile uint32_t compact_miss_count;
    volatile uint32_t compact_bad_count;
    volatile uint32_t compact_fail_count;
#endif
} era_split_transaction_engine_state_t;

static era_split_transaction_engine_state_t g_era_split_transaction_engine;

static void era_split_transaction_engine_reset_seq_state(void) {
    g_era_split_transaction_engine.tx_seq  = 0;
    g_era_split_transaction_engine.ack_seq = 0;
}

static void era_split_transaction_engine_init(void) {
    if (g_era_split_transaction_engine.initialized) {
        return;
    }
    memset(&g_era_split_transaction_engine, 0, sizeof(g_era_split_transaction_engine));
    g_era_split_transaction_engine.initialized = true;
    era_split_transaction_backend_init();
}

void era_split_transaction_engine_init_initiator_driver(void) {
    era_split_transaction_engine_init();
    if (era_split_transaction_backend_init_initiator()) {
        era_split_transaction_engine_reset_seq_state();
    }
}

void era_split_transaction_engine_init_responder_driver(void) {
    era_split_transaction_engine_init();
    if (era_split_transaction_backend_init_responder()) {
        era_split_transaction_engine_reset_seq_state();
    }
}

void era_split_transaction_engine_release_driver(void) {
    era_split_transaction_engine_init();
    era_split_transaction_backend_release();
    era_split_transaction_engine_reset_seq_state();
}

void era_split_transaction_engine_reset_link_state(void) {
    era_split_transaction_engine_init();
    era_split_transaction_engine_reset_seq_state();
    era_split_transaction_backend_reset_link_state();
}

bool era_split_transaction_engine_prepare_control(bool ext, uint8_t *control, uint8_t *tx_seq) {
    if (control == NULL || tx_seq == NULL) {
        return false;
    }

    uint8_t next_tx = era_split_wire_next_seq(g_era_split_transaction_engine.tx_seq);
    *control        = era_split_wire_control_byte(next_tx, g_era_split_transaction_engine.ack_seq, ext);
    *tx_seq         = next_tx;
    return true;
}

void era_split_transaction_engine_commit_prepared_tx(uint8_t tx_seq) {
    g_era_split_transaction_engine.tx_seq = tx_seq & ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK;
}

void era_split_transaction_engine_commit_received_frame(const era_split_wire_frame_t *frame) {
    if (frame == NULL) {
        return;
    }
    g_era_split_transaction_engine.ack_seq = frame->tx_seq & ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK;
}

static bool era_split_transaction_engine_response_kind_matches(era_split_wire_payload_kind_t expected_kind, era_split_wire_payload_kind_t alternate_expected_kind, era_split_wire_payload_kind_t response_kind) {
    if (expected_kind == ERA_SPLIT_WIRE_PAYLOAD_HP_STANDING_RSP) {
        return response_kind == ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK ||
               response_kind == ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER_HOST_SOURCE_RSP ||
               response_kind == ERA_SPLIT_WIRE_PAYLOAD_FLOW_RSP;
    }
    return response_kind == expected_kind || (alternate_expected_kind != ERA_SPLIT_WIRE_PAYLOAD_INVALID && response_kind == alternate_expected_kind);
}

static void era_split_transaction_engine_note_owned_result(era_split_transaction_engine_result_t result, era_split_transaction_failure_t failure) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    if (result == ERA_SPLIT_TRANSACTION_RESULT_MISS || result == ERA_SPLIT_TRANSACTION_RESULT_BAD || result == ERA_SPLIT_TRANSACTION_RESULT_FAIL) {
        era_split_wire_cause_finish(result, failure);
    }
#endif
    switch (result) {
        case ERA_SPLIT_TRANSACTION_RESULT_MISS:
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
            g_era_split_transaction_engine.compact_miss_count++;
#endif
            if (failure == ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT) {
                era_split_transaction_backend_clear();
            }
            break;
        case ERA_SPLIT_TRANSACTION_RESULT_BAD:
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
            g_era_split_transaction_engine.compact_bad_count++;
#endif
            if (failure == ERA_SPLIT_TRANSACTION_FAILURE_DECODE || failure == ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_CONTRACT) {
                era_split_transaction_backend_clear();
            }
            break;
        case ERA_SPLIT_TRANSACTION_RESULT_FAIL:
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
            g_era_split_transaction_engine.compact_fail_count++;
#endif
            if (failure == ERA_SPLIT_TRANSACTION_FAILURE_IO || failure == ERA_SPLIT_TRANSACTION_FAILURE_PIO_ERROR || failure == ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME) {
                era_split_transaction_backend_clear();
            }
            break;
        default:
            break;
    }
}

era_split_transaction_engine_result_t era_split_transaction_engine_receive_idle_owned(era_split_wire_direction_t expected_direction,
                                                                                       uint16_t first_byte_timeout_ms,
                                                                                       uint16_t frame_timeout_ms,
                                                                                       uint16_t owner_epoch,
                                                                                       uint8_t *wire,
                                                                                       uint16_t wire_capacity,
                                                                                       era_split_wire_frame_t *frame,
                                                                                       era_split_transaction_failure_t *failure) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    era_split_wire_cause_begin(owner_epoch, ERA_CAUSE_IDLE_BEGIN);
#endif
    era_split_transaction_engine_result_t result = era_split_transaction_io_receive_idle_owned(expected_direction,
                                                                                                 first_byte_timeout_ms,
                                                                                                 frame_timeout_ms,
                                                                                                 owner_epoch,
                                                                                                 wire,
                                                                                                 wire_capacity,
                                                                                                 frame,
                                                                                                 failure);
    if (result == ERA_SPLIT_TRANSACTION_RESULT_OK) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
        g_era_split_transaction_engine.compact_rx_count++;
#endif
    } else if (result != ERA_SPLIT_TRANSACTION_RESULT_NONE) {
        era_split_transaction_engine_note_owned_result(result, failure != NULL ? *failure : ERA_SPLIT_TRANSACTION_FAILURE_IO);
    }
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    era_split_wire_cause_end();
#endif
    return result;
}

era_split_transaction_engine_result_t era_split_transaction_engine_send_compact_response_owned(era_split_wire_direction_t response_direction,
                                                                                                const uint8_t *payload,
                                                                                                uint8_t payload_len,
                                                                                                uint8_t tx_seq,
                                                                                                uint16_t owner_epoch,
                                                                                                era_split_transaction_failure_t *failure) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    era_split_wire_cause_begin(owner_epoch, ERA_CAUSE_SEND);
#endif
    era_split_transaction_engine_result_t result = era_split_transaction_io_send_compact_owned(response_direction,
                                                                                                 payload,
                                                                                                 payload_len,
                                                                                                 owner_epoch,
                                                                                                 failure);
    if (result != ERA_SPLIT_TRANSACTION_RESULT_OK) {
        era_split_transaction_engine_note_owned_result(result, failure != NULL ? *failure : ERA_SPLIT_TRANSACTION_FAILURE_IO);
        return result;
    }
    era_split_transaction_engine_commit_prepared_tx(tx_seq);
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    g_era_split_transaction_engine.compact_tx_count++;
    era_split_wire_cause_end();
#endif
    return result;
}

era_split_transaction_engine_result_t era_split_transaction_engine_transact_compact_owned(era_split_wire_direction_t request_direction,
                                                                                          const uint8_t *payload,
                                                                                          uint8_t payload_len,
                                                                                          uint8_t tx_seq,
                                                                                          era_split_wire_payload_kind_t expected_kind,
                                                                                          era_split_wire_payload_kind_t alternate_expected_kind,
                                                                                          uint16_t response_window_ms,
                                                                                          uint16_t owner_epoch,
                                                                                          era_split_wire_frame_t *response,
                                                                                          bool *request_sent,
                                                                                          era_split_transaction_failure_t *failure) {
    if (request_sent != NULL) {
        *request_sent = false;
    }
    if (failure == NULL || response == NULL) {
        return ERA_SPLIT_TRANSACTION_RESULT_BAD;
    }
    *failure = ERA_SPLIT_TRANSACTION_FAILURE_NONE;

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    era_split_wire_cause_begin(owner_epoch, ERA_CAUSE_SEND);
#endif
    era_split_transaction_engine_result_t result = era_split_transaction_io_send_compact_owned(request_direction, payload, payload_len, owner_epoch, failure);
    if (result != ERA_SPLIT_TRANSACTION_RESULT_OK) {
        era_split_transaction_engine_note_owned_result(result, *failure);
        return result;
    }

    era_split_transaction_engine_commit_prepared_tx(tx_seq);
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    g_era_split_transaction_engine.compact_tx_count++;
#endif
    if (request_sent != NULL) {
        *request_sent = true;
    }

    era_split_wire_direction_t response_direction = request_direction == ERA_SPLIT_WIRE_DIRECTION_PRIMARY_TO_SECONDARY ? ERA_SPLIT_WIRE_DIRECTION_SECONDARY_TO_PRIMARY : ERA_SPLIT_WIRE_DIRECTION_PRIMARY_TO_SECONDARY;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    era_split_wire_cause_begin(owner_epoch, ERA_CAUSE_RESPONSE_BODY);
#endif
    result = era_split_transaction_io_receive_compact_owned(response_direction, response_window_ms, owner_epoch, response, failure);
    if (result != ERA_SPLIT_TRANSACTION_RESULT_OK) {
        era_split_transaction_engine_note_owned_result(result, *failure);
        return result;
    }
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    g_era_split_transaction_engine.compact_rx_count++;
#endif

    bool kind_matches = era_split_transaction_engine_response_kind_matches(expected_kind, alternate_expected_kind, response->kind);
    if (!kind_matches || response->ack_seq != tx_seq) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
        era_split_wire_cause_contract(!kind_matches, response->ack_seq != tx_seq, expected_kind, alternate_expected_kind, response->kind, tx_seq, response->ack_seq);
#endif
        *failure = ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_CONTRACT;
        era_split_transaction_engine_note_owned_result(ERA_SPLIT_TRANSACTION_RESULT_BAD, *failure);
        return ERA_SPLIT_TRANSACTION_RESULT_BAD;
    }

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    era_split_wire_cause_end();
#endif
    return ERA_SPLIT_TRANSACTION_RESULT_OK;
}

void era_split_transaction_engine_get_diagnostics_snapshot(era_split_transaction_engine_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    __DMB();
    snapshot->compact_tx_count   = g_era_split_transaction_engine.compact_tx_count;
    snapshot->compact_rx_count   = g_era_split_transaction_engine.compact_rx_count;
    snapshot->compact_miss_count = g_era_split_transaction_engine.compact_miss_count;
    snapshot->compact_bad_count  = g_era_split_transaction_engine.compact_bad_count;
    snapshot->compact_fail_count = g_era_split_transaction_engine.compact_fail_count;
#endif
}
