// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

/* Cause-variant-only timing record for one WIRE_DIAG interval. The public
 * record contains values only; the request/response state that produces them
 * stays private to the diagnostic unit. */
#define ERA_VIA_MACRO_DIAGNOSTICS_RECORD_BYTES 56U

typedef struct {
    uint32_t receive_count;
    uint32_t response_count;
    uint32_t drain_count;
    uint32_t handler_total_ms;
    uint32_t send_total_ms;
    uint32_t drain_total_ms;
    uint32_t application_total_ms;
    uint16_t first_receive_elapsed_ms;
    uint16_t last_receive_elapsed_ms;
    uint16_t receive_gap_last_ms;
    uint16_t receive_gap_max_ms;
    uint16_t receive_gap_over_quiet_count;
    uint16_t handler_max_ms;
    uint16_t send_max_ms;
    uint16_t drain_max_ms;
    uint16_t application_max_ms;
    uint16_t response_overlap_count;
    uint16_t intervening_raw_count;
    uint8_t  response_pending;
    uint8_t  request_open;
    uint8_t  completion_valid;
    uint8_t  reserved;
} era_via_macro_diagnostics_t;

_Static_assert(sizeof(era_via_macro_diagnostics_t) == ERA_VIA_MACRO_DIAGNOSTICS_RECORD_BYTES,
               "ERA VIA macro diagnostic record budget changed.");

/* Called by the cause-gated seams in quantum/via.c. */
void era_via_macro_diagnostics_receive(uint8_t command_id);
void era_via_macro_diagnostics_response_begin(void);
void era_via_macro_diagnostics_response_end(void);

/* Called after raw_hid_task() from the split housekeeping path. */
void era_via_macro_diagnostics_task(void);

void era_via_macro_diagnostics_get_snapshot(era_via_macro_diagnostics_t *snapshot);
void era_via_macro_diagnostics_reset(void);
