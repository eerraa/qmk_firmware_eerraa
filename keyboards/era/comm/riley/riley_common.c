// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "riley_common.h"

#include <string.h>

#include "keycode_config.h"
#include "rgblight.h"
#include "../../common/storage/era_eeprom_storage.h"
#include "../../common/system/era_nonsplit_board.h"

/* Riley is the one ERA product whose three RGBLight LEDs are simultaneously
   effect pixels and independently selectable lock indicators. RGBLight layers
   are the native post-effect overlay path, so this module adds no scan-rate or
   housekeeping render loop and needs no QMK-core indicator hook. */

#if defined(ERA_RGB_INDICATOR_ENABLE)
#    error Riley owns ERA_EEPROM_RGB_INDICATOR_CONFIG and must not enable the RGB Matrix indicator feature.
#endif

enum {
    RILEY_CONFIG_VALID_BIT        = 0x80,
    RILEY_CONFIG_INDICATOR_ONLY   = 0x40,
    RILEY_CONFIG_MODE_MASK        = 0x03,
    RILEY_CONFIG_MODE_BITS        = 2,
    RILEY_CONFIG_EEPROM_OFFSET    = ERA_EEPROM_RGB_INDICATOR_CONFIG_OFFSET,
    RILEY_CONFIG_EEPROM_END       = RILEY_CONFIG_EEPROM_OFFSET + sizeof(riley_config_t),
    RILEY_CONFIG_AVAILABLE_END    = ERA_EEPROM_KEYBOARD_CONFIG_OFFSET + ERA_EEPROM_KEYBOARD_CONFIG_SIZE,
};

_Static_assert(sizeof(HSV) == 3, "Riley EEPROM layout assumes three-byte HSV.");
_Static_assert(sizeof(riley_config_t) == 10, "Riley config must remain ten bytes.");
_Static_assert(RILEY_CONFIG_EEPROM_OFFSET == ERA_EEPROM_RGB_INDICATOR_CONFIG_OFFSET, "Riley config must start in the common lighting seat.");
_Static_assert(RILEY_CONFIG_EEPROM_END <= RILEY_CONFIG_AVAILABLE_END, "Riley config exceeds the lighting plus keyboard-config seats.");
_Static_assert(RILEY_CONFIG_EEPROM_END <= ERA_EEPROM_SYNCABLE_RESERVED_OFFSET, "Riley config must not consume the syncable reserve.");

riley_config_t g_riley_config;

static rgblight_segment_t riley_indicator_segments[RILEY_INDICATOR_SLOT_COUNT][2] = {
    {{0, 1, 0, 0, 0}, RGBLIGHT_END_SEGMENTS},
    {{1, 1, 0, 0, 0}, RGBLIGHT_END_SEGMENTS},
    {{2, 1, 0, 0, 0}, RGBLIGHT_END_SEGMENTS},
};

static const rgblight_segment_t *const riley_indicator_layers[] = {
    riley_indicator_segments[RILEY_INDICATOR_SLOT_1],
    riley_indicator_segments[RILEY_INDICATOR_SLOT_2],
    riley_indicator_segments[RILEY_INDICATOR_SLOT_3],
    NULL,
};

static uint8_t riley_indicator_mode(const riley_config_t *config, uint8_t slot) {
    if (slot >= RILEY_INDICATOR_SLOT_COUNT) {
        return RILEY_INDICATOR_RGB_EFFECT;
    }
    return (config->flags >> (slot * RILEY_CONFIG_MODE_BITS)) & RILEY_CONFIG_MODE_MASK;
}

static bool riley_indicator_only(const riley_config_t *config) {
    return (config->flags & RILEY_CONFIG_INDICATOR_ONLY) != 0;
}

static void riley_config_apply_defaults(riley_config_t *config) {
    memset(config, 0, sizeof(*config));
    config->flags                           = RILEY_CONFIG_VALID_BIT;
    config->indicator_hsv[0]               = (HSV){0, 255, 255};
    config->indicator_hsv[1]               = (HSV){170, 255, 255};
    config->indicator_hsv[2]               = (HSV){85, 255, 255};
}

static bool riley_config_is_valid(const riley_config_t *config) {
    return (config->flags & RILEY_CONFIG_VALID_BIT) != 0;
}

