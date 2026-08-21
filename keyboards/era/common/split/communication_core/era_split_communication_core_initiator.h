// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../era_host_peer_transaction.h"
#include "../era_split_transaction_types.h"
#include "../era_split_wire_protocol.h"

typedef enum {
    ERA_SPLIT_COMMUNICATION_CORE_RX_WAIT_MODE_NONE                 = 0,
    ERA_SPLIT_COMMUNICATION_CORE_RX_WAIT_MODE_RESPONSE_WINDOW_POLL = 1,
} era_split_communication_core_rx_wait_mode_t;

/* The lanes core0 may queue a request on. A HEARTBEAT lane stood between the
   two below and had no enqueuer at all: core1's standing service builds the
   identical frame from its own period, so what core0 once queued as a
   heartbeat is now a wire operation with no request behind it. It kept its id
   and its counters as capture surface for one development phase and retired
   with that phase.

   `_COUNT` sizes the per-lane state array, so `_INVALID` owns slot 0 and the
   ids stay usable as subscripts. */
typedef enum {
    ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_INVALID = 0,
    ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS,
    ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH,
    ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_COUNT,
} era_split_communication_core_initiator_lane_t;

/* The one 0x20 request lane core0 still queues, and since R2 it is HOST-PEER's
   matrix and nothing else: DUAL-HOST's runtime push and HOST-PEER's AUTHORITY
   push both moved onto core1's standing exchange with the routes that selected
   them. This structure carried an INPUT layer byte and an AUTHORITY body for
   those two planners and kept them for two slices after they retired; both are
   gone with the encoder arms that read them.

   Since D3 the section content is load-bearing rather than incidental: it is
   what tells the responder that a request came from core0's lane rather than
   from core1's standing exchange, so the request validator admits exactly
   ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_CORE0_LANE_SECTIONS here. */
typedef struct {
    uint8_t sections;
    uint8_t matrix_seq;
    uint8_t packed_rows[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES];
} era_split_communication_core_source_push_semantic_t;

typedef struct {
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint16_t request_generation;
    uint8_t  lane;
    uint8_t  route_kind;
    uint8_t  route_reason;
    uint8_t  request_direction;
    uint8_t  expected_kind;
    uint8_t  alternate_expected_kind;
    uint16_t response_window_ms;
    uint32_t not_after_us;
    union {
        era_split_wire_session_status_t                    session_status;
        era_split_communication_core_source_push_semantic_t source_push;
    } semantic;
} era_split_communication_core_initiator_request_t;

typedef struct {
    uint8_t                          valid;
    era_split_wire_session_status_t status;
} era_split_communication_core_session_result_t;

typedef struct {
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint16_t request_generation;
    uint8_t  lane;
    uint8_t  route_kind;
    uint8_t  route_reason;
    uint8_t  tx_seq;
    uint8_t  response_kind;
    uint8_t  response_payload_len;
    uint8_t  response_section_byte;
    uint8_t  matrix_seq;
    /* Which sections this request carried, read back by core0 to run the
       matrix-link bookkeeping. The lane stopped being shared at R2 -- the
       runtime push and the AUTHORITY push moved to the standing exchange with
       their routes -- so this no longer tells two request shapes apart and no
       sent-state retires from it. What it is since D3 is the same fact that
       tells the responder this request came from core0's lane, which is why the
       validator admits exactly one value for it. */
    uint8_t  request_sections;
    uint8_t  request_sent;
    uint8_t  result;
    uint8_t  failure;
    union {
        era_split_communication_core_session_result_t session;
        era_host_peer_transaction_result_t            host_peer;
    } decoded;
} era_split_communication_core_initiator_result_t;

bool era_split_communication_core_enqueue_initiator(const era_split_communication_core_initiator_request_t *request);
bool era_split_communication_core_poll_initiator_result(era_split_communication_core_initiator_result_t *result);
bool era_split_communication_core_initiator_pending(void);
bool era_split_communication_core_initiator_result_ready(void);
void era_split_communication_core_note_initiator_result_stale(uint8_t lane);
