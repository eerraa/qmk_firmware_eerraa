// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_responder_projection.h"

#include <string.h>

#include "atomic_util.h"
#include "era_split_transaction_types.h"
#include "era_split_wire_payload.h"

typedef struct {
    bool     initialized;
    uint32_t frame_rx_count;
    uint32_t relation_request_rx_count;
    uint32_t session_request_rx_count;
    uint32_t session_response_tx_count;
    uint32_t host_peer_heartbeat_rx_count;
    uint32_t host_peer_source_push_rx_count;
    uint32_t host_peer_visual_snapshot_tx_count;
    uint32_t host_peer_rgb_state_tx_count;
    uint32_t ignored_frame_count;
} era_split_responder_state_t;

static era_split_responder_state_t g_era_split_responder;

static void era_split_responder_projection_init(void) {
    ATOMIC_BLOCK_RESTORESTATE {
        if (!g_era_split_responder.initialized) {
            memset(&g_era_split_responder, 0, sizeof(g_era_split_responder));
            g_era_split_responder.initialized = true;
        }
    }
}

void era_split_responder_projection_note_quiet(uint32_t count) {
    if (count == 0) {
        return;
    }
    era_split_responder_projection_init();
    /* The same four fields the per-result path moves for a bare ACK, and only
       those: a quiet response is by definition a received relation request
       answered with an ACK that carried no section, so the visual and RGB
       section counters cannot have moved and the result cannot have been BAD.
       Anything else is published individually and never reaches here. */
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_split_responder.frame_rx_count += count;
        g_era_split_responder.relation_request_rx_count += count;
        g_era_split_responder.host_peer_heartbeat_rx_count += count;
    }
}

void era_split_responder_projection_note_result(bool session,
                                                        bool source_push,
                                                        bool response_sent,
                                                        uint8_t response_section_mask,
                                                        uint8_t transaction_result) {
    era_split_responder_projection_init();
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_split_responder.frame_rx_count++;
        g_era_split_responder.relation_request_rx_count++;
        if (session) {
            g_era_split_responder.session_request_rx_count++;
            if (response_sent) {
                g_era_split_responder.session_response_tx_count++;
            }
        } else {
            if (source_push) {
                g_era_split_responder.host_peer_source_push_rx_count++;
            } else {
                g_era_split_responder.host_peer_heartbeat_rx_count++;
            }
            if (response_sent) {
                if ((response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC) != 0) {
                    g_era_split_responder.host_peer_visual_snapshot_tx_count++;
                }
                if ((response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE) != 0) {
                    g_era_split_responder.host_peer_rgb_state_tx_count++;
                }
            }
        }
        if (transaction_result == ERA_SPLIT_TRANSACTION_RESULT_BAD) {
            g_era_split_responder.ignored_frame_count++;
        }
    }
}

void era_split_responder_projection_get(era_split_responder_projection_t *snapshot) {
    era_split_responder_projection_init();
    if (snapshot == NULL) {
        return;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->admission_blocked                  = 1;
        snapshot->quiesced                           = 1;
        snapshot->frame_rx_count                     = g_era_split_responder.frame_rx_count;
        snapshot->relation_request_rx_count          = g_era_split_responder.relation_request_rx_count;
        snapshot->session_request_rx_count           = g_era_split_responder.session_request_rx_count;
        snapshot->session_response_tx_count          = g_era_split_responder.session_response_tx_count;
        snapshot->host_peer_heartbeat_rx_count       = g_era_split_responder.host_peer_heartbeat_rx_count;
        snapshot->host_peer_source_push_rx_count     = g_era_split_responder.host_peer_source_push_rx_count;
        snapshot->host_peer_visual_snapshot_tx_count = g_era_split_responder.host_peer_visual_snapshot_tx_count;
        snapshot->host_peer_rgb_state_tx_count       = g_era_split_responder.host_peer_rgb_state_tx_count;
        snapshot->ignored_frame_count                = g_era_split_responder.ignored_frame_count;
    }
}
