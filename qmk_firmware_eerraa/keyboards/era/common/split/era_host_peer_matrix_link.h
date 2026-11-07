// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_split_wire_protocol.h"

bool era_host_peer_matrix_link_capture_source_push(uint8_t packed_rows[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES], uint8_t *matrix_seq);
void era_host_peer_matrix_link_note_source_push_accepted(uint8_t matrix_seq);
bool era_host_peer_matrix_link_accept_source_push_packed(const uint8_t packed_rows[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES]);
