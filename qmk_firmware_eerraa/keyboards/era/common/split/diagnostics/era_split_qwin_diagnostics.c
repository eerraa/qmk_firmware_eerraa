// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "era_split_qwin_diagnostics.h"

#include "../communication_core/era_split_communication_core_diagnostics.h"
#include "../../system/era_rp2040_matrix.h"
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
#    include "../../system/era_pass_phase_diagnostics.h"
#endif

#include "timer.h"

/* The scan counter is declared in era_split_wire_diagnostics.h, which this
   unit includes rather than re-declaring: the counter is defined in a third
   file (era_split_wire_diagnostics_counter.c, the one unit present in every
   profile that has either diagnostic), so a hand-written extern here was a
   second declaration of one symbol that no compiler could check against the
   definition. */
#include "era_split_wire_diagnostics.h"

/* The one instrument redesign the performance batch allowed itself, and what
 * each addition answers (era_capture_reading.md carries the field meanings):
 *
 * - `park=` and the segment list exist because a qwin figure is only
 *   comparable at 60 s and against a window of its own length
 *   (device-measured): the first window reads high by up to 4 %
 *   for a reason nobody has established. `seg=` prints the scan rate of each
 *   ERA_SPLIT_QWIN_SEGMENT_MS slice of the window in order, so one long window
 *   shows where the transient sits instead of a run of short windows guessing
 *   at it, and ERA_SPLIT_QWIN_SETTLE_MS defers the start sample by a fixed
 *   settle once a sitting has read a value off `seg=`. Both default to what
 *   the instrument did before: whole window, no settle.
 * - `smp/smp_hz`, `ovr`, `rearm`, `fd1` are the PIO sampler's own facts --
 *   frames the DMA moved (a hardware counter, so no scan-path cost), frames
 *   re-read because the writer lapped them, DMA re-triggers, and the state
 *   machine's stall flags -- because after the PIO engine `scan_hz` is core0's
 *   pass rate and no longer says how often the keys are looked at.
 * - `park=` is core1's sleep: parks and microseconds asleep, the backend's
 *   in-transaction parks and the loop's idle park together, so a window's
 *   sleep share is park_us over elapsed. It reads on both roles.
 *
 * The segment and settle bookkeeping runs from a once-per-millisecond tick
 * that era_split_keyboard.c already paces; the scan path is untouched. */

#ifndef ERA_SPLIT_QWIN_SEGMENT_MS
#    define ERA_SPLIT_QWIN_SEGMENT_MS 10000U
#endif

#ifndef ERA_SPLIT_QWIN_SETTLE_MS
#    define ERA_SPLIT_QWIN_SETTLE_MS 0U
#endif

#define ERA_SPLIT_QWIN_SEGMENTS_MAX 8U

typedef struct {
    bool     armed;   /* first press seen; the start sample may still be pending on the settle */
    bool     active;  /* start sample taken */
    uint32_t print_count;
    uint32_t press_ms;
    uint32_t start_ms;
    uint32_t start_count;
    uint32_t ccore_start_loop_count;
    uint32_t ccore_start_idle_count;
    uint32_t ccore_start_wake_count;
    uint32_t ccore_start_wake_observed_count;
    uint32_t ccore_start_session_transaction_count;
    uint32_t ccore_start_source_push_transaction_count;
    uint32_t ccore_start_park_count;
    uint32_t ccore_start_park_us;
    uint32_t pio_start_sample_words;
    uint32_t pio_start_torn_retries;
    uint32_t pio_start_rearms;
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    era_pass_phase_diagnostics_t phase_start;
#endif
    /* Segment slices: the scan count and time at the last slice boundary and
       the per-slice rates recorded so far. */
    uint32_t segment_last_ms;
    uint32_t segment_last_count;
    uint8_t  segment_count;
    uint32_t segment_hz[ERA_SPLIT_QWIN_SEGMENTS_MAX];
} era_split_qwin_diagnostics_state_t;

static era_split_qwin_diagnostics_state_t era_split_qwin_diagnostics_state;

