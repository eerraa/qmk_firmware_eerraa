// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

bool era_split_qwin_diagnostics_process_record(uint16_t keycode, keyrecord_t *record);
/* Once per millisecond from the split housekeeping's paced block: the settle
   and the segment slices are timed here so the scan path carries nothing. */
void era_split_qwin_diagnostics_tick_1ms(void);
