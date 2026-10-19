// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_host_peer_source_snapshot.h"

#include QMK_KEYBOARD_H

#include "../system/era_matrix_engine.h"
#include "era_host_peer_transaction.h"
#include "era_split_keyboard.h" /* the render gate's owner, for the resolved sleep */
#include "era_split_matrix_frame.h"
#if defined(RGB_MATRIX_ENABLE)
#    include "rgb_matrix.h"
#endif

bool era_host_peer_source_snapshot_publish_visual(void) {
    matrix_row_t rows[MATRIX_ROWS_PER_HAND];
    if (!era_matrix_engine_copy_local_rows(rows)) {
        return false;
    }

    uint8_t baseline[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES];
    if (!era_split_wire_pack_matrix(rows, baseline)) {
        return false;
    }

    return era_host_peer_transaction_publish_responder_visual_snapshot(baseline);
}

bool era_host_peer_source_snapshot_capture_rgb_state(era_host_peer_rgb_state_t *state, bool include_sleep) {
    if (state == NULL) {
        return false;
    }

    rgb_config_t config = rgb_matrix_config;
    state->enabled      = config.enable != 0;
    /* Zero at capture in DUAL-HOST (Slice 12): the sleep bit is a fact about
       the one USB session a HOST-PEER pair shares, and DUAL-HOST halves each
       own one. Captured as zero rather than clipped later so a suspend flip
       publishes an unchanged snapshot and re-arms nothing.

       Read from the sleep decision's owner and not from the render gate. The
       gate is transiently punched by a board's status report -- the core1
       launch-failure frame forces light for 3.68 s -- and publishing that to a
       PEER would make one half's status LED flash the other half's lighting.
       What the PEER needs is the session fact, which is what the owner holds. */
    state->sleep        = include_sleep && era_split_keyboard_lighting_sleep_state();
    state->mode         = config.mode & 0x3F;
    state->hue          = config.hsv.h;
    state->sat          = config.hsv.s;
    state->val          = config.hsv.v;
    state->speed        = config.speed;
    state->flags        = config.flags;
    return true;
}

bool era_host_peer_source_snapshot_publish_rgb_state(bool include_sleep) {
    era_host_peer_rgb_state_t state;
    if (!era_host_peer_source_snapshot_capture_rgb_state(&state, include_sleep)) {
        return false;
    }

    return era_host_peer_transaction_publish_responder_rgb_state(&state);
}
