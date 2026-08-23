// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "era_board_hooks.h"
#ifdef VIA_ENABLE
#    include "era_common_via.h"
#endif

#if defined(MCU_RP)
#    include "hardware/structs/timer.h" /* timer_hw->timerawl, the tick gate's raw-microsecond clock */
#endif

/* The weak defaults behind era_board_hooks.h, in one unit rather than one copy
   per class skeleton: two skeletons that share a header and duplicate the same
   bodies is the under-split shape era_source_map.md's Source Editing Rules
   name, and it is also how a hook comes to exist on one class only. Exactly
   one skeleton is ever linked, so this unit travels with whichever one the
   board selected.

   Two things here are not defaults, and both are here for the same reason the
   defaults are -- one copy for both classes. `era_board_via_keyboard_channel_
   command()` is the keyboard channel's whole answer, and the quiet persistence
   gate below is that channel's save half: this is the one unit that sees every
   channel-0 command *and* owns the once-per-millisecond cadence a deferred
   write is committed from, so the gate needs no file of its own. */

__attribute__((weak)) void era_board_housekeeping_task(void) {}

#ifdef VIA_ENABLE
/* --- The keyboard channel's quiet persistence gate ------------------------ */

/* **One gate for every deferred save on this channel, and one is not an
   approximation of several.** `id_custom_save` on `id_custom_channel` carries
   no value id, so it arms every claimant of the channel at the same instant,
   always -- three per-domain timers here would hold identical values for their
   whole lives, at three times the state. What arms it is a save and never a
   set: every VIA setter in this tree writes runtime only, so "quiet is measured
   from the last persist approval" holds by construction rather than by care.

   **What it buys is the drag.** The ERA backlight menu, the ERA RGB indicator
   and both board indicator surfaces ship `range` and `color` controls, and a
   client pairs a save with every set -- so before this gate a slider drag was
   one flash write per step, each with a different value, which neither the NVM
   layer's byte diff nor the wear-leveling cache compare can collapse: both
   remove writes whose *content* is unchanged, and every step of a drag changes
   it. Only a decision not to write yet removes those, and this is the one place
   in the tree where that decision exists.

   **The number is `ERA_STORAGE_QUIET_DEFER_MS`**, the one this fork already
   defers the RGB Matrix eeconfig block by (`quantum/eeconfig.h`), and there is
   no maximum-age flush beside it: a user holding a slider produces a continuous
   stream of approvals, so a max-age flush would write flash during exactly the
   drag this exists to keep out of it.

   **No domain waits two intervals.** RGB Matrix and RGBLight are answered by
   `quantum/via.c` on channels 3 and 2 and never reach this router, and each
   holds its own gate over its own eeconfig block; this one covers the ERA
   backlight effects and QMK's backlight level, the ERA RGB indicator, and the
   board's own keyboard-channel record. The tomak family's local 500 ms timer
   was deleted in the change that added this, so the badge/indicator surface is
   behind one predicate rather than two.

   What is *not* here is stated where it persists: `era_common_via.c`'s save arm
   keeps tap dance immediate, and the sync policy, link level and system channel
   are critical write-through paths that never enter a gate.

   Without the number the two spellings differ in one place -- the arm is the
   immediate save and the task is empty -- so a build with no quiet interval
   keeps exactly the behaviour it had before the gate existed. */
#    if defined(ERA_STORAGE_QUIET_DEFER_MS)
static bool     era_board_keyboard_save_pending;
static uint16_t era_board_keyboard_save_timer;

static void era_board_keyboard_channel_save_arm(void) {
    era_board_keyboard_save_pending = true;
    era_board_keyboard_save_timer   = timer_read();
}

/* The pending flag is cleared before the write and not after. The write reaches
   the storage engine's dirty intake through the NVM changed hook and can be
   sliced, and a sliced erase runs the keyboard pass in its gaps
   (`era_invariants.md`) -- so the housekeeping cadence is reachable from inside
   this call, and a flag still standing there would schedule a second identical
   write.

   No presenter note here either, since the 2026-08-14 redesign: the write below
   reaches that same intake, and the storage engine's pending fact is what
   lights the SYNC lamp -- at this instant, for every domain, not just this
   one. */
static void era_board_keyboard_channel_save_commit(void) {
    era_board_keyboard_save_pending = false;
    era_common_via_keyboard_channel_save();
    era_board_via_save();
}

