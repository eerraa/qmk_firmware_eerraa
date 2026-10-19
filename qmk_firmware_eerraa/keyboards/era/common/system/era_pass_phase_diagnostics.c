// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* The pass-phase accumulators. What the twelve segments are, and why the
   instrument is its own selector, is at the top of the header. */

#include "era_pass_phase_diagnostics.h"

#if !defined(ERA_PASS_PHASE_DIAGNOSTICS_ENABLE)
#    error "era_pass_phase_diagnostics.c must be built only when ERA_PASS_PHASE_DIAGNOSTICS_ENABLE is set."
#endif

#if !defined(MCU_RP)
#    error "The pass-phase instrument reads the RP2040 raw microsecond counter; there is no portable arm."
#endif

/* The register struct only, and before any ChibiOS header: pico-sdk's
   hardware/timer.h clashes with the ChibiOS TIMER macro (the PIO sampler
   carries the same note). */
#include "hardware/structs/timer.h"

/* Band upper-open edges in microseconds: how many passes ran longer than each.
   Rule 2 knobs -- the default lives with the reader. Chosen against a measured
   ~25 us mean pass, so they read as "twice", "four times" and "eight times". */
#ifndef ERA_PASS_PHASE_BAND1_US
#    define ERA_PASS_PHASE_BAND1_US 50U
#endif
#ifndef ERA_PASS_PHASE_BAND2_US
#    define ERA_PASS_PHASE_BAND2_US 100U
#endif
#ifndef ERA_PASS_PHASE_BAND3_US
#    define ERA_PASS_PHASE_BAND3_US 200U
#endif

static uint32_t era_pass_phase_last_us;
static uint32_t era_pass_phase_band[ERA_PASS_PHASE_BAND_COUNT];
static uint32_t era_pass_phase_hk_last_us;
static uint32_t era_pass_phase_hk_us[ERA_PASS_PHASE_HK_COUNT];
static uint32_t era_pass_phase_hk_max[ERA_PASS_PHASE_HK_COUNT];
static uint32_t era_pass_phase_rgb_last_us;
static uint32_t era_pass_phase_rgb_us[ERA_PASS_PHASE_RGB_COUNT];
static uint32_t era_pass_phase_rgb_max[ERA_PASS_PHASE_RGB_COUNT];
static uint32_t era_pass_phase_rgb_count[ERA_PASS_PHASE_RGB_COUNT];
static uint32_t era_pass_phase_us[ERA_PASS_PHASE_COUNT];
static uint32_t era_pass_phase_us_max[ERA_PASS_PHASE_COUNT];
static uint32_t era_pass_phase_passes;
static uint32_t era_pass_phase_pass_start_us;
static uint32_t era_pass_phase_pass_max_us;

/* Not primed, and it does not need to be. The first mark of a boot charges the
   whole time since reset to whichever segment it names, which poisons that
   accumulator's absolute value and nothing a reader uses: every figure is a
   delta between two snapshots taken inside one window, long after that pass. A
   32-bit microsecond accumulator wraps at about 71 minutes, and unsigned
   subtraction reads the delta correctly across the wrap, so a window is bounded
   by the window and not by uptime. */
void era_pass_phase_mark(uint8_t phase) {
    uint32_t now   = timer_hw->timerawl;
    uint32_t delta = now - era_pass_phase_last_us;
    era_pass_phase_us[phase] += delta;
    if (delta > era_pass_phase_us_max[phase]) {
        era_pass_phase_us_max[phase] = delta;
    }
    era_pass_phase_last_us = now;
}

void era_pass_phase_wrap(void) {
    era_pass_phase_mark(ERA_PASS_PHASE_REST);
    /* mark() has just stored the stamp, so the pass period costs no second
       counter read. */
    uint32_t period = era_pass_phase_last_us - era_pass_phase_pass_start_us;
    if (period > era_pass_phase_pass_max_us) {
        era_pass_phase_pass_max_us = period;
    }
    era_pass_phase_pass_start_us = era_pass_phase_last_us;
    era_pass_phase_passes++;
    if (period >= ERA_PASS_PHASE_BAND1_US) {
        era_pass_phase_band[0]++;
        if (period >= ERA_PASS_PHASE_BAND2_US) {
            era_pass_phase_band[1]++;
            if (period >= ERA_PASS_PHASE_BAND3_US) {
                era_pass_phase_band[2]++;
            }
        }
    }
}

