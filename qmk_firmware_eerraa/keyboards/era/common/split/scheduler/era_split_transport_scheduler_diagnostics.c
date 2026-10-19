// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* No `#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE` inside this file, deliberately.
   Nothing else in the tree compiles it: it reaches SRC only from the ifeq in
   era_split_qmk_rules.mk that emits that same define, so an in-file guard is a
   tautology that reads as a real condition. Nine of them stood here and were
   deleted on 2026-08-18. The .mk is where a gated unit's status is legible
   (era_source_map.md), and the header's guards on these members are NOT of this
   shape -- those gate the struct for release-side includers. */
#include "../era_split_transport_scheduler.h"

#include <stdint.h>
#include <string.h>

#include "era_split_transport_scheduler_internal.h"
#include "../era_host_peer_matrix_link.h"
#include "../era_split_responder_projection.h"
#include "../era_split_scheduler_session.h"
#include "../era_split_transaction_engine.h"
#include "../diagnostics/era_split_transport_scheduler_role_diagnostics.h"

void era_split_transport_scheduler_get_diagnostics_snapshot(era_split_transport_scheduler_diagnostics_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    era_split_transaction_engine_diagnostics_t io;
    era_split_scheduler_session_diagnostics_t  session;
    era_split_responder_projection_t          responder;
    era_host_peer_matrix_link_diagnostics_t    host_peer;
    era_split_transaction_engine_get_diagnostics_snapshot(&io);
    era_split_scheduler_session_get_diagnostics_snapshot(&session);
    era_split_responder_projection_get(&responder);
    era_host_peer_matrix_link_get_diagnostics_snapshot(&host_peer);

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->scheduler_enabled                       = 1;
    snapshot->transaction_engine_diagnostics_fresh          = era_split_transaction_engine_diagnostics_snapshot_fresh() ? 1 : 0;
    snapshot->transaction_engine_diagnostics_fallback_count = era_split_transaction_engine_diagnostics_fallback_count();
    /* One assignment for the engine's whole record. It was thirty, spread
       across three places in this function, because the record was declared a
       second time in the snapshot header. */
    snapshot->io                                      = io;
    snapshot->relation                                = (uint8_t)g_era_split_transport_scheduler.mode;
    snapshot->previous_relation                       = (uint8_t)g_era_split_transport_scheduler.previous_mode;
    snapshot->owner_route_kind                        = (uint8_t)g_era_split_transport_scheduler.last_owner_route;
    snapshot->owner_route_reason                      = (uint8_t)g_era_split_transport_scheduler.last_owner_reason;
    snapshot->transport_step_call_count = g_era_split_transport_scheduler.transport_step_call_count;
    snapshot->scheduler_init_call_count                    = g_era_split_transport_scheduler.init_call_count;
    snapshot->scheduler_housekeeping_task_count            = g_era_split_transport_scheduler.housekeeping_task_count;
    snapshot->scheduler_plan_count                         = g_era_split_transport_scheduler.plan_count;
    snapshot->scheduler_dirty_flags                        = g_era_split_transport_scheduler.scheduler_dirty_flags;
    snapshot->scheduler_route_due_flags                    = g_era_split_transport_scheduler.route_due_flags;
    snapshot->communication_core_start_entry_ms = g_era_split_transport_scheduler.communication_core_start_entry_ms;
    snapshot->communication_core_start_exit_ms  = g_era_split_transport_scheduler.communication_core_start_exit_ms;
    snapshot->owner_step_count                             = g_era_split_transport_scheduler.owner_step_count;
    snapshot->responder_thread_started                     = responder.thread_started;
    snapshot->responder_service_enabled                    = responder.service_enabled;
    snapshot->responder_admission_blocked                  = responder.admission_blocked;
    snapshot->responder_reset_requested                    = responder.reset_requested;
    snapshot->responder_in_serial_io                       = responder.in_serial_io;
    snapshot->responder_in_transaction                     = responder.in_transaction;
    snapshot->responder_quiesced                           = responder.quiesced;
    snapshot->responder_frame_rx_count                     = responder.frame_rx_count;
    snapshot->responder_relation_request_rx_count          = responder.relation_request_rx_count;
    snapshot->responder_session_request_rx_count           = responder.session_request_rx_count;
    snapshot->responder_session_response_tx_count          = responder.session_response_tx_count;
    snapshot->responder_host_peer_heartbeat_rx_count       = responder.host_peer_heartbeat_rx_count;
    snapshot->responder_host_peer_source_push_rx_count     = responder.host_peer_source_push_rx_count;
    snapshot->responder_host_peer_visual_snapshot_tx_count = responder.host_peer_visual_snapshot_tx_count;
    snapshot->responder_host_peer_rgb_state_tx_count       = responder.host_peer_rgb_state_tx_count;
    snapshot->responder_ignored_frame_count                = responder.ignored_frame_count;
    snapshot->peer_session_known                           = session.peer_known;
    snapshot->peer_accepted_host_open                      = session.peer_accepted_host_open;
    snapshot->peer_accepted_no_host                        = session.peer_accepted_no_host;
    snapshot->peer_matrix_ready                            = session.peer_matrix_ready;
    snapshot->peer_bulk_page_supported                     = session.peer_bulk_page_supported;
    snapshot->peer_storage_news_observed             = g_era_split_transport_scheduler.peer_storage_news_observed;
    snapshot->peer_usb_epoch                               = session.peer_usb_epoch;
    snapshot->peer_host_open_generation                    = session.peer_host_open_generation;
    snapshot->peer_host_close_generation                   = session.peer_host_close_generation;
    snapshot->peer_session_rx_count                        = session.peer_session_rx_count;
    snapshot->peer_session_forget_count                    = session.peer_session_forget_count;
    snapshot->peer_session_stale_pending                   = g_era_split_transport_scheduler.peer_session_stale ? 1 : 0;
    snapshot->local_session_tx_count                       = session.local_session_tx_count;
    snapshot->host_peer_local_matrix_ready                 = host_peer.local_matrix_ready;
    snapshot->host_peer_source_push_forced                 = host_peer.local_source_push_forced;
    snapshot->host_peer_peer_cache_valid                   = host_peer.peer_cache_valid;
    snapshot->host_peer_local_current_seq8                 = host_peer.local_current_seq8;
    snapshot->host_peer_local_host_known_seq8              = host_peer.local_host_known_seq8;
    snapshot->host_peer_peer_matrix_seq8                   = host_peer.peer_matrix_seq8;
    snapshot->host_peer_source_push_tx_count               = host_peer.source_push_tx_count;
    snapshot->host_peer_source_push_ack_count              = host_peer.source_push_ack_count;
    snapshot->host_peer_ack_status_tx_count                = host_peer.ack_status_tx_count;
    snapshot->host_peer_visual_snapshot_rx_count           = g_era_split_transport_scheduler.host_peer_visual_snapshot_rx_count;
    snapshot->host_peer_rgb_state_rx_count                 = g_era_split_transport_scheduler.host_peer_rgb_state_rx_count;
    snapshot->host_peer_authority_rx_count                 = g_era_split_transport_scheduler.host_peer_authority_rx_count;
    snapshot->host_peer_input_layer_apply_count            = g_era_split_transport_scheduler.host_peer_input_layer_apply_count;
    snapshot->host_peer_peer_cache_update_count            = host_peer.peer_cache_update_count;
    snapshot->host_peer_peer_cache_project_count           = host_peer.peer_cache_project_count;
    snapshot->host_peer_peer_cache_flush_count             = host_peer.peer_cache_flush_count;
    era_split_transport_scheduler_role_diagnostics_write_snapshot(snapshot);
}

void era_split_transport_scheduler_reset_diagnostics_era_baselines(void) {
    era_split_transport_scheduler_role_diagnostics_reset_baselines(g_era_split_transport_scheduler.mode);
}
