// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "era_split_wire_protocol.h"

bool era_split_wire_matrix_reserved_bits_clear(const uint8_t packed[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES]);
bool era_split_wire_pack_matrix(const matrix_row_t rows[MATRIX_ROWS_PER_HAND], uint8_t packed[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES]);
bool era_split_wire_unpack_matrix(const uint8_t packed[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES], matrix_row_t rows[MATRIX_ROWS_PER_HAND]);
