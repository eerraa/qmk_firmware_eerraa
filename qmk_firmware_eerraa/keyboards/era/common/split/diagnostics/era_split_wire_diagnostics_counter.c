// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_wire_diagnostics.h"


uint32_t era_split_wire_diagnostics_raw_matrix_scan_count;

#ifdef MATRIX_SCAN_COUNT_DIAGNOSTICS_ENABLE
void matrix_scan_count_diagnostics_kb(void) {
    era_split_wire_diagnostics_raw_matrix_scan_count++;
}
#endif