static void era_board_keyboard_channel_save_task(void) {
    if (!era_board_keyboard_save_pending || timer_elapsed(era_board_keyboard_save_timer) <= ERA_STORAGE_QUIET_DEFER_MS) {
        return;
    }
    era_board_keyboard_channel_save_commit();
}
#    else
static void era_board_keyboard_channel_save_arm(void) {
    era_common_via_keyboard_channel_save();
    era_board_via_save();
}

static void era_board_keyboard_channel_save_task(void) {}
#    endif
#endif

/* The class skeletons call this, once per keyboard pass, and it forwards to
   the board hook once per millisecond. Everything a board hangs off this
   cadence is millisecond-scale by contract — deferred config saves, launch
   signal blink phases, the EEPROM SYNC lamp's visible advance — while the
   caller runs at the scan rate, tens of passes per millisecond. Re-asking
   millisecond questions every pass was a measured piece of the 2026-08-15
   scan-rate regression (qwin bisect), so the gate is one raw counter read
   here rather than a discipline every board must remember. On a platform
   without the raw counter the tick degrades to the old every-pass call. */
void era_board_housekeeping_tick(void) {
#if defined(MCU_RP)
    static uint32_t era_board_housekeeping_last_us;
    uint32_t        now_us = timer_hw->timerawl;
    if ((uint32_t)(now_us - era_board_housekeeping_last_us) < 1000) {
        return;
    }
    era_board_housekeeping_last_us = now_us;
#endif
#ifdef VIA_ENABLE
    era_board_keyboard_channel_save_task();
#endif
#if defined(RGBLIGHT_ENABLE) && defined(ERA_STORAGE_QUIET_DEFER_MS)
    /* The underglow domain's own gate, run from the same cadence and not from
       `rgblight_task()`: keeping the call here is what leaves every keyboard in
       the fork outside this layer with an unchanged per-pass path. It is not on
       the keyboard channel and cannot ride the gate above -- VIA answers
       channel 2 inside `quantum/via.c` and returns -- so the two are separate
       gates over separate eeconfig blocks, and a drag arms exactly one. */
    eeconfig_flush_rgblight_deferred_task();
#endif
    era_board_housekeeping_task();
}

__attribute__((weak)) bool era_board_process_record(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;
    (void)record;
    return true;
}

#ifdef VIA_ENABLE
__attribute__((weak)) bool era_board_via_get_value(uint8_t *data) {
    (void)data;
    return false;
}

__attribute__((weak)) bool era_board_via_set_value(uint8_t *data) {
    (void)data;
    return false;
}

__attribute__((weak)) void era_board_via_save(void) {}

/* The keyboard channel's board half, called by whichever class skeleton is
   linked once its own router has declined. It is here rather than in
   era_common_via.c because what it dispatches is the era_board_hooks.h table
   above, and here rather than in either skeleton because both held it and the
   header states what a second copy costs.

   It is also **the one writer of id_unhandled** in the ERA VIA surface. Both
   skeletons call it unconditionally as their last statement, so every command
   no handler claimed arrives here, and every path out of it that answered
   nothing writes the byte. The rule the rest of the chain keeps so that this
   stays true is stated at era_common_via.c's router. */
void era_board_via_keyboard_channel_command(uint8_t *data, uint8_t length) {
    uint8_t *command_id        = &data[0];
    uint8_t *channel_id        = &data[1];
    uint8_t *value_id_and_data = &data[2];

    if (*channel_id == id_custom_channel) {
        if (era_common_via_handle_keyboard_channel_command(data, length)) {
            return;
        }

        switch (*command_id) {
            case id_custom_set_value:
                if (!era_board_via_set_value(value_id_and_data)) {
                    *command_id = id_unhandled;
                }
                break;
            case id_custom_get_value:
                if (!era_board_via_get_value(value_id_and_data)) {
                    *command_id = id_unhandled;
                }
                break;
            case id_custom_save:
                /* Arms the channel's quiet gate above rather than saving here.
                   The common router has already run and persisted what the gate
                   does not own, and it returned false so this arm runs beside
                   it -- which is the same contract as before, one step later in
                   time. */
                era_board_keyboard_channel_save_arm();
                break;
            default:
                *command_id = id_unhandled;
                break;
        }
        return;
    }

    *command_id = id_unhandled;
}
#endif
