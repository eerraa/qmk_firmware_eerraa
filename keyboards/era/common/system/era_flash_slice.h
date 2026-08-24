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
   to skip the transport step (`era_rp2040_matrix_core.c`), and the reset-action
   interceptor below reads it to keep a controlled reset from abandoning the
   outer erase before its cache is durable. */
bool era_flash_slice_in_yield(void);

/* Called after the keyboard and user process-record hooks but before QMK's
   `process_quantum()`. A reset-class key press reached from the sliced erase's
   nested matrix pass is consumed and latched; every other record continues.
   The first reset press wins because without the defer it would have reset the
   MCU before a second press could exist. */
bool era_flash_slice_defer_reset_action(uint16_t keycode, bool pressed);

/* Drain that one action from the ERA class skeleton's top-level housekeeping
   hook. That hook is reached only after the main loop's current keyboard,
   protocol, raw-HID and deferred-exec calls -- including the outer EEPROM
   operation whose gap latched the action -- have returned. */
void era_flash_slice_deferred_reset_task(void);

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
static inline bool era_flash_slice_defer_reset_action(uint16_t keycode, bool pressed) {
    (void)keycode;
    (void)pressed;
    return true;
}
static inline void era_flash_slice_deferred_reset_task(void) {}
static inline void era_flash_slice_note_window_begin(void) {}
static inline void era_flash_slice_note_window_end(void) {}

#endif
