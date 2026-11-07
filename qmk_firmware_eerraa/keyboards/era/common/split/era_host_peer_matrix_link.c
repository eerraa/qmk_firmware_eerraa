// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_host_peer_matrix_link.h"

#include <stddef.h>

#include "../system/era_matrix_engine.h"
#include "era_split_matrix_frame.h"

#if !defined(ERA_RP2040_MATRIX_ENABLE)
#    error "era_host_peer_matrix_link.c requires the ERA RP2040 matrix engine."
#endif

bool era_host_peer_matrix_link_capture_source_push(uint8_t packed_rows[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES], uint8_t *matrix_seq) {
    if (packed_rows == NULL || matrix_seq == NULL) {
        return false;
    }

    matrix_row_t rows[MATRIX_ROWS_PER_HAND];
    uint8_t      seq = 0;
    if (!era_matrix_engine_copy_source_push_rows(rows, &seq) || !era_split_wire_pack_matrix(rows, packed_rows)) {
        return false;
    }

    *matrix_seq = seq;
    return true;
}

void era_host_peer_matrix_link_note_source_push_accepted(uint8_t matrix_seq) {
    era_matrix_engine_note_source_push_accepted(matrix_seq);
}

bool era_host_peer_matrix_link_accept_source_push_packed(const uint8_t packed_rows[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES]) {
    matrix_row_t rows[MATRIX_ROWS_PER_HAND];
    if (packed_rows == NULL || !era_split_wire_unpack_matrix(packed_rows, rows)) {
        return false;
    }
    return era_matrix_engine_accept_peer_snapshot(rows);
}
