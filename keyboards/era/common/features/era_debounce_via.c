// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_debounce_via.h"

#include "../system/era_common_via.h"
#include "../system/era_matrix_debounce_config.h"
#include "via.h"

enum {
    ERA_DEBOUNCE_VIA_VALUE_MODE = 1,
    ERA_DEBOUNCE_VIA_VALUE_TIME_SINGLE,
    ERA_DEBOUNCE_VIA_VALUE_TIME_PRE,
    ERA_DEBOUNCE_VIA_VALUE_TIME_POST,
};

static void era_debounce_via_save(uint8_t channel_id) {
    (void)channel_id;
    era_matrix_debounce_config_save();
}

static bool era_debounce_via_set_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    switch (value_id) {
        case ERA_DEBOUNCE_VIA_VALUE_MODE:
            return era_matrix_debounce_config_set_mode(value_data[0]);
        case ERA_DEBOUNCE_VIA_VALUE_TIME_SINGLE:
            return era_matrix_debounce_config_set_single_delay(value_data[0]);
        case ERA_DEBOUNCE_VIA_VALUE_TIME_PRE:
            return era_matrix_debounce_config_set_press_delay(value_data[0]);
        case ERA_DEBOUNCE_VIA_VALUE_TIME_POST:
            return era_matrix_debounce_config_set_release_delay(value_data[0]);
        default:
            return false;
    }
}

static bool era_debounce_via_get_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    value_data[0] = 0;

    switch (value_id) {
        case ERA_DEBOUNCE_VIA_VALUE_MODE:
            value_data[0] = era_matrix_debounce_config_get_mode();
            return true;
        case ERA_DEBOUNCE_VIA_VALUE_TIME_SINGLE:
        case ERA_DEBOUNCE_VIA_VALUE_TIME_PRE:
            value_data[0] = era_matrix_debounce_config_get_press_delay();
            return true;
        case ERA_DEBOUNCE_VIA_VALUE_TIME_POST:
            value_data[0] = era_matrix_debounce_config_get_release_delay();
            return true;
        default:
            return false;
    }
}

bool era_debounce_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || data[1] != ERA_VIA_DEBOUNCE_CHANNEL) {
        return false;
    }
    return era_common_via_value_command(data, length, era_debounce_via_save, era_debounce_via_set_value, era_debounce_via_get_value);
}
