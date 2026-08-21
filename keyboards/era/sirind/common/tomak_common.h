// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

/* The tomak family's whole board content, owned once for `tomak`, `tomak79h`
   and `tomak79s`. **None of the three keeps a `.c` file**, and the reason that
   is reachable rather than aspirational is that everything which differs
   between them is geometry, and geometry needs no translation unit: the matrix
   and its pins are `keyboard.json`'s and `config.h`'s, the keymap is
   `keymaps/`', the LED count is `keyboard.json`'s, and which LEDs are the
   badge the indicator paints is one `config.h` constant per board --
   `TOMAK_BADGE_LED_MIN`, which tomak_common.c refuses a board for omitting.

   Family content, not class content. Every name here is `tomak_*`, and that is
   the boundary rule the split class skeleton is held to: anything naming a
   tomak type belongs to this unit, and the class layer may reach only
   `era_split_*` and `era_common_*`.

   **The three boards are one design and TOMAK79H was its reference** (owner
   decision 2026-08-17). The hardware differs in the matrix and the RGB/LED
   arrangement and in nothing else, so a runtime difference between them was a
   defect of history rather than a board fact. Six stood in three board files
   when the decision was made. Five were resolved by 79S and `tomak` adopting
   79H, which is what this unit now holds one copy of:

     - the lock-LED source is the state `led_update_kb()` last cached, not a
       live `host_keyboard_led_state()`;
     - the core1 launch-signal report exists on all three, with the
       STATUS arbitration it needs (launch outranks the link-fallback
       report, which outranks the storage indicator);
     - the storage-visibility predicate is advanced from the housekeeping
       cadence, not from inside the render pass;
     - the STATUS frame's own bookkeeping is reached whenever RGB_MATRIX is
       built, not only when the storage engine is, because the launch report is
       the other producer and does not depend on the storage engine;
     - `rgb_matrix_indicators_kb` is defined, so the indicator gets a
       full-range apply per frame beside the per-iteration window.

   **The sixth was resolved by removal**: `tomak`'s eight `IN_*` indicator
   keycodes went, with the `era_board_process_record()` override and the
   increment/decrement helpers that served them, and the indicator is
   configured through VIA. No capability was lost, and that is checkable rather
   than asserted -- every value the eight keycodes reached is on this unit's own
   VIA surface, on all three boards: `id_custom_indicator_toggle`,
   `id_custom_indicator_override`, `id_custom_indicator_brightness` and
   `id_custom_indicator_color`. What went was a second control path over one
   state, on one board of three.

   It cost `tomak` two things worth naming because neither is silent. Six
   shipping keymaps lost the eight positions those keys occupied, which are now
   `KC_TRNS` as they already were on 79H and 79S. And the family's keycode enum
   below became one enum, so `tomak`'s three diagnostics keycodes moved from
   `QK_KB_16`..`18` down to `QK_KB_8`..`10` where its siblings always had them:
   a dynamic keymap stored by an earlier firmware reads those slots as
   different keycodes, which is the ordinary consequence of a keycode change
   and not a compatibility surface this firmware carries.

   **The family this repository did it to first is `newone/odessey`**, and its
   shape is the one here: one `_common.c` holding all of it, no board `.c`, and
   one `SRC` line in each board's `post_rules.mk`. */

enum tomak_indicator_mode {
    TOMAK_INDICATOR_OFF = 0,
    TOMAK_INDICATOR_CAPS_LOCK,
    TOMAK_INDICATOR_SCROLL_LOCK,
    TOMAK_INDICATOR_NUM_LOCK
};

/* The shape tomak79h carried, which is the one that had no union around it.
   What left on 2026-08-11, and why none of it reached EEPROM: the `raw`
   uint32_t view had no reader anywhere in the tree, so the union, the
   bitfields and the packed attribute existed for a view nobody took;
   `lock_indicator_enabled` and `lock_indicator_mode_initialized` were written
   by tomak_config_set_indicator_mode() and read by nothing; and the boards
   disagreed on `reserved0`, which shifted every bitfield after it between two
   boards that persist through the same record. That cost nothing only because
   `raw` was unused and the persist is field-by-field into
   tomak_era_keyboard_config_t, which is the stored layout and is unchanged. */
typedef struct {
    uint8_t lock_indicator_mode;
    bool    lock_indicator_overrides_rgb;
    HSV     lock_indicator_hsv;
    bool    full_rgb_matrix_enabled;
} tomak_config_t;

extern tomak_config_t g_tomak_config;

/* The family's keycodes, one enum for three boards. It is here and not in each
   board header because it is family-named -- `tomak_keycodes` -- and the
   boundary rule above puts a tomak-named thing in this unit. The three copies
   it replaces were character-identical once `tomak`'s eight `IN_*` values
   went; before that they were not, and the comment that stood in each board
   header explained the offset they caused. */
enum tomak_keycodes {
    TD0 = QK_KB_0,
    TD1,
    TD2,
    TD3,
    TD4,
    TD5,
    TD6,
    TD7,
    WIRE_DIAG,   // scheduler-owned passive split wire diagnostics
    WIRE_DIAG_2, // communication-core stop/probe split wire diagnostics
    WIRE_QWIN    // fixed-window QMK scan counter diagnostics
};

#ifdef VIA_ENABLE
// via value id declaration
enum tomak_custom_value_id {
    id_custom_indicator_toggle = 0,
    id_custom_indicator_override,
    id_custom_indicator_brightness,
    id_custom_indicator_color,
    id_custom_badge_only
};
#endif
