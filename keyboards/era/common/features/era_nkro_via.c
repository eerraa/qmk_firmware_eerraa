// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_nkro_via.h"

#include "eeconfig.h"
#include "keycode_config.h"
#include "quantum.h"
#include "via.h"

bool era_nkro_via_is_value_id(uint8_t value_id) {
    return value_id == ERA_VIA_NKRO_ENABLE_VALUE_ID;
}

/* The bit is QMK's `keymap_config.nkro` and the storage is QMK's eeconfig, so
 * this is an adapter and not a feature: nothing here owns state. What it does
 * own is the two `clear_keyboard()` calls around the change. Switching NKRO
 * swaps the report the host is being sent, so a key held across the swap is
 * reported in one protocol and released in the other -- the host never sees the
 * release and the key sticks. Clearing on both sides is what makes the toggle
 * safe to press while typing, and it is why this is not a bare bit write. */
static void era_nkro_via_set_enabled(bool enabled) {
    eeconfig_read_keymap(&keymap_config);
    if (keymap_config.nkro == enabled) {
        return;
    }

    clear_keyboard();
    keymap_config.nkro = enabled;
    eeconfig_update_keymap(&keymap_config);
    clear_keyboard();
}

bool era_nkro_via_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || data[1] != id_custom_channel || !era_nkro_via_is_value_id(data[2])) {
        return false;
    }

    uint8_t *command_id = &data[0];
    uint8_t *value_data = &data[3];

    switch (*command_id) {
        case id_custom_set_value:
            era_nkro_via_set_enabled(value_data[0] != 0);
            /* Echo what the bit actually became rather than what was asked.
               The two differ if a future NKRO_ENABLE=no build ever reaches
               here, and VIA renders the echo. */
            value_data[0] = keymap_config.nkro ? 1 : 0;
            return true;
        case id_custom_get_value:
            eeconfig_read_keymap(&keymap_config);
            value_data[0] = keymap_config.nkro ? 1 : 0;
            return true;
        /* No id_custom_save arm on purpose: the set path already wrote through
           eeconfig, so there is nothing held back for a save to flush. */
        default:
            return false;
    }
}
