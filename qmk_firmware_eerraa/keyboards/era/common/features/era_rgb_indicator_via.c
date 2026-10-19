// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_rgb_indicator_via.h"

#include "quantum.h"
#include "via.h"
#include "era_rgb_indicator.h"

/* Off `era_common_via.h`'s shared value-command rail, for the reason
   era_backlight_via.c is: the rail claims `id_custom_save` for its caller, and
   on the keyboard channel the save has to fall through so a board's own
   `era_board_via_save()` runs beside it. What replaces the rail's readback is
   the explicit echo on the source arm below, which is the only value here that
   a set can change. */

enum {
    ERA_RGB_INDICATOR_VIA_FIELD_SOURCE = 0,
    ERA_RGB_INDICATOR_VIA_FIELD_BRIGHTNESS,
    ERA_RGB_INDICATOR_VIA_FIELD_COLOR,
};

bool era_rgb_indicator_via_is_value_id(uint8_t value_id) {
    return value_id >= ERA_VIA_RGB_INDICATOR_FIRST_VALUE_ID && value_id <= ERA_VIA_RGB_INDICATOR_LAST_VALUE_ID;
}

static uint8_t era_rgb_indicator_via_slot(uint8_t value_id) {
    return (uint8_t)((value_id - ERA_VIA_RGB_INDICATOR_SLOT_BASE_VALUE_ID) / ERA_VIA_RGB_INDICATOR_VALUES_PER_SLOT);
}

static uint8_t era_rgb_indicator_via_field(uint8_t value_id) {
    return (uint8_t)((value_id - ERA_VIA_RGB_INDICATOR_SLOT_BASE_VALUE_ID) % ERA_VIA_RGB_INDICATOR_VALUES_PER_SLOT);
}

static bool era_rgb_indicator_via_get_value(uint8_t *value_id_and_data) {
    const uint8_t value_id   = value_id_and_data[0];
    uint8_t      *value_data = &value_id_and_data[1];

    if (value_id == ERA_VIA_RGB_INDICATOR_ENABLE_VALUE_ID) {
        value_data[0] = era_rgb_indicator_get_enabled() ? 1 : 0;
        return true;
    }

    const uint8_t slot = era_rgb_indicator_via_slot(value_id);
    if (!era_rgb_indicator_slot_exists(slot)) {
        return false;
    }

    switch (era_rgb_indicator_via_field(value_id)) {
        case ERA_RGB_INDICATOR_VIA_FIELD_SOURCE:
            value_data[0] = era_rgb_indicator_get_source(slot);
            return true;
        case ERA_RGB_INDICATOR_VIA_FIELD_BRIGHTNESS:
            value_data[0] = era_rgb_indicator_get_brightness(slot);
            return true;
        case ERA_RGB_INDICATOR_VIA_FIELD_COLOR:
            era_rgb_indicator_get_color(slot, value_data);
            return true;
        default:
            return false;
    }
}

static bool era_rgb_indicator_via_set_value(uint8_t *value_id_and_data) {
    const uint8_t value_id   = value_id_and_data[0];
    uint8_t      *value_data = &value_id_and_data[1];

    if (value_id == ERA_VIA_RGB_INDICATOR_ENABLE_VALUE_ID) {
        era_rgb_indicator_set_enabled(value_data[0] != 0);
        return true;
    }

    const uint8_t slot = era_rgb_indicator_via_slot(value_id);
    if (!era_rgb_indicator_slot_exists(slot)) {
        return false;
    }

    switch (era_rgb_indicator_via_field(value_id)) {
        case ERA_RGB_INDICATOR_VIA_FIELD_SOURCE:
            era_rgb_indicator_set_source(slot, value_data[0]);
            /* Echo what the source became. A value past the dropdown normalises
               to Off, and VIA renders the echo rather than the request. */
            value_data[0] = era_rgb_indicator_get_source(slot);
            return true;
        case ERA_RGB_INDICATOR_VIA_FIELD_BRIGHTNESS:
            era_rgb_indicator_set_brightness(slot, value_data[0]);
            return true;
        case ERA_RGB_INDICATOR_VIA_FIELD_COLOR:
            era_rgb_indicator_set_color(slot, value_data);
            return true;
        default:
            return false;
    }
}

void era_rgb_indicator_via_save(void) {
    era_rgb_indicator_save_config();
}

bool era_rgb_indicator_via_handle_via_command(uint8_t *data, uint8_t length) {
    (void)length;

    if (!data || data[1] != id_custom_channel || !era_rgb_indicator_via_is_value_id(data[2])) {
        return false;
    }

    uint8_t *command_id        = &data[0];
    uint8_t *value_id_and_data = &data[2];

    switch (*command_id) {
        case id_custom_set_value:
            return era_rgb_indicator_via_set_value(value_id_and_data);
        case id_custom_get_value:
            return era_rgb_indicator_via_get_value(value_id_and_data);
        default:
            /* `id_custom_save` carries no value id and never reaches here; the
               router's save arm calls era_rgb_indicator_via_save() directly. */
            return false;
    }
}
