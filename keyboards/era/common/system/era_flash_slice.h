// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The keyboard pass that runs between two sectors of a sliced backing-store
   erase, and the instrument that says whether it ran.

   Slicing the erase is worth nothing on its own: twelve sector erases in a
   tight loop block core0 for the same ~390 ms one whole-store erase does. What
   bounds the outage is this unit running a real scan-and-process pass in each
   gap, which is why the counters below are a pair - `slices` says the driver
   decomposed, `scans` says the gap was used. A build where the first rises and
   the second does not has the decomposition and none of the fix.

   Gated on residency because that is the condition under which the yield is
   reachable at all: an XIP image holds the interrupt mask across the commit
   window (`wear_leveling_rp2040_flash.c`), and a keyboard pass with interrupts
   masked is not one. One include moves a board across both. */

#if defined(ERA_SRAM_RESIDENT_IMAGE)

/* Set once the keyboard loop is live. Until then a yield does nothing: the
   boot-time consolidation inside wear_leveling_init() runs before
   matrix_init(), and a scan there would read pins nobody has configured. */
void era_flash_slice_arm(void);

/* True only while control is inside the pass below. The matrix engine reads it
   to skip the transport step (`era_rp2040_matrix_core.c`); nothing else may
   depend on it. */
bool era_flash_slice_in_yield(void);

/* The core0 flash window, opened and closed by whoever owns the EEPROM commit
   hooks. Distinct from that hook's own `max_ms`, which measures the whole
   operation: this measures the longest span inside it that core0 could not
   leave. Before this slice the two were the same number. */
void era_flash_slice_note_window_begin(void);
void era_flash_slice_note_window_end(void);

/* `cross` counts backing-store operations refused because a gap was open. It
   is structurally zero: the wear-leveling interlock takes every reachable
   writer before it gets here. Nonzero means the gap's contract was crossed by
   a path nobody enumerated, which is the failure the rule alone could not
   catch. */
void era_flash_slice_get_diagnostics(uint16_t *stall_max_ms, uint32_t *slice_count, uint32_t *scan_count, uint32_t *cross_count);

#else

static inline void era_flash_slice_arm(void) {}
static inline bool era_flash_slice_in_yield(void) {
    return false;
}
static inline void era_flash_slice_note_window_begin(void) {}
static inline void era_flash_slice_note_window_end(void) {}

#endif
