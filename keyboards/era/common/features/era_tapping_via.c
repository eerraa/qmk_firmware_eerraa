// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_tapping_via.h"

#include "era_tapping.h"
#include "../storage/era_eeprom_layout.h"
#include "../system/era_common_via.h"
#include "via.h"

enum {
    ERA_TAPPING_VIA_VALUE_GLOBAL_TERM = 1,
    ERA_TAPPING_VIA_VALUE_PERMISSIVE_HOLD,
    ERA_TAPPING_VIA_VALUE_HOLD_ON_OTHER_KEY_PRESS,
    ERA_TAPPING_VIA_VALUE_RETRO_TAPPING,
    ERA_TAPPING_VIA_VALUE_GLOBAL_TERM_EXACT = 5,
};

static void era_tapping_via_save(uint8_t channel_id) {
    (void)channel_id;
    era_tapping_save_config();
}

static bool era_tapping_via_set_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    switch (value_id) {
        case ERA_TAPPING_VIA_VALUE_GLOBAL_TERM:
            era_tapping_set_term_ms(era_common_via_legacy_term_ms(value_data[0]));
            return true;
        case ERA_TAPPING_VIA_VALUE_GLOBAL_TERM_EXACT:
            return era_tapping_set_term_ms_exact(era_common_via_get_u16_be(value_data));
        case ERA_TAPPING_VIA_VALUE_PERMISSIVE_HOLD:
            era_tapping_set_permissive_hold(value_data[0] != 0);
            return true;
        case ERA_TAPPING_VIA_VALUE_HOLD_ON_OTHER_KEY_PRESS:
            era_tapping_set_hold_on_other_key_press(value_data[0] != 0);
            return true;
        case ERA_TAPPING_VIA_VALUE_RETRO_TAPPING:
            era_tapping_set_retro_tapping(value_data[0] != 0);
            return true;
        default:
            return false;
    }
}

static bool era_tapping_via_get_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    value_data[0] = 0;

    switch (value_id) {
        case ERA_TAPPING_VIA_VALUE_GLOBAL_TERM:
            value_data[0] = era_common_via_legacy_term_units(era_tapping_get_term_ms());
            return true;
        case ERA_TAPPING_VIA_VALUE_GLOBAL_TERM_EXACT:
            era_common_via_put_u16_be(value_data, era_tapping_get_term_ms());
            return true;
        case ERA_TAPPING_VIA_VALUE_PERMISSIVE_HOLD:
            value_data[0] = era_tapping_get_permissive_hold() ? 1 : 0;
            return true;
        case ERA_TAPPING_VIA_VALUE_HOLD_ON_OTHER_KEY_PRESS:
            value_data[0] = era_tapping_get_hold_on_other_key_press() ? 1 : 0;
            return true;
        case ERA_TAPPING_VIA_VALUE_RETRO_TAPPING:
            value_data[0] = era_tapping_get_retro_tapping() ? 1 : 0;
            return true;
        default:
            return false;
    }
}

bool era_tapping_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || data[1] != ERA_VIA_TAPPING_CHANNEL) {
        return false;
    }
    return era_common_via_value_command(data, length, era_tapping_via_save, era_tapping_via_set_value, era_tapping_via_get_value);
}
