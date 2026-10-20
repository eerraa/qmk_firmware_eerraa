// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
void era_sync_timing_write_begin(uint32_t bytes);
void era_sync_timing_write_end(uint32_t elapsed_ms);
void era_sync_timing_commit(uint32_t bytes, bool remote);
void era_sync_timing_apply_end(uint32_t elapsed_ms);
void era_sync_timing_pending(bool pending);
/* First abort: packed state/operation/status/path, emitted only after F24.
 * Paths: 1 peer reset, 2 host close, 3 active host identity replacement,
 * 4 peer phase expiry, 5 host phase expiry. */
void era_sync_timing_abort(uint8_t state, uint8_t operation, uint8_t status, uint8_t path);
