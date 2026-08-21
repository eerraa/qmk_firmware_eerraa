// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_matrix_frame.h"


bool era_split_wire_matrix_reserved_bits_clear(const uint8_t packed[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES]) {
    if (packed == NULL) {
        return false;
    }

    uint8_t used_bits = ERA_SPLIT_WIRE_HALF_MATRIX_BITS & 0x07;
    if (used_bits == 0) {
        return true;
    }

    uint8_t final_mask = (uint8_t)((1U << used_bits) - 1U);
    return (packed[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES - 1] & (uint8_t)~final_mask) == 0;
}

static void era_split_wire_clear_matrix_reserved_bits(uint8_t packed[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES]) {
    uint8_t used_bits = ERA_SPLIT_WIRE_HALF_MATRIX_BITS & 0x07;
    if (used_bits != 0) {
        uint8_t final_mask = (uint8_t)((1U << used_bits) - 1U);
        packed[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES - 1] &= final_mask;
    }
}

bool era_split_wire_pack_matrix(const matrix_row_t rows[MATRIX_ROWS_PER_HAND], uint8_t packed[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES]) {
    if (rows == NULL || packed == NULL) {
        return false;
    }

    volatile uint8_t *vpacked = packed;
    for (uint8_t index = 0; index < ERA_SPLIT_WIRE_HALF_MATRIX_BYTES; index++) {
        vpacked[index] = 0;
    }
    for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; row++) {
        matrix_row_t row_value = rows[row];
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            if ((row_value & ((matrix_row_t)1 << col)) != 0) {
                uint8_t bit = (uint8_t)(row * MATRIX_COLS + col);
                packed[bit >> 3] |= (uint8_t)(1U << (bit & 0x07));
            }
        }
    }
    era_split_wire_clear_matrix_reserved_bits(packed);
    return true;
}

bool era_split_wire_unpack_matrix(const uint8_t packed[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES], matrix_row_t rows[MATRIX_ROWS_PER_HAND]) {
    if (packed == NULL || rows == NULL || !era_split_wire_matrix_reserved_bits_clear(packed)) {
        return false;
    }

    volatile matrix_row_t *vrows = rows;
    for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; row++) {
        vrows[row] = 0;
    }
    uint8_t bit = 0;
    for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            if ((packed[bit >> 3] & (uint8_t)(1U << (bit & 0x07))) != 0) {
                rows[row] |= (matrix_row_t)1 << col;
            }
            bit++;
        }
    }
    return true;
}
