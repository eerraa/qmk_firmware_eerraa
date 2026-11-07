// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_responder_projection.h"

#include <string.h>

#include "atomic_util.h"

typedef struct {
    bool     initialized;
    uint32_t relation_request_rx_count;
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

void era_split_responder_projection_note_received(uint32_t count) {
    if (count == 0) {
        return;
    }
    era_split_responder_projection_init();
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_split_responder.relation_request_rx_count += count;
    }
}

void era_split_responder_projection_get(era_split_responder_projection_t *snapshot) {
    era_split_responder_projection_init();
    if (snapshot == NULL) {
        return;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        snapshot->relation_request_rx_count = g_era_split_responder.relation_request_rx_count;
    }
}
