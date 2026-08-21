// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

#include "../era_split_transport_scheduler_diagnostics.h"

typedef struct {
    uint32_t print_count;
    uint32_t last_print_ms;
    uint32_t raw_matrix_scan_count;
    uint32_t raw_matrix_scan_hz;
    uint32_t raw_matrix_read_us_total;
    uint32_t raw_matrix_read_us_max;
    uint32_t host_peer_source_push_hz;
    uint32_t host_peer_ack_status_hz;
    era_split_transport_scheduler_diagnostics_snapshot_t scheduler;
} era_split_wire_diagnostics_snapshot_t;

extern uint32_t era_split_wire_diagnostics_raw_matrix_scan_count;

void era_split_wire_diagnostics_task(void);
bool era_split_wire_diagnostics_process_record(uint16_t keycode, keyrecord_t *record);

/* R3.1's write-burst bracket: called from the eeprom_driver_write_begin_kb /
   write_end_kb overrides in era_split_transport_scheduler.c, which declares
   these locally rather than including this header. */
void era_split_wire_diagnostics_note_write_block_begin(void);
void era_split_wire_diagnostics_note_write_block_end(void);
