// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

extern uint32_t era_split_wire_diagnostics_raw_matrix_scan_count;

void era_split_wire_diagnostics_task(void);
bool era_split_wire_diagnostics_process_record(uint16_t keycode, keyrecord_t *record);
