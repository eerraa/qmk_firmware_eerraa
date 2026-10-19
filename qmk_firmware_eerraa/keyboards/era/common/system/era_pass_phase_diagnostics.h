// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

/* The keyboard pass, itemised.
 *
 * Performance batch 1 left one number nobody has ever split up. On the no-cable
 * LEFT half the pass is about 24.7 us and the matrix sampler's consumer is
 * 3.2 us of it; the other ~21.6 us has no owner, and it read the same on the
 * PIO engine and the CPU one (21.56 against 21.65), so it is not the matrix.
 * This instrument charges every microsecond of the pass to one of twelve named
 * segments so that the next optimisation is aimed rather than guessed.
 *
 * Each id names the span that ENDS at its mark, and the twelve are contiguous:
 * they tile one pass end to end, so `sum(us) / passes` must come out at the
 * pass period `scan_hz` reports. That identity is the instrument's own
 * self-check and the first thing a reading is held to.
 *
 * What it does NOT do, deliberately: convert time. Every stamp is one raw read
 * of the RP2040 microsecond counter, because platform timer conversions on the
 * scan path are what the 2026-08-15 scan-rate regression was made of
 * (era_qmk_fork_ledger.md, platforms/chibios/timer.c). One microsecond of
 * resolution is coarse against a sub-microsecond segment and does not need to
 * be finer: the accumulator sums the truncated differences over millions of
 * passes while the segment boundaries drift across the microsecond, so the mean
 * converges on the true one. A single-pass figure from this instrument is
 * meaningless; a window figure is not.
 *
 * It is its own selector rather than a rider on the diagnostics ones, and that
 * is the whole reason the `qwin_phase` rung exists. Twelve counter reads per
 * pass cost scan rate, and folding them into `qwin` would move the comparison
 * point every reading is judged against. The cost is measured rather than
 * argued: scan_hz(qwin) - scan_hz(qwin_phase) in one sitting IS the
 * instrument's price, and every segment figure is read net of it. Measured
 * 2026-08-16 at 4.13 us a pass, about 0.34 us in each segment, against an
 * arithmetic estimate of 4.0 -- and it must be re-measured whenever a mark is
 * added, which the maxima below did.
 */

#define ERA_PASS_PHASE_RAW 0U    /* era_rp2040_matrix_update_raw_rows(): the frame fetch and decode */
#define ERA_PASS_PHASE_DEB 1U    /* the rest of era_matrix_engine_scan_local(): scan hooks, debounce, local publish */
#define ERA_PASS_PHASE_XPORT 2U  /* era_split_transport_scheduler_transport_step() */
#define ERA_PASS_PHASE_SCANHK 3U /* the rest of matrix_scan(): matrix_scan_kb(), the composed-input edge */
#define ERA_PASS_PHASE_DIFF 4U   /* matrix_task()'s difference loop against matrix_previous[], plus matrix_scan_perf_task() */
#define ERA_PASS_PHASE_ACT 5U    /* the rest of matrix_task(): the tick event, or the changed-row walk into action_exec() */
#define ERA_PASS_PHASE_QTM 6U    /* quantum_task() */
#define ERA_PASS_PHASE_RGB 7U    /* rgb_matrix_task() */
#define ERA_PASS_PHASE_KTAIL 8U  /* the rest of keyboard_task(): mousekey_task(), led_task() */
#define ERA_PASS_PHASE_LOOP 9U   /* protocol_post_task(), raw_hid_task(), console_task(), deferred_exec_task(), housekeeping down to the split skeleton */
#define ERA_PASS_PHASE_HK 10U    /* era_split_keyboard_task() */
#define ERA_PASS_PHASE_REST 11U  /* from the end of era_split_keyboard_task() back to the top of the next matrix_scan() */
#define ERA_PASS_PHASE_COUNT 12U

/* The accumulators answer "what does the pass cost on average". The maxima
 * answer the question the accumulators structurally cannot: **how big is the
 * worst pass, and which segment made it**.
 *
 * That is the whole decision between leaving presentation on core0 and moving
 * it to core1. Rendering is not a per-pass cost, it is about eight working
 * passes per 16 ms frame among six hundred that do nothing — so an average hides
 * it and a maximum does not. A keyboard's felt latency is its worst pass, not
 * its mean one, and no instrument in this tree has ever reported one.
 *
 * One microsecond of resolution makes a sub-microsecond segment's maximum
 * meaningless (it reads 1 or 0). It is exactly right for the ones this is for,
 * which are the tens-of-microseconds spikes. Read a maximum only where the mean
 * says there is something to see. */
/* A maximum says how bad, and nothing at all about how often. A 254 us pass
 * once in eighty seconds is noise; the same pass a thousand times a second is
 * the design. The bands close that gap for the cost of three compares on a
 * value the wrap already holds. */
#define ERA_PASS_PHASE_BAND_COUNT 3U

/* `HK` is the largest segment and the one whose maximum moved most between
 * relations, and its mean cannot say which of its four parts holds that. These
 * four tile it: the common feature tasks, the split scheduler task, the
 * once-per-millisecond gate body, and -- derived by the reader as HK minus the
 * three -- the wire diagnostics task and the plumbing.
 *
 * They run on a cursor of their own so the twelve top-level segments keep
 * tiling: a sub-mark must not advance the main cursor or `us[HK]` would hold
 * only its tail. Seeding costs no counter read, because the LOOP mark that
 * opens HK has just taken one. */
