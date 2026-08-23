// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "era_common_via.h"

#ifdef VIA_ENABLE
/* The one boundary check the ERA VIA surface has, and the reason it replaced
   thirty-three that could not fire is at the top of `era_common_via.h`. ERA
   reads `data[0..4]` -- command, channel, value id, and a two-byte value for
   `era_socd_via.c`'s keycodes -- so a report smaller than five bytes stops the
   build here rather than being read past at run time. `usb_descriptor.h` is
   included for `RAW_EPSIZE` alone, and here rather than in the header because
   board files include that header and have no business pulling in the USB
   descriptor. */
#    include "usb_descriptor.h"
_Static_assert(RAW_EPSIZE >= 5, "An ERA VIA report must carry command, channel, value id and a two-byte value.");
#endif

#ifdef ERA_TAP_DANCE_ENABLE
#    include "../features/era_tapdance_via.h"
#    include "via.h"
#endif
#ifdef ERA_DEBOUNCE_ENABLE
#    include "../features/era_debounce_via.h"
#endif
#ifdef ERA_KKUK_ENABLE
#    include "../features/era_kkuk_via.h"
#endif
#ifdef ERA_NKRO_VIA_ENABLE
#    include "../features/era_nkro_via.h"
#    include "via.h"
#endif
#ifdef ERA_BACKLIGHT_EFFECT_ENABLE
#    include "../features/era_backlight_via.h"
#    include "via.h"
#endif
#ifdef ERA_RGB_INDICATOR_ENABLE
#    include "../features/era_rgb_indicator_via.h"
#    include "via.h"
#endif
#ifdef ERA_SOCD_ENABLE
#    include "../features/era_socd_via.h"
#endif
#ifdef ERA_TAPPING_CONFIG_ENABLE
#    include "../features/era_tapping_via.h"
#endif
#ifdef ERA_MOUSEKEY_CONFIG_ENABLE
#    include "../features/era_mousekey_via.h"
#endif
#ifdef ERA_VIA_SYSTEM_ENABLE
#    include "era_via_system.h"
#endif

/* **The unclaimed answer.** Every handler below returns true only for a command
   it actually answered. A channel it does not own, a value id it does not own,
   a command id it has no arm for, a report too short to carry one: all of those
   return false and write nothing into the report. `id_unhandled` is written in
   exactly one place -- era_board_via_keyboard_channel_command()
   (system/era_board_hooks.c) -- because that is the end of the chain on both
   class skeletons and the only point that knows the chain is out of handlers.

   Two channels have a second claimant standing behind the first, which is what
   makes this a rule rather than a tidy: era_via_system.c owns value ids 1-4 on
   ERA_VIA_SYSTEM_CHANNEL and era_split_via_sync.c owns 5-7 on the same channel,
   reachable only because the first declines. id_custom_channel carries a stack
   of claimants the same way -- tap dance, NKRO, the two common lighting
   adapters, and whatever a board hangs off
   era_board_hooks.h. A handler that marks id_unhandled and claims the command
   deletes every claimant behind it; the single-claimant channels used to do
   exactly that and got away with it only because no value id is shared, which
   is an accident of the partition and not a licence. */

bool era_common_via_handle_system_command(uint8_t *data, uint8_t length) {
#ifdef ERA_VIA_SYSTEM_ENABLE
    return era_via_system_handle_via_command(data, length);
#else
    (void)data;
    (void)length;
    return false;
#endif
}

bool era_common_via_handle_feature_command(uint8_t *data, uint8_t length) {
#ifdef ERA_SOCD_ENABLE
    if (era_socd_handle_via_command(data, length)) {
        return true;
    }
#endif

#ifdef ERA_KKUK_ENABLE
    if (era_kkuk_handle_via_command(data, length)) {
        return true;
    }
#endif

#ifdef ERA_DEBOUNCE_ENABLE
    if (era_debounce_handle_via_command(data, length)) {
        return true;
    }
#endif

#ifdef ERA_TAPPING_CONFIG_ENABLE
    if (era_tapping_handle_via_command(data, length)) {
        return true;
    }
#endif

#ifdef ERA_MOUSEKEY_CONFIG_ENABLE
    if (era_mousekey_handle_via_command(data, length)) {
        return true;
    }
#endif

    return false;
}

/* **Everything on this channel whose persistence the quiet gate owns.** A VIA
   save carries no value id, so it cannot be routed by one: every claimant of
   this channel that holds state has to be listed, here or at the save arm
   below, and a new one is covered by being added to whichever list it belongs
   in. The gate that calls this lives in system/era_board_hooks.c and states why
   one timer serves the whole channel.

   The two here are the continuous ones: both ship `range` controls -- backlight
   brightness, breathing period and blink speed; indicator brightness and colour
   -- and a client dragging any of them sends a save per step. */
void era_common_via_keyboard_channel_save(void) {
#ifdef ERA_BACKLIGHT_EFFECT_ENABLE
    era_backlight_via_save();
#endif
#ifdef ERA_RGB_INDICATOR_ENABLE
    era_rgb_indicator_via_save();
#endif
}

bool era_common_via_handle_keyboard_channel_command(uint8_t *data, uint8_t length) {
    if (!data || data[1] != id_custom_channel) {
        return false;
    }

    /* Persist and then return false on purpose, so a board with its own
       keyboard-channel config can save beside these. What persists *here* is
       only what the quiet gate does not own; the rest is armed by the caller
       (system/era_board_hooks.c) and runs from the list above. */
    if (data[0] == id_custom_save) {
#ifdef ERA_TAP_DANCE_ENABLE
        /* Immediate, and it is the one claimant on this channel that stays so.
           Tap dance has no continuous control -- its VIA surface is keycode
           pickers, dropdowns and exact terms, so a save is one deliberate user
           action and never a drag -- and its 88-byte block is the largest here,
           so deferring it would buy nothing and put the largest write behind a
           timer. */
        era_tapdance_handle_via_command(data, length);
#endif
        return false;
    }

#ifdef ERA_TAP_DANCE_ENABLE
    if (era_tapdance_is_value_id(data[2])) {
        return era_tapdance_handle_via_command(data, length);
    }
#endif

#ifdef ERA_NKRO_VIA_ENABLE
    /* Below the tap-dance block; NKRO has no held state to flush and claims one
       value id, so order costs nothing here. */
    if (era_nkro_via_is_value_id(data[2])) {
        return era_nkro_via_handle_via_command(data, length);
    }
#endif

#ifdef ERA_BACKLIGHT_EFFECT_ENABLE
    /* Last of the common claimants, and the only one reaching into the 0..4
       band a board may otherwise keep for itself. This router runs ahead of
       the board hook, so on a board that enabled both, these four ids would
       win -- which is why the selector defaults off and why no board that has
       a keyboard-channel handler of its own turns it on. */
    if (era_backlight_via_is_value_id(data[2])) {
        return era_backlight_via_handle_via_command(data, length);
    }
#endif

#ifdef ERA_RGB_INDICATOR_ENABLE
    /* 6..12, above both the board's own 0..4 band and the NKRO toggle at 5, so
       unlike the backlight block above this one takes nothing a board could
       have kept. Order against the blocks above is therefore free; it is last
       because it is the newest. */
    if (era_rgb_indicator_via_is_value_id(data[2])) {
        return era_rgb_indicator_via_handle_via_command(data, length);
    }
#endif

    return false;
}

bool era_common_via_handle_command(uint8_t *data, uint8_t length) {
    if (era_common_via_handle_system_command(data, length)) {
        return true;
    }

    return era_common_via_handle_feature_command(data, length);
}
