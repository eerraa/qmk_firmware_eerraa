// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_split_transaction_backend.h"
#include "era_split_transaction_types.h"
#include "era_split_wire_protocol.h"

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
typedef struct {
    uint32_t wait_rx_us;
    uint32_t rx_decode_us;
} era_split_transaction_io_receive_timing_t;
#endif

era_split_transaction_engine_result_t era_split_transaction_io_send_compact_owned(era_split_wire_direction_t      direction,
                                                                                  const uint8_t                  *payload,
                                                                                  uint8_t                         payload_len,
                                                                                  uint16_t                        owner_epoch,
                                                                                  era_split_transaction_failure_t *failure
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
                                                                                  ,
                                                                                  era_split_transaction_backend_send_timing_t *timing
#endif
);
era_split_transaction_engine_result_t era_split_transaction_io_receive_compact_owned(era_split_wire_direction_t      expected_direction,
                                                                                     uint16_t                        response_window_ms,
                                                                                     uint16_t                        owner_epoch,
                                                                                     era_split_wire_frame_t         *frame,
                                                                                     era_split_transaction_failure_t *failure
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
                                                                                     ,
                                                                                     era_split_transaction_io_receive_timing_t *timing
#endif
);
/* Responder idle receive over the caller's raw buffer. A caller whose
 * buffer holds only a compact frame keeps the former compact-only contract:
 * the bulk length escape is rejected before any body byte is read. A caller
 * passing bulk-page capacity accepts both lanes, which is what admits the
 * initiator-sent PUSH_CHUNK_REQ. The buffer is a parameter because the one
 * bulk-capable caller owns a static scratch for it — a core1 stack buffer
 * of that size would break the zero-slack stack floor. */
era_split_transaction_engine_result_t era_split_transaction_io_receive_idle_owned(era_split_wire_direction_t       expected_direction,
                                                                                  uint16_t                         first_byte_timeout_ms,
                                                                                  uint16_t                         frame_timeout_ms,
                                                                                  uint16_t                         owner_epoch,
                                                                                  uint8_t                         *wire,
                                                                                  uint16_t                         wire_capacity,
                                                                                  era_split_wire_frame_t          *frame,
                                                                                  era_split_transaction_failure_t *failure);
