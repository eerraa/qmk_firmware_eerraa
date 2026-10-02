// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "era_nonsplit_board.h"
#include "era_common_features.h"
#include "../storage/era_reset_guard.h"
#ifdef VIA_ENABLE
#    include "era_common_via.h"
#endif

/* The non-split class skeleton: the QMK hooks every non-split ERA board wires
   identically, owned once. What a board adds on top of them is
   era_board_hooks.h; what it replaces instead of them is nothing -- a board
   that needs a different body for one of the hooks below opts out of the whole
   unit with ERA_BOARD_COMMON_ENABLE = no in its post_rules.mk, because a
   second strong definition of any of these fails the link rather than winning.

   Class-scoped by name and by content. It may reach era_common_* surfaces and
   nothing under keyboards/era/common/split, which is what keeps the split
   skeleton from being the place a non-split board's behaviour ends up.

   The split flavour of the same five hooks is split/era_split_board.c. */

/* The weak defaults for this class's own two extension points. Here rather
   than beside the neutral set in era_board_hooks.c because there is exactly one
   non-split skeleton, so there is no second copy for them to drift against --
   which is the only reason that file exists. split/era_split_board.c holds its
   own two for the same reason. */
__attribute__((weak)) void era_board_config_load(void) {}

__attribute__((weak)) void era_board_config_reset(void) {}

/* Active-loop services belong to the class skeleton. During QMK's blocking
   suspend loop, era_usb_session.c carries the double-tap closer instead;
   neither service depends on a board-specific user hook. */
void housekeeping_task_kb(void) {
    era_common_features_task();
    era_board_housekeeping_tick();
    era_common_features_maintenance_task();
}

void matrix_init_kb(void) {
    era_reset_guard_check();
    era_board_config_load();
    era_common_features_init();
    matrix_init_user();
}

void eeconfig_init_kb(void) {
    era_board_config_reset();
    era_common_features_init();
    eeconfig_init_user();
    era_reset_guard_write();
}

#if defined(ERA_BACKLIGHT_EFFECT_ENABLE) || defined(ERA_RGBLIGHT_PULSE_ENABLE)
void switch_event_kb(uint8_t row, uint8_t col, bool pressed) {
    era_common_features_switch_event(row, col, pressed);
    switch_event_user(row, col, pressed);
}
#endif

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (!era_common_features_process_record(keycode, record)) {
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
void via_init_kb(void) {
    /* Invalidate before VIA checks its region; matrix_init_kb owns runtime
       loading for both VIA and default keymaps. */
    era_reset_guard_check();
}

/* The one VIA dispatcher. QMK's own via_custom_value_command_kb is weak
   (quantum/via.c), so this is a strong override exactly as each board file's
   was; what changes is that there is one of it.

   This class's router is the ERA command router directly, where the split
   class inserts its sync-policy value ids first. Below that the keyboard
   channel is identical on both classes and is answered once, in
   era_board_hooks.c. The common handler persists on id_custom_save and returns
   false on purpose, so era_board_via_save() runs beside it rather than instead
   of it. */
void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    if (era_common_via_handle_command(data, length)) {
        return;
    }

    era_board_via_keyboard_channel_command(data, length);
}
#endif
