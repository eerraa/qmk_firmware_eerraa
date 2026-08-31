// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_kkuk_via.h"

#include "era_kkuk.h"
#include "../system/era_common_via.h"
#include "via.h"

enum {
    ERA_KKUK_VIA_VALUE_ENABLE = 1,
    ERA_KKUK_VIA_VALUE_DELAY  = 2,
    ERA_KKUK_VIA_VALUE_REPEAT = 3,
    ERA_KKUK_VIA_VALUE_MODE   = 4,
};

static void era_kkuk_via_save(uint8_t channel_id) {
    (void)channel_id;
    era_kkuk_save_config();
}

static bool era_kkuk_via_set_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    switch (value_id) {
        case ERA_KKUK_VIA_VALUE_ENABLE:
            era_kkuk_set_enabled(value_data[0] != 0);
            return true;
        case ERA_KKUK_VIA_VALUE_DELAY:
            era_kkuk_set_delay_ticks(value_data[0]);
            return true;
        case ERA_KKUK_VIA_VALUE_REPEAT:
            era_kkuk_set_repeat_ticks(value_data[0]);
            return true;
        case ERA_KKUK_VIA_VALUE_MODE:
            era_kkuk_set_mode(ERA_KKUK_MODE_REPORT_PULSE);
            return true;
        default:
            return false;
    }
}

static bool era_kkuk_via_get_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    value_data[0] = 0;

    switch (value_id) {
        case ERA_KKUK_VIA_VALUE_ENABLE:
            value_data[0] = era_kkuk_get_enabled() ? 1 : 0;
            return true;
        case ERA_KKUK_VIA_VALUE_DELAY:
            value_data[0] = era_kkuk_get_delay_ticks();
            return true;
        case ERA_KKUK_VIA_VALUE_REPEAT:
            value_data[0] = era_kkuk_get_repeat_ticks();
            return true;
        case ERA_KKUK_VIA_VALUE_MODE:
            value_data[0] = era_kkuk_get_mode();
            return true;
        default:
            return false;
    }
}

bool era_kkuk_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || data[1] != ERA_VIA_KKUK_CHANNEL) {
        return false;
    }
    return era_common_via_value_command(data, length, era_kkuk_via_save, era_kkuk_via_set_value, era_kkuk_via_get_value);
}