static void riley_write_config_to_eeprom(const riley_config_t *config) {
    era_eeprom_update_config(config, RILEY_CONFIG_EEPROM_OFFSET, sizeof(*config));
}

static void riley_read_config_from_eeprom(riley_config_t *config) {
    if (era_eeprom_read_config(config, RILEY_CONFIG_EEPROM_OFFSET, sizeof(*config)) != sizeof(*config) || !riley_config_is_valid(config)) {
        riley_config_apply_defaults(config);
        riley_write_config_to_eeprom(config);
    }
}

static bool riley_lock_active(uint8_t mode, led_t led_state) {
    switch (mode) {
        case RILEY_INDICATOR_CAPS_LOCK:
            return led_state.caps_lock;
        case RILEY_INDICATOR_SCROLL_LOCK:
            return led_state.scroll_lock;
        case RILEY_INDICATOR_NUM_LOCK:
            return led_state.num_lock;
        default:
            return false;
    }
}

static void riley_refresh_indicator_layers(led_t led_state) {
    for (uint8_t slot = 0; slot < RILEY_INDICATOR_SLOT_COUNT; slot++) {
        uint8_t mode = riley_indicator_mode(&g_riley_config, slot);
        if (mode == RILEY_INDICATOR_RGB_EFFECT) {
            rgblight_set_layer_state(slot, false);
            continue;
        }

        bool active = riley_lock_active(mode, led_state);
        if (!active && !riley_indicator_only(&g_riley_config)) {
            rgblight_set_layer_state(slot, false);
            continue;
        }

        rgblight_segment_t *segment = &riley_indicator_segments[slot][0];
        if (active) {
            HSV color    = g_riley_config.indicator_hsv[slot];
            segment->hue = color.h;
            segment->sat = color.s;
            segment->val = color.v;
        } else {
            segment->hue = 0;
            segment->sat = 0;
            segment->val = 0;
        }
        rgblight_set_layer_state(slot, true);
    }
}

void keyboard_post_init_kb(void) {
    rgblight_layers = riley_indicator_layers;

    /* Riley has no user-facing persistent "all RGB off" state. Repair a stale
       disabled QMK RGBLight config at boot, but never do this from LED updates:
       RGBLIGHT_SLEEP deliberately disables the same runtime flag on suspend. */
    if (!rgblight_is_enabled()) {
        rgblight_enable();
    }

    keyboard_post_init_user();
    riley_refresh_indicator_layers(host_keyboard_led_state());
}

bool led_update_kb(led_t led_state) {
    bool res = led_update_user(led_state);
    if (res) {
        /* Preserve the original Riley GP25 Caps Lock indicator independently
           of the three WS2812 overlay slots. */
        led_update_ports(led_state);
        riley_refresh_indicator_layers(led_state);
    }
    return res;
}

void era_board_config_load(void) {
    riley_read_config_from_eeprom(&g_riley_config);
}

void era_board_config_reset(void) {
    riley_config_apply_defaults(&g_riley_config);
    riley_write_config_to_eeprom(&g_riley_config);
}

bool era_board_process_record(uint16_t keycode, keyrecord_t *record) {
    (void)record;
    /* Riley's RGB rail also supplies its lock indicators. Do not let the
       generic underglow toggle create a persistent all-off state. Automatic
       suspend/host-loss darkness bypasses key processing and remains intact. */
    if (keycode == QK_UNDERGLOW_TOGGLE) {
        return false;
    }
    return true;
}

#ifdef VIA_ENABLE
#    include "../../common/system/era_state_sync.h"

static void riley_set_indicator_mode(riley_config_t *config, uint8_t slot, uint8_t mode) {
    if (slot >= RILEY_INDICATOR_SLOT_COUNT) {
        return;
    }
    uint8_t shift      = slot * RILEY_CONFIG_MODE_BITS;
    uint8_t normalized = mode <= RILEY_INDICATOR_NUM_LOCK ? mode : RILEY_INDICATOR_RGB_EFFECT;
    config->flags      = (config->flags & (uint8_t)~(RILEY_CONFIG_MODE_MASK << shift)) | ((normalized & RILEY_CONFIG_MODE_MASK) << shift);
}

