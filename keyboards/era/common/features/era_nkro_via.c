// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_nkro_via.h"

#include "eeconfig.h"
#include "eeprom.h"
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
static bool era_nkro_via_set_enabled(bool enabled) {
    if (!eeprom_begin_transaction()) return false;
    keymap_config_t stored;
    eeconfig_read_keymap(&stored);
    /* A failed write can leave the active format ahead of EEPROM. Compare the
     * runtime bit, and preserve other owners' durable bits in the write. */
    bool changed = keymap_config.nkro != enabled;
    if (changed) clear_keyboard();
    keymap_config.nkro = enabled;
    stored.nkro = enabled;
    eeconfig_update_keymap(&stored);
    if (changed) clear_keyboard();
    return eeprom_end_transaction();
}

bool era_nkro_via_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || data[1] != id_custom_channel || !era_nkro_via_is_value_id(data[2])) {
        return false;
    }

    uint8_t *command_id = &data[0];
    uint8_t *value_data = &data[3];

    switch (*command_id) {
        case id_custom_set_value:
            if (!era_nkro_via_set_enabled(value_data[0] != 0)) *command_id = id_unhandled;
            /* Echo what the bit actually became rather than what was asked.
               The two differ if a future NKRO_ENABLE=no build ever reaches
               here, and VIA renders the echo. */
            value_data[0] = keymap_config.nkro ? 1 : 0;
            return true;
        case id_custom_get_value:
            value_data[0] = keymap_config.nkro ? 1 : 0;
            return true;
        /* No id_custom_save arm on purpose: the set path already wrote through
           eeconfig, so there is nothing held back for a save to flush. */
        default:
            return false;
    }
}