static void era_split_qwin_diagnostics_flush(void) {
#ifdef CONSOLE_ENABLE
    extern void console_task(void);
    console_task();
#endif
}

static uint32_t era_split_qwin_diagnostics_rate_hz(uint32_t elapsed_ms, uint32_t delta) {
    if (elapsed_ms == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)delta * 1000ULL + elapsed_ms / 2U) / elapsed_ms);
}

static void era_split_qwin_diagnostics_take_start_sample(uint32_t now_ms) {
    era_split_qwin_diagnostics_state_t *state = &era_split_qwin_diagnostics_state;
    state->active                             = true;
    state->start_ms                           = now_ms;
    state->start_count                        = era_split_wire_diagnostics_raw_matrix_scan_count;
    state->segment_last_ms                    = now_ms;
    state->segment_last_count                 = state->start_count;
    state->segment_count                      = 0;
    era_split_communication_core_diagnostics_t ccore;
    era_split_communication_core_get_diagnostics_snapshot(&ccore);
    state->ccore_start_loop_count                    = ccore.loop_count;
    state->ccore_start_idle_count                    = ccore.idle_count;
    state->ccore_start_wake_count                    = ccore.wake_count;
    state->ccore_start_wake_observed_count           = ccore.wake_observed_count;
    state->ccore_start_session_transaction_count     = ccore.lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].transaction_count;
    state->ccore_start_source_push_transaction_count = ccore.lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].transaction_count;
    state->ccore_start_park_count                    = ccore.park_count + ccore.idle_count;
    state->ccore_start_park_us                       = ccore.park_us + ccore.idle_us;
    era_rp2040_matrix_pio_diagnostics_t pio;
    era_rp2040_matrix_pio_get_diagnostics(&pio);
    state->pio_start_sample_words = pio.sample_words;
    state->pio_start_torn_retries = pio.torn_retries;
    state->pio_start_rearms       = pio.rearms;
    era_rp2040_matrix_pio_clear_fdebug();
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    era_pass_phase_get_diagnostics(&state->phase_start);
    /* The maxima belong to this window and cannot be differenced out of a pair
       of snapshots, so the window's start is where they are cleared. */
    era_pass_phase_reset_maxima();
#endif
}

static void era_split_qwin_diagnostics_start(void) {
    era_split_qwin_diagnostics_state_t *state = &era_split_qwin_diagnostics_state;
    uint32_t                            now_ms = timer_read32();
    state->armed                               = true;
    state->active                              = false;
    state->press_ms                            = now_ms;
    if (ERA_SPLIT_QWIN_SETTLE_MS == 0U) {
        era_split_qwin_diagnostics_take_start_sample(now_ms);
    }
}

void era_split_qwin_diagnostics_tick_1ms(void) {
    era_split_qwin_diagnostics_state_t *state = &era_split_qwin_diagnostics_state;
    if (!state->armed) {
        return;
    }
    uint32_t now_ms = timer_read32();
    if (!state->active) {
        if ((uint32_t)(now_ms - state->press_ms) >= ERA_SPLIT_QWIN_SETTLE_MS) {
            era_split_qwin_diagnostics_take_start_sample(now_ms);
        }
        return;
    }
    if (state->segment_count < ERA_SPLIT_QWIN_SEGMENTS_MAX && (uint32_t)(now_ms - state->segment_last_ms) >= ERA_SPLIT_QWIN_SEGMENT_MS) {
        uint32_t count                           = era_split_wire_diagnostics_raw_matrix_scan_count;
        state->segment_hz[state->segment_count]  = era_split_qwin_diagnostics_rate_hz(now_ms - state->segment_last_ms, count - state->segment_last_count);
        state->segment_count++;
        state->segment_last_ms    = now_ms;
        state->segment_last_count = count;
    }
}