void era_pass_phase_hk_open(void) {
    era_pass_phase_hk_last_us = era_pass_phase_last_us;
}

void era_pass_phase_hk_mark(uint8_t part) {
    uint32_t now   = timer_hw->timerawl;
    uint32_t delta = now - era_pass_phase_hk_last_us;
    era_pass_phase_hk_us[part] += delta;
    if (delta > era_pass_phase_hk_max[part]) {
        era_pass_phase_hk_max[part] = delta;
    }
    era_pass_phase_hk_last_us = now;
}

/* Counted as well as timed: exactly one state-machine arm runs per pass, so the
   counts are how many passes a frame spends in each -- which is what turns an
   arm's maximum into a chunk cost rather than an outlier. */
void era_pass_phase_rgb_mark(uint8_t part) {
    uint32_t now   = timer_hw->timerawl;
    uint32_t delta = now - era_pass_phase_rgb_last_us;
    era_pass_phase_rgb_us[part] += delta;
    era_pass_phase_rgb_count[part]++;
    if (delta > era_pass_phase_rgb_max[part]) {
        era_pass_phase_rgb_max[part] = delta;
    }
    era_pass_phase_rgb_last_us = now;
}

void era_pass_phase_reset_maxima(void) {
    era_pass_phase_pass_max_us = 0;
    for (uint8_t phase = 0; phase < ERA_PASS_PHASE_COUNT; phase++) {
        era_pass_phase_us_max[phase] = 0;
    }
    for (uint8_t part = 0; part < ERA_PASS_PHASE_HK_COUNT; part++) {
        era_pass_phase_hk_max[part] = 0;
    }
    for (uint8_t part = 0; part < ERA_PASS_PHASE_RGB_COUNT; part++) {
        era_pass_phase_rgb_max[part] = 0;
    }
}

/* The five quantum/keyboard.c sites. Named rather than numbered because core
   cannot see the ids (header). Each is a tail call. */
void era_pass_phase_mark_difference(void) {
    era_pass_phase_mark(ERA_PASS_PHASE_DIFF);
}

void era_pass_phase_mark_action(void) {
    era_pass_phase_mark(ERA_PASS_PHASE_ACT);
}

void era_pass_phase_mark_quantum(void) {
    era_pass_phase_mark(ERA_PASS_PHASE_QTM);
    /* This mark closes the segment before RGB, so its stamp is the RGB span's
       start. Seeding here is a copy rather than a read, and it keeps
       quantum/keyboard.c free of a call it would otherwise need. */
    era_pass_phase_rgb_last_us = era_pass_phase_last_us;
}

void era_pass_phase_mark_rgb(void) {
    era_pass_phase_mark(ERA_PASS_PHASE_RGB);
}

void era_pass_phase_mark_keyboard_tail(void) {
    era_pass_phase_mark(ERA_PASS_PHASE_KTAIL);
}

void era_pass_phase_get_diagnostics(era_pass_phase_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }
    snapshot->passes      = era_pass_phase_passes;
    snapshot->pass_max_us = era_pass_phase_pass_max_us;
    for (uint8_t phase = 0; phase < ERA_PASS_PHASE_COUNT; phase++) {
        snapshot->us[phase]     = era_pass_phase_us[phase];
        snapshot->us_max[phase] = era_pass_phase_us_max[phase];
    }
    for (uint8_t band = 0; band < ERA_PASS_PHASE_BAND_COUNT; band++) {
        snapshot->band[band] = era_pass_phase_band[band];
    }
    for (uint8_t part = 0; part < ERA_PASS_PHASE_HK_COUNT; part++) {
        snapshot->hk_us[part]  = era_pass_phase_hk_us[part];
        snapshot->hk_max[part] = era_pass_phase_hk_max[part];
    }
    for (uint8_t part = 0; part < ERA_PASS_PHASE_RGB_COUNT; part++) {
        snapshot->rgb_us[part]    = era_pass_phase_rgb_us[part];
        snapshot->rgb_max[part]   = era_pass_phase_rgb_max[part];
        snapshot->rgb_count[part] = era_pass_phase_rgb_count[part];
    }
}
