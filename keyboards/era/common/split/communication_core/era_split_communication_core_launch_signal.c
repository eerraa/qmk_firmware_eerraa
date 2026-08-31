// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_communication_core_launch_signal.h"

#include "era_split_communication_core_internal.h"
#include "timer.h"

typedef struct {
    uint16_t ms;
    uint8_t  on;
} era_split_communication_core_launch_signal_step_t;

/* The owner's table, one row per interval. Two flickers make a group, three
 * groups make the message, and the trailing dark interval is what makes "two
 * at a time, three times" readable instead of six evenly spaced blinks: the
 * 640 ms group gap is four times the 160 ms in-group gap, and the 960 ms tail
 * is longer again so the end is unambiguous. 3 x 480 + 2 x 640 + 960 = 3680 ms.
 *
 * Cost is per transition, not per frame: a held status frame that is ACTIVE
 * and not DIRTY neither renders nor flushes, so this is eight render+flush
 * pairs in total. */
static const era_split_communication_core_launch_signal_step_t era_split_communication_core_launch_signal_steps[] = {
    {ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_FLICKER_MS, 1},
    {ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_FLICKER_MS, 0},
    {ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_FLICKER_MS, 1},
    {ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_GROUP_GAP_MS, 0},

    {ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_FLICKER_MS, 1},
    {ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_FLICKER_MS, 0},
    {ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_FLICKER_MS, 1},
    {ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_GROUP_GAP_MS, 0},

    {ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_FLICKER_MS, 1},
    {ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_FLICKER_MS, 0},
    {ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_FLICKER_MS, 1},
    {ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_TAIL_MS, 0},
};

#define ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_STEP_COUNT \
    (sizeof(era_split_communication_core_launch_signal_steps) / sizeof(era_split_communication_core_launch_signal_steps[0]))

typedef struct {
    uint8_t  armed;
    uint8_t  running;
    uint8_t  step;
    uint32_t step_start_ms;
} era_split_communication_core_launch_signal_state_t;

static era_split_communication_core_launch_signal_state_t g_era_split_communication_core_launch_signal;

/* A recorded launch failure that core1 has not since made good.
 *
 * The two counters are monotonic and `launched` is written by core1's own
 * entry, so the conjunction is an edge that cannot un-happen and cannot fire
 * on a healthy boot. It is also why the arm is safe to evaluate at the
 * housekeeping cadence rather than at the failure itself: the scheduler's
 * retry runs earlier in the same housekeeping pass, so a launch that fails
 * once and succeeds on the immediately following attempt has already set
 * `launched` by the time this is read, and never arms the signal. Reporting a
 * failure the firmware recovered from before anyone could see it would make
 * the light mean less, not more. */
static bool era_split_communication_core_launch_signal_failed(void) {
    return (g_era_split_communication_core.launch_error_count != 0 ||
            g_era_split_communication_core.entry_timeout_count != 0) &&
           !g_era_split_communication_core.launched;
}

bool era_split_communication_core_launch_signal_advance(bool *on) {
    era_split_communication_core_launch_signal_state_t *signal = &g_era_split_communication_core_launch_signal;
    bool                                                lit    = false;

    /* Every board that boots correctly runs this on every housekeeping pass
       for its whole life and must never reach the timer: the un-armed arm is
       three loads of monotonic state, and once the pattern has finished it is
       one load. Only the 3.68 s the pattern is actually running reads a clock. */
    if (!signal->armed) {
        /* Armed once, on the first failure edge, and never re-armed. This used
           to reason from "the unbounded launch retry re-reaches the same
           failure on every pass"; R7 bounded that retry at
           ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_ATTEMPT_CAP attempts per boot,
           and the conclusion is unchanged because it never rested on the retry
           in the first place. The predicate below reads a *state*, not an
           event: the two counters are monotonic and, once the cap is reached,
           `launched` never becomes 1 again for this boot -- so a "currently
           failing" predicate is permanently true on a failed board and would
           restart the pattern forever instead of emitting it once. */
        if (era_split_communication_core_launch_signal_failed()) {
            signal->armed         = 1;
            signal->running       = 1;
            signal->step          = 0;
            signal->step_start_ms = timer_read32();
        }
    } else if (signal->running) {
        /* Advance by whole steps rather than up to `now_ms`, so a late pass
           cannot stretch the pattern. Bounded by the table either way. */
        uint32_t now_ms = timer_read32();
        while (signal->running && (uint32_t)(now_ms - signal->step_start_ms) >= era_split_communication_core_launch_signal_steps[signal->step].ms) {
            signal->step_start_ms += era_split_communication_core_launch_signal_steps[signal->step].ms;
            signal->step++;
            if (signal->step >= (uint8_t)ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_STEP_COUNT) {
                signal->running = 0;
            }
        }
    }

    if (signal->running) {
        lit = era_split_communication_core_launch_signal_steps[signal->step].on != 0;
    }

    if (on != NULL) {
        *on = lit;
    }
    return signal->running != 0;
}
