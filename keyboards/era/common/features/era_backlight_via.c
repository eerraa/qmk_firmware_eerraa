// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_backlight_via.h"

#include "backlight.h"
#include "quantum.h"
#include "via.h"
#include "era_backlight.h"
#include "../storage/era_eeprom_layout.h"
#include "../system/era_state_sync.h"

/* Not on `era_common_via.h`'s shared value-command rail, and the reason is the
 * rail's own: it answers a set by calling the setter and then the getter, and
 * brightness here is QMK's state rather than this feature's. The rail would be
 * correct; it would just be a second indirection over four ids that already
 * read as a table. The three units that are genuinely off the rail for a
 * behavioural reason are named at `era_common_via.h`. */

bool era_backlight_via_is_value_id(uint8_t value_id) {
    return value_id <= ERA_VIA_BACKLIGHT_BLINK_SPEED_VALUE_ID;
}

static bool era_backlight_via_get_value(uint8_t *value_id_and_data) {
    uint8_t *value_id   = &value_id_and_data[0];
    uint8_t *value_data = &value_id_and_data[1];

    switch (*value_id) {
        case ERA_VIA_BACKLIGHT_BRIGHTNESS_VALUE_ID:
            *value_data = get_backlight_level();
            return true;
        case ERA_VIA_BACKLIGHT_EFFECT_VALUE_ID:
            *value_data = era_backlight_get_effect();
            return true;
        case ERA_VIA_BACKLIGHT_BREATHING_PERIOD_VALUE_ID:
            *value_data = era_backlight_get_breathing_period();
            return true;
        case ERA_VIA_BACKLIGHT_BLINK_SPEED_VALUE_ID:
            *value_data = era_backlight_get_blink_speed();
            return true;
        default:
            return false;
    }
}

static bool era_backlight_via_set_value(uint8_t *value_id_and_data) {
    uint8_t *value_id   = &value_id_and_data[0];
    uint8_t *value_data = &value_id_and_data[1];

    switch (*value_id) {
        case ERA_VIA_BACKLIGHT_BRIGHTNESS_VALUE_ID: {
            /* `_noeeprom`, with the write deferred to the save arm below, for
               the reason VIA's own backlight handler does the same: a client
               dragging the slider sends a set per step, and the eeprom-writing
               form would put a flash write behind each one. */
            uint8_t previous = get_backlight_level();
            backlight_level_noeeprom(value_data[0]);
            if (previous != get_backlight_level()) {
                era_state_sync_note_config_semantic_commit(ERA_EEPROM_BACKLIGHT_CONFIG_OFFSET, ERA_EEPROM_BACKLIGHT_CONFIG_SIZE);
            }
            return true;
        }
        case ERA_VIA_BACKLIGHT_EFFECT_VALUE_ID:
            era_backlight_set_effect(value_data[0]);
            /* Echo what the effect became. An unsupported entry -- breathing on
               a board built without the table -- is refused rather than stored,
               and the client should render the refusal instead of its own
               request. */
            value_data[0] = era_backlight_get_effect();
            return true;
        case ERA_VIA_BACKLIGHT_BREATHING_PERIOD_VALUE_ID:
            era_backlight_set_breathing_period(value_data[0]);
            return true;
        case ERA_VIA_BACKLIGHT_BLINK_SPEED_VALUE_ID:
            era_backlight_set_blink_speed(value_data[0]);
            return true;
        default:
            return false;
    }
}

void era_backlight_via_save(void) {
    /* Two owners, one save: the level is QMK's eeconfig and the three effect
       bytes are the ERA config block. A client sends one `id_custom_save` for
       the menu, so both have to land here or the half that did not is lost at
       the next boot. */
    eeconfig_update_backlight_current();
    era_backlight_save_config();
}

bool era_backlight_via_handle_via_command(uint8_t *data, uint8_t length) {
    (void)length;

    if (!data || data[1] != id_custom_channel || !era_backlight_via_is_value_id(data[2])) {
        return false;
    }

    uint8_t *command_id        = &data[0];
    uint8_t *value_id_and_data = &data[2];

    switch (*command_id) {
        case id_custom_set_value:
            return era_backlight_via_set_value(value_id_and_data);
        case id_custom_get_value:
            return era_backlight_via_get_value(value_id_and_data);
        default:
            /* `id_custom_save` carries no value id, so it never reaches here --
               the caller's save arm calls `era_backlight_via_save()` directly,
               beside tap dance's, and that is the only path that persists. */
            return false;
    }
}
