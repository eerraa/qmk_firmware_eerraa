// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t  thread_started;
    uint8_t  service_enabled;
    uint8_t  admission_blocked;
    uint8_t  reset_requested;
    uint8_t  in_serial_io;
    uint8_t  in_transaction;
    uint8_t  quiesced;
    uint32_t frame_rx_count;
    uint32_t relation_request_rx_count;
    uint32_t session_request_rx_count;
    uint32_t session_response_tx_count;
    uint32_t host_peer_heartbeat_rx_count;
    uint32_t host_peer_source_push_rx_count;
    uint32_t host_peer_visual_snapshot_tx_count;
    uint32_t host_peer_rgb_state_tx_count;
    uint32_t ignored_frame_count;
} era_split_responder_projection_t;

void era_split_responder_projection_note_result(bool session,
                                                        bool source_push,
                                                        bool response_sent,
                                                        uint8_t response_section_mask,
                                                        uint8_t transaction_result);
/* Bulk-fold for requests core1 answered with a bare ACK and did not publish a
   result for (era_split_communication_core_responder_service.c). They are real
   received frames and real sent ACKs, so the projection must count them -- but
   counting them one at a time is exactly the per-exchange core0 wake that
   skipping the publish removed, so they arrive as a delta at whatever cadence
   core0 happens to run. */
void era_split_responder_projection_note_quiet(uint32_t count);
void era_split_responder_projection_get(era_split_responder_projection_t *snapshot);
