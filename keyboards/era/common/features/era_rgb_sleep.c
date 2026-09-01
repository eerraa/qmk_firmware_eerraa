// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_rgb_sleep.h"

#include "eeconfig.h"
#include "keycode_config.h"

bool era_rgb_sleep_enabled(void) {
    return !keymap_config.era_rgb_sleep_disabled;
}

#ifdef VIA_ENABLE
#    include "../system/era_via_system.h"
#    include "via.h"

static void era_rgb_sleep_set_enabled(bool enabled) {
    eeconfig_read_keymap(&keymap_config);
    if (era_rgb_sleep_enabled() == enabled) {
        return;
    }

    keymap_config.era_rgb_sleep_disabled = !enabled;
    eeconfig_update_keymap(&keymap_config);
}

bool era_rgb_sleep_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || length < 4 || data[1] != ERA_VIA_SYSTEM_CHANNEL || data[2] != ERA_VIA_SYSTEM_RGB_SLEEP_ENABLE_VALUE_ID) {
        return false;
    }

    switch (data[0]) {
        case id_custom_set_value:
            era_rgb_sleep_set_enabled(data[3] != 0);
            data[3] = era_rgb_sleep_enabled() ? 1 : 0;
            return true;
        case id_custom_get_value:
            eeconfig_read_keymap(&keymap_config);
            data[3] = era_rgb_sleep_enabled() ? 1 : 0;
            return true;
        default:
            return false;
    }
}
#endif