#define ERA_PASS_PHASE_HK_FEATURES 0U
#define ERA_PASS_PHASE_HK_SCHED 1U
#define ERA_PASS_PHASE_HK_TICK 2U
#define ERA_PASS_PHASE_HK_COUNT 3U

/* `RGB` split the same way, and for a question with a decision behind it: a
 * render chunk's cost is what bounds how long moving the render to core1 may
 * delay a wire exchange, and in HOST-PEER that exchange carries half the
 * keyboard's keys. `mx[RGB]` reports 90 us and cannot say whether that is a
 * render chunk, the flush's ninety-six-word conversion, or the frame start.
 *
 * The five tile RGB: the timer head, then whichever one of the four
 * state-machine arms ran. **At most one arm runs per pass**, so the counts say
 * how many passes each frame spends where, the accumulators give each arm's
 * amortised share, and the maxima give the arm's own cost -- which is the
 * figure the chunk bound is set from. The remainder (RGB minus the head and the
 * arm) is the switch and the state read; the reader subtracts for it.
 *
 * At most, and not exactly, since 2026-08-16: RGB_MATRIX_IDLE_GATE_ENABLE
 * returns from rgb_matrix_task() ahead of the head on a SYNCING pass whose
 * millisecond has not turned over, and such a pass stamps neither the head nor
 * an arm. The head's count is therefore the passes that reached the state
 * machine, and **the gated passes are `ph` minus the four arm counts**. The RGB
 * segment itself still closes on every pass, because that mark is
 * quantum/keyboard.c's and sits outside this task.
 *
 * The cursor is seeded by the QTM mark, which is the mark that closes the
 * segment before RGB, so opening this span costs no counter read and adds no
 * call site to quantum/keyboard.c. */
#define ERA_PASS_PHASE_RGB_TIMERS 0U
#define ERA_PASS_PHASE_RGB_START 1U
#define ERA_PASS_PHASE_RGB_RENDER 2U
#define ERA_PASS_PHASE_RGB_FLUSH 3U
#define ERA_PASS_PHASE_RGB_SYNC 4U
#define ERA_PASS_PHASE_RGB_COUNT 5U

typedef struct {
    uint32_t passes;
    uint32_t pass_max_us; /* longest whole pass in the window */
    uint32_t band[ERA_PASS_PHASE_BAND_COUNT];
    uint32_t us[ERA_PASS_PHASE_COUNT];
    uint32_t us_max[ERA_PASS_PHASE_COUNT];
    uint32_t hk_us[ERA_PASS_PHASE_HK_COUNT];
    uint32_t hk_max[ERA_PASS_PHASE_HK_COUNT];
    uint32_t rgb_us[ERA_PASS_PHASE_RGB_COUNT];
    uint32_t rgb_max[ERA_PASS_PHASE_RGB_COUNT];
    uint32_t rgb_count[ERA_PASS_PHASE_RGB_COUNT];
} era_pass_phase_diagnostics_t;

/* Open the HK sub-span. Called immediately after the LOOP mark, which has just
   stamped the counter, so this is a copy rather than a read. */
void era_pass_phase_hk_open(void);
void era_pass_phase_hk_mark(uint8_t part);

/* Declared in quantum/rgb_matrix/rgb_matrix.c rather than included, the
   treatment quantum/keyboard.c's marks take: keyboards/era is not on core's
   include path. The ids stay here for the same reason they do there. */
void era_pass_phase_rgb_mark(uint8_t part);

/* Clear the maxima. A maximum is not a delta -- two snapshots cannot subtract
   into "the worst pass of this window" -- so the window's start clears them and
   its end reads them. The accumulators are untouched: they are deltas and must
   stay free-running. */
void era_pass_phase_reset_maxima(void);

/* Close the segment `phase` names and open the next one. Called with a literal
   at every ERA-side site. */
void era_pass_phase_mark(uint8_t phase);

/* The five segments that end inside quantum/keyboard.c, one named entry point
   each. Core declares these in the file rather than including this header --
   keyboards/era is not on QMK core's include path, which is the same reason
   quantum/action_layer.c declares its ERA accessor -- and a declaration cannot
   carry the ids without giving them a second home. So the ids stay here and
   what crosses is five names, each a tail call onto mark(). The alternative
   costs one constant with two definitions, which is the defect this project
   pays for most often. */
void era_pass_phase_mark_difference(void);
void era_pass_phase_mark_action(void);
void era_pass_phase_mark_quantum(void);
void era_pass_phase_mark_rgb(void);
void era_pass_phase_mark_keyboard_tail(void);

/* The wrap point, at the top of matrix_scan(): closes ERA_PASS_PHASE_REST and
   counts one pass. Separate from mark() so the pass counter costs a store on
   one site rather than a compare on twelve. */
void era_pass_phase_wrap(void);

void era_pass_phase_get_diagnostics(era_pass_phase_diagnostics_t *snapshot);
