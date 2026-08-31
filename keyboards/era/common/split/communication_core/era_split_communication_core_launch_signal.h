// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The one-shot user-visible report that core1 never came up.
 *
 * The shape is an owner decision (2026-07-26): full-field red, two flickers
 * per group, three groups, then stop. It is deliberately not a mode and does
 * not repeat - a red light blinking forever on a keyboard someone is typing on
 * becomes a defect of its own, and the evidence survives in the lifecycle
 * counters whether or not anyone was watching the LEDs.
 *
 * Every interval below is a multiple of RGB_MATRIX_LED_FLUSH_LIMIT (16 ms).
 * The matrix flush is gated at that period, so an interval off that grid
 * cannot be rendered as written: it would land on whatever the next flush
 * allowed, and the group boundaries this pattern exists to make legible would
 * stop being legible. 160 ms is also exactly the eeprom-sync status hold that
 * already ships, so the flicker length is a reused device-proven interval
 * rather than a fresh guess.
 *
 * What it was built not to claim was a core1 that dies *after* a successful
 * launch, on the reasoning that there was no relaunch for that and therefore
 * no moment at which this could honestly fire for it. R7 then built exactly
 * that relaunch: `declare_dead()` PSM-resets core1 and clears `launched`, and
 * the next `start()` runs the full handshake, under a per-boot attempt cap
 * whose give-up state is LOCAL_NO_LINK.
 *
 * What survives is the narrower claim the arm predicate actually makes, which
 * is worth stating exactly because it is no longer the same claim. The arm is
 * `(launch_error_count || entry_timeout_count) && !launched`, and
 * `declare_dead()` clears `launched` without clearing either monotonic
 * counter. So a board whose core1 was declared dead fires this only if it also
 * recorded a launch or entry failure at some point -- either the relaunch's
 * own failures, which is the honest case, or a transient boot failure it had
 * already recovered from, which is not: that board can show the light during
 * the relaunch window even though the relaunch then succeeds. Whether the
 * predicate should be narrowed against that is a behaviour question and an
 * owner decision, not something to settle by editing this comment. */

#ifndef ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_FLICKER_MS
#    define ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_FLICKER_MS 160U
#endif
#ifndef ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_GROUP_GAP_MS
#    define ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_GROUP_GAP_MS 640U
#endif
#ifndef ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_TAIL_MS
#    define ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_SIGNAL_TAIL_MS 960U
#endif

/* Advance the pattern from a cold task boundary. Returns whether it still owns
 * the status frame; `on` receives whether the field should be lit at this
 * instant and is written on every call, including when the pattern is idle. */
bool era_split_communication_core_launch_signal_advance(bool *on);