static void riley_set_indicator_only(riley_config_t *config, bool enabled) {
    if (enabled) {
        config->flags |= RILEY_CONFIG_INDICATOR_ONLY;
    } else {
        config->flags &= (uint8_t)~RILEY_CONFIG_INDICATOR_ONLY;
    }
}

static bool riley_value_to_slot(uint8_t value_id, uint8_t *slot, uint8_t *field) {
    if (value_id < id_custom_riley_ind1_mode || value_id > id_custom_riley_ind3_color) {
        return false;
    }
    uint8_t relative = value_id - id_custom_riley_ind1_mode;
    *slot             = relative / 3;
    *field            = relative % 3;
    return *slot < RILEY_INDICATOR_SLOT_COUNT;
}

static void riley_get_color(const HSV *color, uint8_t *data) {
    data[0] = color->h;
    data[1] = color->s;
}

static void riley_set_color(HSV *color, const uint8_t *data) {
    color->h = data[0];
    color->s = data[1];
}

bool era_board_via_get_value(uint8_t *data) {
    uint8_t *value_data = &data[1];
    uint8_t  slot;
    uint8_t  field;

    if (riley_value_to_slot(data[0], &slot, &field)) {
        switch (field) {
            case 0:
                value_data[0] = riley_indicator_mode(&g_riley_config, slot);
                break;
            case 1:
                value_data[0] = g_riley_config.indicator_hsv[slot].v;
                break;
            case 2:
                riley_get_color(&g_riley_config.indicator_hsv[slot], value_data);
                break;
        }
        return true;
    }

    switch (data[0]) {
        case id_custom_riley_indicator_only:
            value_data[0] = riley_indicator_only(&g_riley_config) ? 1 : 0;
            return true;
        case id_custom_riley_velocikey_enable:
#    if defined(VELOCIKEY_ENABLE)
            value_data[0] = rgblight_velocikey_enabled() ? 1 : 0;
#    else
            value_data[0] = 0;
#    endif
            return true;
        default:
            return false;
    }
}

bool era_board_via_set_value(uint8_t *data) {
    uint8_t *value_data = &data[1];
    uint8_t  slot;
    uint8_t  field;
    bool     config_changed  = false;
    bool     runtime_changed = false;

    if (riley_value_to_slot(data[0], &slot, &field)) {
        riley_config_t previous = g_riley_config;
        switch (field) {
            case 0:
                riley_set_indicator_mode(&g_riley_config, slot, value_data[0]);
                break;
            case 1:
                g_riley_config.indicator_hsv[slot].v = value_data[0];
                break;
            case 2:
                riley_set_color(&g_riley_config.indicator_hsv[slot], value_data);
                break;
        }
        config_changed = memcmp(&previous, &g_riley_config, sizeof(previous)) != 0;
    } else {
        switch (data[0]) {
            case id_custom_riley_indicator_only: {
                bool previous = riley_indicator_only(&g_riley_config);
                riley_set_indicator_only(&g_riley_config, value_data[0] != 0);
                config_changed = previous != riley_indicator_only(&g_riley_config);
                break;
            }
            case id_custom_riley_velocikey_enable:
#    if defined(VELOCIKEY_ENABLE)
                if ((value_data[0] != 0) != rgblight_velocikey_enabled()) {
                    rgblight_velocikey_toggle();
                    runtime_changed = true;
                }
                value_data[0] = rgblight_velocikey_enabled() ? 1 : 0;
                break;
#    else
                return false;
#    endif
            default:
                return false;
        }
    }

    if (config_changed) {
        riley_refresh_indicator_layers(host_keyboard_led_state());
    }
    if (config_changed || runtime_changed) {
        era_state_sync_note_config_semantic_commit(RILEY_CONFIG_EEPROM_OFFSET, sizeof(g_riley_config));
    }
    return true;
}

void era_board_via_save(void) {
    riley_write_config_to_eeprom(&g_riley_config);
}
#endif

#ifdef RILEY_RGB_INDICATOR_TEST
uint8_t riley_indicator_mode_for_testing(uint8_t slot) {
    return riley_indicator_mode(&g_riley_config, slot);
}

bool riley_indicator_only_for_testing(void) {
    return riley_indicator_only(&g_riley_config);
}
#endif
