// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_flash_slice.h"

#if defined(ERA_SRAM_RESIDENT_IMAGE)

#    include "keyboard.h"
#    include "timer.h"
#    include "wear_leveling.h"

/* What runs in the gap, and why it is the whole pass rather than the scan.

   A scan alone bounds `scan_hz` and fixes nothing a user can feel. QMK turns a
   scan into a keystroke in matrix_task(), by differencing the debounced matrix
   against `matrix_previous` - and that comparison is what a blocked core0 is
   not doing. Sampling the matrix twelve times during an erase while nobody
   differences it leaves a press-and-release inside the window exactly as lost
   as it is today, because both samples are consumed by one comparison after
   the window closes. So the gap runs matrix_task(), and the outage this slice
   bounds is the outage a typist has.

   What the gap must not run is the wire. matrix_scan() reaches
   matrix_post_scan(), which is the split transport scheduler - route planning,
   result apply, and the storage lane whose durable apply is very often the
   flash operation we are standing inside. That step is skipped here, and it is
   skipped rather than made reentrant because since Slice 11.7 and R2 the
   relation's liveness is core1's standing exchange in both serviced relations.
   Core0 missing its transport step for 400 ms is the case that architecture was
   built for; re-entering the storage state machine from inside its own EEPROM
   write is not a case at all.

   Two omissions are deliberate and neither is free. `last_matrix_activity_
   trigger()` is a keyboard.c internal with no declaration anywhere, so a key
   pressed in the gap does not refresh the RGB/OLED idle timers until the next
   ordinary pass a few milliseconds later. And a keycode that resets the MCU
   (QK_BOOT, QK_REBOOT) is now reachable mid-erase, which widens the existing
   power-loss window wear_leveling_consolidate_force() already documents; the
   store recovers through its own checksum-mismatch path, losing the log, as it
   does for a power cut in the same place. */

static struct {
    bool     armed;
    bool     in_yield;
    uint8_t  window_depth;
    uint16_t window_open_ms;
    uint16_t stall_max_ms;
    uint32_t slice_count;
    uint32_t scan_count;
    uint32_t cross_count;
} g_era_flash_slice;

/* The gap's contract, held by a check rather than by the rule that states it.
   Two guards hold the reachable paths - the wear-leveling interlock for EEPROM
   writes and the transport skip for the wire - and both are real code. What was
   held by convention alone is the third part: that nothing else `action_exec`
   can reach from inside a gap arrives at the backing store. That set grows
   silently as the action layer grows, and this session already watched a
   convention-held boundary produce a defect nobody's check caught.

   So the backing store asks before it commits. Structurally zero, published
   rather than asserted, because a check nobody can read has become a rule
   again. */
bool backing_store_commit_blocked_kb(void) {
    if (!g_era_flash_slice.in_yield) {
        return false;
    }
    g_era_flash_slice.cross_count++;
    return true;
}

void era_flash_slice_arm(void) {
    if (g_era_flash_slice.armed) {
        return;
    }
    g_era_flash_slice.armed = true;

    /* Everything before this instant is boot, and boot is a different regime
       rather than a worse one: keyboard_init() runs wear_leveling_init()'s
       consolidation and eeconfig_init_quantum()'s CLEAN erase with no loop to
       yield to and no keyboard to keep responsive. Their width is the Boot Path
       Gate's business and is read from `max_ms`, which this slice does not
       move. `stall_ms` is about the outage a typist has, so its window starts
       here - otherwise one CLEAN boot pins it at ~380 ms and it can never
       report anything again.

       `slices` and `scans` are deliberately not reset. They stay cumulative so
       the boot's unused gaps remain visible, which is the honest record and is
       why both are read as a delta across the operation under test. */
    g_era_flash_slice.stall_max_ms = 0;
}

bool era_flash_slice_in_yield(void) {
    return g_era_flash_slice.in_yield;
}

/* The span, and the operation that owns it, are two different brackets.

   A span is one stretch core0 could not leave, and the yield closes and reopens
   one at every gap it uses. The operation is the whole EEPROM commit, and only
   its outermost begin/end may start and finish the measurement - a commit
   reached from inside a gap is the caller's own work, not a second operation,
   and must not truncate the enclosing one. That is why the public pair counts
   depth and the yield uses the span helpers directly.

   Zero is the closed flag as well as a timestamp, so a real zero reading is
   stamped 1 - the same trade the scheduler's own edge recorder makes, for the
   same reason and at the same cost of at most 1 ms per 65 s wrap. */
static void era_flash_slice_open_span(void) {
    if (g_era_flash_slice.window_open_ms != 0) {
        return;
    }
    uint16_t now_ms                  = (uint16_t)timer_read32();
    g_era_flash_slice.window_open_ms = now_ms == 0 ? 1 : now_ms;
}

static void era_flash_slice_close_span(void) {
    uint16_t open_ms = g_era_flash_slice.window_open_ms;
    if (open_ms == 0) {
        return;
    }
    uint16_t elapsed_ms = (uint16_t)((uint16_t)timer_read32() - open_ms);
    if (elapsed_ms > g_era_flash_slice.stall_max_ms) {
        g_era_flash_slice.stall_max_ms = elapsed_ms;
    }
    g_era_flash_slice.window_open_ms = 0;
}

void era_flash_slice_note_window_begin(void) {
    g_era_flash_slice.window_depth++;
    if (g_era_flash_slice.window_depth != 1) {
        return;
    }
    era_flash_slice_open_span();
}

void era_flash_slice_note_window_end(void) {
    if (g_era_flash_slice.window_depth == 0) {
        return;
    }
    g_era_flash_slice.window_depth--;
    if (g_era_flash_slice.window_depth != 0) {
        return;
    }
    era_flash_slice_close_span();
}

void era_flash_slice_get_diagnostics(uint16_t *stall_max_ms, uint32_t *slice_count, uint32_t *scan_count, uint32_t *cross_count) {
    if (stall_max_ms != NULL) {
        *stall_max_ms = g_era_flash_slice.stall_max_ms;
    }
    if (slice_count != NULL) {
        *slice_count = g_era_flash_slice.slice_count;
    }
    if (scan_count != NULL) {
        *scan_count = g_era_flash_slice.scan_count;
    }
    if (cross_count != NULL) {
        *cross_count = g_era_flash_slice.cross_count;
    }
}

/* The strong override of the wear-leveling layer's weak yield hook.

   The window is split only when the pass actually runs, and that is the whole
   honesty of `stall_ms`. Splitting it unconditionally would make the number
   fall to one sector's width on a build whose gaps do nothing, which is the
   shape of every metric this project has improved without the thing it names.
   A refused yield therefore leaves the span accumulating and reports the real
   blocked width. */
void backing_store_erase_yield_kb(void) {
    g_era_flash_slice.slice_count++;

    if (!g_era_flash_slice.armed || g_era_flash_slice.in_yield) {
        return;
    }

    g_era_flash_slice.in_yield = true;
    era_flash_slice_close_span();

    (void)matrix_task();

    g_era_flash_slice.scan_count++;
    era_flash_slice_open_span();
    g_era_flash_slice.in_yield = false;
}

#endif
