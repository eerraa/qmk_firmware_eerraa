// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t relation_request_rx_count;
} era_split_responder_projection_t;

/* Core0's liveness observation includes individual results and delayed folds
 * of quiet/coalesced requests, without adding a per-exchange wake. */
void era_split_responder_projection_note_received(uint32_t count);
void era_split_responder_projection_get(era_split_responder_projection_t *snapshot);
