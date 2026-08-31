// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "era_split_board.h"
#include "era_split_keyboard.h"
#include "../system/era_board_hooks.h"
#include "../system/era_common_features.h"
#ifdef VIA_ENABLE
#    include "../system/era_common_via.h"
#endif

/* The split class skeleton: the QMK hooks every split ERA board wires
   identically, owned once. Extracted 2026-08-13 from sirind/common/tomak_common.c
   rather than from three board files, which is the reason the split half of the
   board layer was built last -- extracting a class layer from an already-clean
   family unit is a different operation from extracting one from three diverged
   copies, and only the first can be checked by reading.

   **The discipline that keeps it honest**: this unit references only
   `era_split_*` and `era_common_*` surfaces. Anything naming a `tomak_*` type
   is family content and belongs in `sirind/common/tomak_common.[ch]`. A split
   class layer that absorbs tomak content would look correct while every split
   board is a tomak, and the next split family is what would pay for it.

   What a board adds is system/era_board_hooks.h -- the class-neutral set the
   non-split skeleton calls too -- plus the split-only points in
   era_split_board.h.

   **What this unit deliberately does NOT own**, because taking it would have
   changed runtime behaviour rather than moved it:

   - `matrix_init_kb`, `eeconfig_init_kb` and `via_init_kb`. On the only split
     family in the tree the ERA module init (`era_split_sync_policy_init()` and
     `era_common_features_init()`) runs *conditionally* inside `matrix_init_kb`
     -- only when the reset guard failed and a strict reset just wrote fresh
     defaults -- and unconditionally in the other two. A class skeleton calling
     it unconditionally in all three would add an EEPROM re-read on every boot,
     and one that called it nowhere would leave the post-reset re-init to a
     board. Neither is a move. They stay in the family unit.
   - `led_update_kb`, `notify_usb_device_state_change_kb` and
     `rgb_matrix_indicators_advanced_kb`, which carry indicator content on
     every split board in the tree and nothing class-generic. */

/* The weak defaults for this class's own extension points. They live in
   this unit rather than beside the neutral set in system/era_board_hooks.c
   because there is exactly one split skeleton, so there is no second copy for
   them to drift against -- which is the only reason that file exists. */
__attribute__((weak)) void era_split_board_pre_init(void) {}

__attribute__((weak)) void era_split_board_post_init(void) {}

__attribute__((weak)) uint16_t era_split_board_rgb_sleep_timeout_seconds(void) {
    return 0;
}

__attribute__((weak)) bool era_split_board_set_rgb_sleep_timeout_seconds(uint16_t seconds) {
    (void)seconds;
    return false;
}

/* housekeeping_task_kb is not optional on an ERA board, and the split flavour
   of that fact is the same one the non-split skeleton carries:
   `era_split_keyboard_task()` calls `era_common_features_task()` first, and
   that is the only caller of `rp2040_bootloader_double_tap_reset_task()` in the
   tree. A board .c that stopped calling it never closed the window armed before
   crt0's copy loops and entered BOOTSEL on every reset, permanently, with
   nothing mechanical able to see the loss. Owning the hook here is what retires
   that failure class for split boards, exactly as
   system/era_nonsplit_board.c does for the others. */
void housekeeping_task_kb(void) {
    era_split_keyboard_task();
    era_board_housekeeping_tick();
    /* Presentation edges are discovered by the board tick above. Only after
       that may opportunistic NVM maintenance claim a blocking flash window; the
       common maintenance owner yields while the refreshed frame is unfinished. */
    era_common_features_maintenance_task();
    // No need to invoke the user-specific callback, as it's been called
    // already.
}

void keyboard_pre_init_kb(void) {
    era_split_board_pre_init();
    era_split_keyboard_pre_init();
    keyboard_pre_init_user();
}

void keyboard_post_init_kb(void) {
    keyboard_post_init_user();
    /* Before the wire opens. era_split_keyboard_post_init() runs storage init
       and then the single core1 launch step, so board content that touches the
       wire pins has to be on this side of it. */
    era_split_board_post_init();
    era_split_keyboard_post_init();
}

void suspend_wakeup_init_kb(void) {
    era_split_keyboard_suspend_wakeup_init();
    suspend_wakeup_init_user();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (!era_split_keyboard_process_record(keycode, record)) {
        return false;
    }
    if (!era_board_process_record(keycode, record)) {
        return false;
    }
    if (!process_record_user(keycode, record)) {
        return false;
    }
    return true;
}

#ifdef VIA_ENABLE
/* The one split VIA dispatcher, and this hook is the one place the two classes
   differ: a split board routes through era_split_keyboard_handle_via_command()
   first, which adds the sync-policy value ids ahead of the ERA command router
   a non-split board reaches directly. That difference is the whole of this
   function. The keyboard channel below it is identical on both classes and is
   answered once, in system/era_board_hooks.c, which is also where the reason a
   second copy is not kept is written. */
void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    if (era_split_keyboard_handle_via_command(data, length)) {
        return;
    }

    era_board_via_keyboard_channel_command(data, length);
}
#endif
