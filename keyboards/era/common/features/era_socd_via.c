// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_socd_via.h"

#include "era_socd.h"
#include "../storage/era_eeprom_storage.h"
#include "../system/era_common_via.h"
#include "via.h"

enum {
    ERA_SOCD_VIA_VALUE_ENABLE    = 1,
    ERA_SOCD_VIA_VALUE_KEYCODE_0 = 2,
    ERA_SOCD_VIA_VALUE_KEYCODE_1 = 3,
    ERA_SOCD_VIA_VALUE_MODE      = 4,
};

static uint8_t era_socd_via_pair_from_channel(uint8_t channel_id) {
    switch (channel_id) {
        case ERA_VIA_SOCD_LR_CHANNEL:
            return ERA_SOCD_PAIR_LR;
        case ERA_VIA_SOCD_UD_CHANNEL:
            return ERA_SOCD_PAIR_UD;
        default:
            return ERA_SOCD_PAIR_COUNT;
    }
}

/* The pair is derived from the channel the command arrived on, which is why
   these three take the channel byte the shared value-command terminal hands
   every callback rather than a pair of their own. It is the one caller that
   uses it, and it is why the terminal passes it at all. */
static void era_socd_via_save(uint8_t channel_id) {
    era_socd_save_pair(era_socd_via_pair_from_channel(channel_id));
}

static bool era_socd_via_set_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    uint8_t pair = era_socd_via_pair_from_channel(channel_id);
    switch (value_id) {
        case ERA_SOCD_VIA_VALUE_ENABLE:
            return era_socd_set_enabled(pair, value_data[0] != 0);
        case ERA_SOCD_VIA_VALUE_KEYCODE_0:
        case ERA_SOCD_VIA_VALUE_KEYCODE_1:
            return era_socd_set_keycode(pair, value_id - ERA_SOCD_VIA_VALUE_KEYCODE_0, era_common_via_get_u16_be(value_data));
        case ERA_SOCD_VIA_VALUE_MODE:
            return era_socd_set_mode(pair, ERA_SOCD_MODE_LAST_INPUT_WINS);
        default:
            return false;
    }
}

static bool era_socd_via_get_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    uint8_t pair = era_socd_via_pair_from_channel(channel_id);
    value_data[0] = 0;
    value_data[1] = 0;

    switch (value_id) {
        case ERA_SOCD_VIA_VALUE_ENABLE:
            value_data[0] = era_socd_get_enabled(pair) ? 1 : 0;
            return true;
        case ERA_SOCD_VIA_VALUE_KEYCODE_0:
        case ERA_SOCD_VIA_VALUE_KEYCODE_1: {
            uint16_t keycode = era_socd_get_keycode(pair, value_id - ERA_SOCD_VIA_VALUE_KEYCODE_0);
            era_common_via_put_u16_be(value_data, keycode);
            return true;
        }
        case ERA_SOCD_VIA_VALUE_MODE:
            value_data[0] = era_socd_get_mode(pair);
            return true;
        default:
            return false;
    }
}

bool era_socd_handle_via_command(uint8_t *data, uint8_t length) {
    /* Two channels rather than one, so the addressing test is "does this
       channel name a pair" instead of an equality. Everything after it is the
       shared protocol, which is what the pair used to be threaded through. */
    if (!data || era_socd_via_pair_from_channel(data[1]) >= ERA_SOCD_PAIR_COUNT) {
        return false;
    }
    return era_common_via_value_command(data, length, era_socd_via_save, era_socd_via_set_value, era_socd_via_get_value);
}
