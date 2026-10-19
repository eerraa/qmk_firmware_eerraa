// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_rgb_sleep.h"

#include "eeconfig.h"
#include "eeprom.h"
#include "keycode_config.h"

bool era_rgb_sleep_enabled(void) {
    return !keymap_config.era_rgb_sleep_disabled;
}

#ifdef VIA_ENABLE
#    include "../system/era_via_system.h"
#    include "via.h"

static bool era_rgb_sleep_set_enabled(bool enabled) {
    if (!eeprom_begin_transaction()) return false;
    keymap_config_t stored;
    eeconfig_read_keymap(&stored);
    /* RGB sleep owns only its bit; reloading the global word could switch a
     * failed-but-active NKRO choice without clearing its old report. */
    keymap_config.era_rgb_sleep_disabled = !enabled;
    stored.era_rgb_sleep_disabled = !enabled;
    eeconfig_update_keymap(&stored);
    return eeprom_end_transaction();
}

bool era_rgb_sleep_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || length < 4 || data[1] != ERA_VIA_SYSTEM_CHANNEL || data[2] != ERA_VIA_SYSTEM_RGB_SLEEP_ENABLE_VALUE_ID) {
        return false;
    }

    switch (data[0]) {
        case id_custom_set_value:
            if (!era_rgb_sleep_set_enabled(data[3] != 0)) data[0] = id_unhandled;
            data[3] = era_rgb_sleep_enabled() ? 1 : 0;
            return true;
        case id_custom_get_value:
            data[3] = era_rgb_sleep_enabled() ? 1 : 0;
            return true;
        default:
            return false;
    }
}
#endif