static void era_split_qwin_diagnostics_stop(void) {
    era_split_qwin_diagnostics_state_t *state = &era_split_qwin_diagnostics_state;
    uint32_t                            now_ms      = timer_read32();
    uint32_t                            end_count   = era_split_wire_diagnostics_raw_matrix_scan_count;
    uint32_t                            elapsed_ms  = state->active ? now_ms - state->start_ms : 0;
    uint32_t                            delta_count = state->active ? end_count - state->start_count : 0;
    uint32_t                            scan_hz     = era_split_qwin_diagnostics_rate_hz(elapsed_ms, delta_count);

    state->armed  = false;
    state->active = false;
    state->print_count++;

    era_split_communication_core_diagnostics_t ccore;
    era_split_communication_core_get_diagnostics_snapshot(&ccore);
    uint32_t ccore_loop_delta = ccore.loop_count - state->ccore_start_loop_count;
    uint32_t ccore_idle_delta = ccore.idle_count - state->ccore_start_idle_count;
    uint32_t ccore_wake_delta = ccore.wake_count - state->ccore_start_wake_count;
    uint32_t ccore_wobs_delta = ccore.wake_observed_count - state->ccore_start_wake_observed_count;
    uint32_t ccore_sess_delta = ccore.lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].transaction_count - state->ccore_start_session_transaction_count;
    uint32_t ccore_sp_delta   = ccore.lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].transaction_count - state->ccore_start_source_push_transaction_count;
    uint32_t park_count_delta = (ccore.park_count + ccore.idle_count) - state->ccore_start_park_count;
    uint32_t park_us_delta    = (ccore.park_us + ccore.idle_us) - state->ccore_start_park_us;
    uprintf("wire qwin pc=%lu ms=%lu scan_hz=%lu raw=%lu start=%lu end=%lu ccore=%lu/%lu/%lu/%lu sp=%lu sess=%lu park=%lu/%lu settle=%lu",
            (unsigned long)state->print_count,
            (unsigned long)elapsed_ms,
            (unsigned long)scan_hz,
            (unsigned long)delta_count,
            (unsigned long)state->start_count,
            (unsigned long)end_count,
            (unsigned long)ccore_loop_delta,
            (unsigned long)ccore_idle_delta,
            (unsigned long)ccore_wake_delta,
            (unsigned long)ccore_wobs_delta,
            (unsigned long)ccore_sp_delta,
            (unsigned long)ccore_sess_delta,
            (unsigned long)park_count_delta,
            (unsigned long)park_us_delta,
            (unsigned long)ERA_SPLIT_QWIN_SETTLE_MS);
    era_rp2040_matrix_pio_diagnostics_t pio;
    era_rp2040_matrix_pio_get_diagnostics(&pio);
    uint32_t sample_words_delta = pio.sample_words - state->pio_start_sample_words;
    uint32_t frames_delta       = pio.frame_words != 0 ? sample_words_delta / pio.frame_words : 0;
    uprintf(" smp=%lu smp_hz=%lu ovr=%lu rearm=%lu fd1=%X",
            (unsigned long)frames_delta,
            (unsigned long)era_split_qwin_diagnostics_rate_hz(elapsed_ms, frames_delta),
            (unsigned long)(pio.torn_retries - state->pio_start_torn_retries),
            (unsigned long)(pio.rearms - state->pio_start_rearms),
            (unsigned)pio.fdebug);
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    /* The pass, itemised: one accumulated microsecond total per segment over
       this window, in execution order, beside the passes they were charged over
       (era_pass_phase_diagnostics.h names them). Deltas, so a reader divides by
       `ph` and the twelve must sum to the pass period scan_hz reports. Printed
       only on the qwin_phase rung -- the plain qwin line is unchanged, which is
       what keeps this window comparable with the batch-1 campaign's. */
    era_pass_phase_diagnostics_t phase;
    era_pass_phase_get_diagnostics(&phase);
    uprintf(" ph=%lu us=", (unsigned long)(phase.passes - state->phase_start.passes));
    for (uint8_t segment = 0; segment < ERA_PASS_PHASE_COUNT; segment++) {
        uprintf(segment == 0 ? "%lu" : ",%lu", (unsigned long)(phase.us[segment] - state->phase_start.us[segment]));
    }
    /* The worst pass of the window and the worst each segment contributed --
       absolutes, not deltas, cleared at the window's start. This is the half of
       the instrument that can see a render spike; the accumulators above
       structurally cannot. */
    uprintf(" pmax=%lu mx=", (unsigned long)phase.pass_max_us);
    for (uint8_t segment = 0; segment < ERA_PASS_PHASE_COUNT; segment++) {
        uprintf(segment == 0 ? "%lu" : ",%lu", (unsigned long)phase.us_max[segment]);
    }
    /* How often, beside how bad. A maximum on its own cannot tell a once-a-
       minute outlier from a thousand-a-second rhythm, and those are different
       designs. Deltas, like the accumulators. */
    uprintf(" over=");
    for (uint8_t band = 0; band < ERA_PASS_PHASE_BAND_COUNT; band++) {
        uprintf(band == 0 ? "%lu" : ",%lu", (unsigned long)(phase.band[band] - state->phase_start.band[band]));
    }
    /* HK split into its parts: feature tasks, scheduler task, the
       once-per-millisecond gate. The remainder of HK is the wire diagnostics
       task and the plumbing, and the reader subtracts for it. */
    uprintf(" hk=");
    for (uint8_t part = 0; part < ERA_PASS_PHASE_HK_COUNT; part++) {
        uprintf(part == 0 ? "%lu" : ",%lu", (unsigned long)(phase.hk_us[part] - state->phase_start.hk_us[part]));
    }
    uprintf(" hkmx=");
    for (uint8_t part = 0; part < ERA_PASS_PHASE_HK_COUNT; part++) {
        uprintf(part == 0 ? "%lu" : ",%lu", (unsigned long)phase.hk_max[part]);
    }
    /* RGB split into the timer head and the four state-machine arms. `rgbn` is
       how many passes each arm ran -- at MOST one per pass, so an arm's maximum
       is that arm's own cost rather than an outlier, and the render arm's is
       what a chunk bound is set from. The remainder of RGB is the switch and
       the state read.

       At most rather than exactly, and the head is no longer per-pass either,
       since RGB_MATRIX_IDLE_GATE_ENABLE (2026-08-16): a SYNCING pass whose
       millisecond has not turned over returns ahead of the head and stamps
       neither it nor an arm, so the four arm counts do not sum to `ph` and the
       difference is the gated passes. The RGB segment itself still closes every
       pass -- that mark is quantum/keyboard.c's and sits outside this task.
       (era_pass_phase_diagnostics.h, era_capture_reading.md.) */
    uprintf(" rgb=");
    for (uint8_t part = 0; part < ERA_PASS_PHASE_RGB_COUNT; part++) {
        uprintf(part == 0 ? "%lu" : ",%lu", (unsigned long)(phase.rgb_us[part] - state->phase_start.rgb_us[part]));
    }
    uprintf(" rgbn=");
    for (uint8_t part = 0; part < ERA_PASS_PHASE_RGB_COUNT; part++) {
        uprintf(part == 0 ? "%lu" : ",%lu", (unsigned long)(phase.rgb_count[part] - state->phase_start.rgb_count[part]));
    }
    uprintf(" rgbmx=");
    for (uint8_t part = 0; part < ERA_PASS_PHASE_RGB_COUNT; part++) {
        uprintf(part == 0 ? "%lu" : ",%lu", (unsigned long)phase.rgb_max[part]);
    }
#endif
    uprintf(" seg=");
    for (uint8_t segment = 0; segment < state->segment_count; segment++) {
        uprintf(segment == 0 ? "%lu" : ",%lu", (unsigned long)state->segment_hz[segment]);
    }
    if (state->segment_count == 0) {
        uprintf("-");
    }
    uprintf("\r\n");
    era_split_qwin_diagnostics_flush();
}

bool era_split_qwin_diagnostics_process_record(uint16_t keycode, keyrecord_t *record) {
    if (record == NULL || !record->event.pressed) {
        return true;
    }
    if (keycode != WIRE_QWIN) {
        return true;
    }

    if (era_split_qwin_diagnostics_state.armed) {
        era_split_qwin_diagnostics_stop();
    } else {
        era_split_qwin_diagnostics_start();
    }
    return false;
}
