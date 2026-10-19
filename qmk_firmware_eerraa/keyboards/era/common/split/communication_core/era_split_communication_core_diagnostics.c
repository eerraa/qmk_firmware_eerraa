// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_communication_core_internal.h"

#include <stddef.h>
#include <string.h>

#include "hal.h"
#include "era_split_communication_core_owner.h"

void era_split_communication_core_get_diagnostics_snapshot(era_split_communication_core_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    __DMB();
    snapshot->available           = 1;
    snapshot->initialized         = g_era_split_communication_core.initialized;
    snapshot->launch_attempted    = g_era_split_communication_core.launch_attempted;
    snapshot->launched            = g_era_split_communication_core.launched;
    snapshot->running             = g_era_split_communication_core.running;
    snapshot->stop_requested      = g_era_split_communication_core.stop_requested;
    snapshot->wake_pending        = g_era_split_communication_core.wake_pending;
    snapshot->launch_error        = g_era_split_communication_core.launch_error;
    snapshot->entry_timeout       = g_era_split_communication_core.entry_timeout;
    snapshot->stop_timeout        = g_era_split_communication_core.stop_timeout;
    snapshot->launch_error_stage  = g_era_split_communication_core.launch_error_stage;
    snapshot->launch_error_phase  = g_era_split_communication_core.launch_error_phase;
    snapshot->start_count         = g_era_split_communication_core.start_count;
    snapshot->stop_count          = g_era_split_communication_core.stop_count;
    snapshot->wake_count          = g_era_split_communication_core.wake_count;
    snapshot->wake_observed_count = g_era_split_communication_core.wake_observed_count;
    snapshot->loop_count          = g_era_split_communication_core.loop_count;
    snapshot->idle_count          = g_era_split_communication_core.idle_count;
#ifdef ERA_SPLIT_CORE1_PARK_DIAGNOSTICS_ENABLE
    snapshot->idle_us = g_era_split_communication_core.idle_us;
    era_split_transaction_backend_get_park_diagnostics(&snapshot->park_count, &snapshot->park_us);
#else
    snapshot->idle_us    = 0;
    snapshot->park_count = 0;
    snapshot->park_us    = 0;
#endif
    snapshot->launch_error_count  = g_era_split_communication_core.launch_error_count;
    snapshot->entry_timeout_count = g_era_split_communication_core.entry_timeout_count;
    snapshot->stop_timeout_count  = g_era_split_communication_core.stop_timeout_count;
    snapshot->launch_capped       = g_era_split_communication_core.launch_capped;
    snapshot->dead_declared_count = g_era_split_communication_core.dead_declared_count;
    snapshot->queue_available              = 1;
    snapshot->queue_capacity               = ERA_SPLIT_COMMUNICATION_CORE_QUEUE_SLOTS - 1U;
    snapshot->queue_level                  = era_split_communication_core_request_level();
    snapshot->queue_high_water             = g_era_split_communication_core.queue_high_water;
    snapshot->queue_result_level           = era_split_communication_core_result_level();
    snapshot->queue_result_high_water      = g_era_split_communication_core.queue_result_high_water;
    snapshot->queue_generation             = g_era_split_communication_core.queue_generation;
    snapshot->queue_flush_count            = g_era_split_communication_core.queue_flush_count;
    era_split_communication_core_owner_diagnostics_t owner;
    era_split_communication_core_owner_get_diagnostics_snapshot(&owner);
    snapshot->backend_owner_available              = 1;
    snapshot->backend_owner                        = owner.owner;
    snapshot->backend_owner_role                   = owner.backend_role;
    snapshot->backend_owner_revoke_pending         = owner.revoke_pending;
    snapshot->backend_owner_cancel_pending         = owner.cancel_pending;
    snapshot->backend_owner_reset_pending          = owner.reset_pending;
    snapshot->backend_owner_epoch                  = owner.owner_epoch;
    snapshot->backend_owner_released_epoch         = owner.released_epoch;
    snapshot->backend_owner_ready_epoch            = owner.ready_epoch;
    snapshot->backend_owner_transfer_count         = owner.transfer_count;
    snapshot->backend_owner_revoke_count           = owner.revoke_count;
    snapshot->backend_owner_release_count          = owner.release_count;
    snapshot->backend_owner_ready_count            = owner.ready_count;
    snapshot->backend_owner_transfer_timeout_count = owner.transfer_timeout_count;
    snapshot->backend_owner_reclaim_count          = owner.reclaim_count;
    snapshot->backend_owner_ready_timeout_count    = owner.ready_timeout_count;
    snapshot->backend_owner_init_fail_count        = owner.init_fail_count;
    snapshot->initiator_queue_expired_count        = g_era_split_communication_core.initiator_queue_expired_count;
    snapshot->initiator_owner_count                = g_era_split_communication_core.initiator_owner_count;
    snapshot->initiator_epoch_count                = g_era_split_communication_core.initiator_epoch_count;
    snapshot->initiator_cancel_count               = g_era_split_communication_core.initiator_cancel_count;
    snapshot->initiator_reset_count                = g_era_split_communication_core.initiator_reset_count;
    snapshot->initiator_pio_error_count            = g_era_split_communication_core.initiator_pio_error_count;
    snapshot->initiator_send_timeout_count         = g_era_split_communication_core.initiator_send_timeout_count;
    snapshot->initiator_response_timeout_count     = g_era_split_communication_core.initiator_response_timeout_count;
    snapshot->initiator_partial_frame_count        = g_era_split_communication_core.initiator_partial_frame_count;
    snapshot->initiator_io_count                   = g_era_split_communication_core.initiator_io_count;
    snapshot->initiator_decode_count               = g_era_split_communication_core.initiator_decode_count;
    snapshot->initiator_response_contract_count    = g_era_split_communication_core.initiator_response_contract_count;
    for (uint8_t lane = 0; lane < ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_COUNT; lane++) {
        snapshot->lane[lane].available              = lane != ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_INVALID ? 1 : 0;
        snapshot->lane[lane].pending                = g_era_split_communication_core.lane[lane].pending;
        snapshot->lane[lane].result_ready           = g_era_split_communication_core.lane[lane].result_ready;
        snapshot->lane[lane].generation             = g_era_split_communication_core.lane[lane].generation;
        snapshot->lane[lane].last_result_generation = g_era_split_communication_core.lane[lane].last_result_generation;
        snapshot->lane[lane].submit_count           = g_era_split_communication_core.lane[lane].submit_count;
        snapshot->lane[lane].accept_count           = g_era_split_communication_core.lane[lane].accept_count;
        snapshot->lane[lane].full_count             = g_era_split_communication_core.lane[lane].full_count;
        snapshot->lane[lane].start_fail_count       = g_era_split_communication_core.lane[lane].start_fail_count;
        snapshot->lane[lane].transaction_count      = g_era_split_communication_core.lane[lane].transaction_count;
        snapshot->lane[lane].result_count           = g_era_split_communication_core.lane[lane].result_count;
        snapshot->lane[lane].result_stale_count     = g_era_split_communication_core.lane[lane].result_stale_count;
        snapshot->lane[lane].result_full_count      = g_era_split_communication_core.lane[lane].result_full_count;
        snapshot->lane[lane].ok_count               = g_era_split_communication_core.lane[lane].ok_count;
        snapshot->lane[lane].miss_count             = g_era_split_communication_core.lane[lane].miss_count;
        snapshot->lane[lane].bad_count              = g_era_split_communication_core.lane[lane].bad_count;
        snapshot->lane[lane].fail_count             = g_era_split_communication_core.lane[lane].fail_count;
        snapshot->lane[lane].last_result            = g_era_split_communication_core.lane[lane].last_result;
        snapshot->lane[lane].last_request_sent      = g_era_split_communication_core.lane[lane].last_request_sent;
        snapshot->lane[lane].last_route_kind        = g_era_split_communication_core.lane[lane].last_route_kind;
        snapshot->lane[lane].last_route_reason      = g_era_split_communication_core.lane[lane].last_route_reason;
        snapshot->lane[lane].last_response_kind     = g_era_split_communication_core.lane[lane].last_response_kind;
    }
    snapshot->session_last_failure                  = g_era_split_communication_core.session_last_failure;
    snapshot->session_last_peer_status_valid        = g_era_split_communication_core.session_last_peer_status_valid;
    snapshot->source_push_last_response_payload_len  = g_era_split_communication_core.source_push_last_response_payload_len;
    snapshot->source_push_last_response_section_byte = g_era_split_communication_core.source_push_last_response_section_byte;
    snapshot->source_push_last_matrix_seq            = g_era_split_communication_core.source_push_last_matrix_seq;
    snapshot->source_push_last_lock_state_valid      = g_era_split_communication_core.source_push_last_lock_state_valid;
    snapshot->source_push_last_lock_state            = g_era_split_communication_core.source_push_last_lock_state;
    snapshot->source_push_last_visual_snapshot_valid = g_era_split_communication_core.source_push_last_visual_snapshot_valid;
    snapshot->source_push_last_rgb_state_valid       = g_era_split_communication_core.source_push_last_rgb_state_valid;
    snapshot->source_push_last_storage_news_valid    = g_era_split_communication_core.source_push_last_storage_news_valid;
    snapshot->source_push_last_storage_news          = g_era_split_communication_core.source_push_last_storage_news;
    snapshot->source_push_rx_wait_mode               = g_era_split_communication_core.source_push_rx_wait_mode;
    snapshot->source_push_hsrsp_count                = g_era_split_communication_core.source_push_hsrsp_count;
    snapshot->source_push_visual_snapshot_count      = g_era_split_communication_core.source_push_visual_snapshot_count;
    snapshot->source_push_rgb_state_count            = g_era_split_communication_core.source_push_rgb_state_count;
    snapshot->source_push_storage_news_count         = g_era_split_communication_core.source_push_storage_news_count;
    snapshot->source_push_response_section_or        = g_era_split_communication_core.source_push_response_section_or;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    snapshot->responder_available                  = 1;
    snapshot->responder_snapshot_valid             = g_era_split_communication_core.responder_snapshot_valid;
    snapshot->responder_snapshot_source            = g_era_split_communication_core.responder_snapshot_source;
    snapshot->responder_source_push_slot_reserved  = g_era_split_communication_core.responder_source_push_slot_reserved;
    snapshot->responder_source_push_result_ready   = g_era_split_communication_core.responder_source_push_result_ready;
    snapshot->responder_owner_gate_ready           = g_era_split_communication_core.responder_owner_gate_ready;
    snapshot->responder_owner_epoch                = g_era_split_communication_core.responder_owner_epoch;
    snapshot->responder_relation_generation        = g_era_split_communication_core.responder_relation_generation;
    snapshot->responder_snapshot_generation        = g_era_split_communication_core.responder_snapshot_generation;
    snapshot->responder_last_result_generation     = g_era_split_communication_core.responder_last_result_generation;
    snapshot->responder_snapshot_publish_count     = g_era_split_communication_core.responder_snapshot_publish_count;
    snapshot->responder_owner_gate_ready_count     = g_era_split_communication_core.responder_owner_gate_ready_count;
    snapshot->responder_owner_gate_block_count     = g_era_split_communication_core.responder_owner_gate_block_count;
    snapshot->responder_slot_reserve_count         = g_era_split_communication_core.responder_slot_reserve_count;
    snapshot->responder_slot_full_count            = g_era_split_communication_core.responder_slot_full_count;
    snapshot->responder_accept_count               = g_era_split_communication_core.responder_accept_count;
    snapshot->responder_accept_stale_count         = g_era_split_communication_core.responder_accept_stale_count;
    snapshot->responder_drain_count                = g_era_split_communication_core.responder_drain_count;
    snapshot->responder_noack_count                = g_era_split_communication_core.responder_noack_count;
    snapshot->responder_accepted_rx_count          = g_era_split_communication_core.responder_accepted_rx_count;
    snapshot->responder_undecodable_rx_count       = g_era_split_communication_core.responder_undecodable_rx_count;
    snapshot->responder_quiet_count                = g_era_split_communication_core.responder_quiet_count;
    snapshot->responder_coalesced_heartbeat_count  = g_era_split_communication_core.responder_coalesced_heartbeat_count;
    snapshot->responder_coalesced_runtime_section_count = g_era_split_communication_core.responder_coalesced_runtime_section_count;
    snapshot->responder_response_prepare_count     = g_era_split_communication_core.responder_response_prepare_count;
    snapshot->responder_response_prepare_fail_count = g_era_split_communication_core.responder_response_prepare_fail_count;
    snapshot->responder_last_matrix_seq            = g_era_split_communication_core.responder_last_matrix_seq;
    snapshot->responder_last_request_kind_flags    = g_era_split_communication_core.responder_last_request_kind_flags;
    snapshot->responder_last_response_section_mask = g_era_split_communication_core.responder_last_response_section_mask;
    snapshot->responder_last_response_payload_len  = g_era_split_communication_core.responder_last_response_payload_len;
    snapshot->responder_last_visual_reason         = g_era_split_communication_core.responder_last_visual_reason;
    snapshot->responder_anchor_hold_last_us        = g_era_split_communication_core.responder_anchor_hold_last_us;
    snapshot->responder_anchor_hold_max_us         = g_era_split_communication_core.responder_anchor_hold_max_us;
#endif
}
