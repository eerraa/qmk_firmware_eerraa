// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_split_transaction_types.h"
#include "era_split_wire_protocol.h"

void era_split_transaction_engine_init_initiator_driver(void);
void era_split_transaction_engine_init_responder_driver(void);
void era_split_transaction_engine_release_driver(void);
void era_split_transaction_engine_reset_link_state(void);

bool era_split_transaction_engine_prepare_control(bool ext, uint8_t *control, uint8_t *tx_seq);
void era_split_transaction_engine_commit_prepared_tx(uint8_t tx_seq);
void era_split_transaction_engine_commit_received_frame(const era_split_wire_frame_t *frame);

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
typedef struct {
    uint32_t start_us;
    uint32_t end_us;
    uint32_t generation;
    uint8_t  route_kind;
    uint8_t  route_reason;
    uint8_t  result;
    uint8_t  valid;
} era_split_transaction_route_window_t;

_Static_assert(sizeof(era_split_transaction_route_window_t) == 16U,
               "ERA transaction route-window diagnostic budget changed.");

void era_split_transaction_engine_timing_begin_route(uint8_t route_kind, uint8_t route_reason);
void era_split_transaction_engine_timing_end_route(void);
bool era_split_transaction_engine_timing_last_completed_route(era_split_transaction_route_window_t *window);
#endif

era_split_transaction_engine_result_t era_split_transaction_engine_transact_compact_owned(era_split_wire_direction_t       request_direction,
                                                                                          const uint8_t                   *payload,
                                                                                          uint8_t                          payload_len,
                                                                                          uint8_t                          tx_seq,
                                                                                          era_split_wire_payload_kind_t   expected_kind,
                                                                                          era_split_wire_payload_kind_t   alternate_expected_kind,
                                                                                          uint16_t                         response_window_ms,
                                                                                          uint16_t                         owner_epoch,
                                                                                          era_split_wire_frame_t          *response,
                                                                                          bool                            *request_sent,
                                                                                          era_split_transaction_failure_t *failure);
/* Idle receive over the caller's raw buffer; both frame lanes when the
 * buffer has bulk-page capacity, compact-only (bulk escape rejected)
 * otherwise. See era_split_transaction_io.h for the buffer contract. */
era_split_transaction_engine_result_t era_split_transaction_engine_receive_idle_owned(era_split_wire_direction_t       expected_direction,
                                                                                       uint16_t                         first_byte_timeout_ms,
                                                                                       uint16_t                         frame_timeout_ms,
                                                                                       uint16_t                         owner_epoch,
                                                                                       uint8_t                         *wire,
                                                                                       uint16_t                         wire_capacity,
                                                                                       era_split_wire_frame_t          *frame,
                                                                                       era_split_transaction_failure_t *failure);
era_split_transaction_engine_result_t era_split_transaction_engine_send_compact_response_owned(era_split_wire_direction_t       response_direction,
                                                                                                const uint8_t                   *payload,
                                                                                                uint8_t                          payload_len,
                                                                                                uint8_t                          tx_seq,
                                                                                                uint16_t                         owner_epoch,
                                                                                                era_split_transaction_failure_t *failure);

void era_split_transaction_engine_get_diagnostics_snapshot(era_split_transaction_engine_diagnostics_t *snapshot);
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
void era_split_transaction_engine_publish_diagnostics_snapshot(void);
bool era_split_transaction_engine_diagnostics_snapshot_fresh(void);
uint32_t era_split_transaction_engine_diagnostics_fallback_count(void);
#else
static inline void era_split_transaction_engine_publish_diagnostics_snapshot(void) {}
#endif
