// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* For the lane enum the per-lane array below is indexed and sized by. */
#include "era_split_communication_core_initiator.h"

/* The published mirror of one initiator lane's state. `available` says the
   lane exists in this build; the rest are the lane record's own names,
   unprefixed, because the array index already says which lane. */
typedef struct {
    uint8_t  available;
    uint8_t  pending;
    uint8_t  result_ready;
    uint16_t generation;
    uint16_t last_result_generation;
    uint32_t submit_count;
    uint32_t accept_count;
    uint32_t full_count;
    uint32_t start_fail_count;
    uint32_t transaction_count;
    uint32_t result_count;
    uint32_t result_stale_count;
    uint32_t result_full_count;
    uint32_t ok_count;
    uint32_t miss_count;
    uint32_t bad_count;
    uint32_t fail_count;
    uint8_t  last_result;
    uint8_t  last_request_sent;
    uint8_t  last_route_kind;
    uint8_t  last_route_reason;
    uint8_t  last_response_kind;
} era_split_communication_core_lane_diagnostics_t;

typedef struct {
    uint8_t  available;
    uint8_t  initialized;
    uint8_t  launch_attempted;
    uint8_t  launched;
    uint8_t  running;
    uint8_t  stop_requested;
    uint8_t  wake_pending;
    uint8_t  launch_error;
    uint8_t  entry_timeout;
    uint8_t  stop_timeout;
    uint8_t  launch_error_stage;
    uint8_t  launch_error_phase;
    uint32_t start_count;
    uint32_t stop_count;
    uint32_t wake_count;
    uint32_t wake_observed_count;
    uint32_t loop_count;
    uint32_t idle_count;
    /* Core1's sleep instrument (diagnostic images only; zero on release):
       microseconds the lifecycle loop's own idle park lasted in total, and
       the backend's in-transaction parks as a count and total microseconds
       (era_split_transaction_backend.h). Boot-cumulative; a reader takes
       deltas against the elapsed window for the sleep share. */
    uint32_t idle_us;
    uint32_t park_count;
    uint32_t park_us;
    uint32_t launch_error_count;
    uint32_t entry_timeout_count;
    uint32_t stop_timeout_count;
    uint8_t  launch_capped;
    uint32_t dead_declared_count;
    uint8_t  queue_available;
    uint8_t  queue_capacity;
    uint8_t  queue_level;
    uint8_t  queue_high_water;
    uint8_t  queue_result_level;
    uint8_t  queue_result_high_water;
    uint16_t queue_generation;
    uint32_t queue_flush_count;
    uint8_t  backend_owner_available;
    uint8_t  backend_owner;
    uint8_t  backend_owner_role;
    uint8_t  backend_owner_revoke_pending;
    uint8_t  backend_owner_cancel_pending;
    uint8_t  backend_owner_reset_pending;
    uint16_t backend_owner_epoch;
    uint16_t backend_owner_released_epoch;
    uint16_t backend_owner_ready_epoch;
    uint32_t backend_owner_transfer_count;
    uint32_t backend_owner_revoke_count;
    uint32_t backend_owner_release_count;
    uint32_t backend_owner_ready_count;
    uint32_t backend_owner_transfer_timeout_count;
    uint32_t backend_owner_ready_timeout_count;
    uint32_t backend_owner_init_fail_count;
    uint32_t backend_owner_reclaim_count;
    uint32_t initiator_queue_expired_count;
    uint32_t initiator_owner_count;
    uint32_t initiator_epoch_count;
    uint32_t initiator_cancel_count;
    uint32_t initiator_reset_count;
    uint32_t initiator_pio_error_count;
    uint32_t initiator_send_timeout_count;
    uint32_t initiator_response_timeout_count;
    uint32_t initiator_partial_frame_count;
    uint32_t initiator_io_count;
    uint32_t initiator_decode_count;
    uint32_t initiator_response_contract_count;
    /* One published record per lane, indexed by the lane id, mirroring
       era_split_communication_core_lane_state_t. The three flat blocks this
       replaces were the same twenty-one names spelled three times, and the
       copy that filled them was ninety-five assignments that a loop now does.
       Slot LANE_INVALID is unused. */
    era_split_communication_core_lane_diagnostics_t lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_COUNT];
    /* SESSION_STATUS only. */
    uint8_t  session_last_failure;
    uint8_t  session_last_peer_status_valid;
    /* SOURCE_PUSH only: the HSRSP detail of the admitted answer. */
    uint8_t  source_push_last_response_payload_len;
    uint8_t  source_push_last_response_section_byte;
    uint8_t  source_push_last_matrix_seq;
    uint8_t  source_push_last_lock_state_valid;
    uint8_t  source_push_last_lock_state;
    uint8_t  source_push_last_visual_snapshot_valid;
    uint8_t  source_push_last_rgb_state_valid;
    uint8_t  source_push_last_storage_news_valid;
    uint8_t  source_push_last_storage_news;
    uint8_t  source_push_rx_wait_mode;
    uint32_t source_push_hsrsp_count;
    uint32_t source_push_visual_snapshot_count;
    uint32_t source_push_rgb_state_count;
    uint32_t source_push_storage_news_count;
    uint8_t  source_push_response_section_or;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    uint8_t  responder_available;
    uint8_t  responder_snapshot_valid;
    uint8_t  responder_snapshot_source;
    uint8_t  responder_source_push_slot_reserved;
    uint8_t  responder_source_push_result_ready;
    uint8_t  responder_owner_gate_ready;
    uint16_t responder_owner_epoch;
    uint16_t responder_relation_generation;
    uint16_t responder_snapshot_generation;
    uint16_t responder_last_result_generation;
    uint32_t responder_snapshot_publish_count;
    uint32_t responder_owner_gate_ready_count;
    uint32_t responder_owner_gate_block_count;
    uint32_t responder_slot_reserve_count;
    uint32_t responder_slot_full_count;
    uint32_t responder_accept_count;
    uint32_t responder_accept_stale_count;
    uint32_t responder_drain_count;
    uint32_t responder_noack_count;
    /* The two wire-fact counters core0 reads outside diagnostics -- accepted
       frames (the silence watch) and undecodable arrivals (the link listener)
       -- printed side by side because the listener's step is their ratio. */
    uint32_t responder_accepted_rx_count;
    uint32_t responder_undecodable_rx_count;
    uint32_t responder_quiet_count;
    uint32_t responder_response_prepare_count;
    uint32_t responder_response_prepare_fail_count;
    uint8_t  responder_last_matrix_seq;
    uint8_t  responder_last_request_kind_flags;
    uint8_t  responder_last_response_section_mask;
    uint8_t  responder_last_response_payload_len;
    uint8_t  responder_last_visual_reason;
    /* The anchor send-hold instrument (R6): last and worst elapsed between
       core0's capture stamp and core1's encode, in microseconds. */
    uint32_t responder_anchor_hold_last_us;
    uint32_t responder_anchor_hold_max_us;
#endif
} era_split_communication_core_diagnostics_t;

void era_split_communication_core_get_diagnostics_snapshot(era_split_communication_core_diagnostics_t *snapshot);
