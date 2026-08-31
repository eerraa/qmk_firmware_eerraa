// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "odessey_common.h"

#include <string.h>
#include "quantum.h"
#include "keycode_config.h"
#include "rgblight.h"
#include "rgblight_drivers.h"
#include "../../common/storage/era_eeprom_storage.h"
#include "../../common/system/era_nonsplit_board.h"

/* The odessey family's whole board content, owned once for `odessey60h` and
   `odessey60s`. The two board files it replaces were string-identical after
   substituting the board token -- `diff` returned empty on both the `.c` and
   the `.h` -- so there is no per-board delta to parameterise and neither board
   keeps a `.c` at all.

   Family content rather than class content, which is why it is here and not in
   the common layer: an indicator on one LED with an underglow effect range
   behind it, three lock sources and an HSV, is this product's, and the next
   non-split ERA board shares none of it. What both boards get from the class
   layer instead is common/system/era_nonsplit_board.c, which owns
   housekeeping_task_kb, the init trio, process_record_kb and the one
   via_custom_value_command_kb; this unit reaches it through the weak hooks in
   era_nonsplit_board.h and defines the two QMK hooks the skeleton does not
   touch (keyboard_post_init_kb and led_update_kb) directly. */

enum {
    ODESSEY_INDICATOR_LED_INDEX = 0,
    ODESSEY_UNDERGLOW_LED_START = 1,
    ODESSEY_UNDERGLOW_LED_COUNT = 30
};

odessey_config_t g_odessey_config;

static HSV  odessey_cached_indicator_hsv;
static RGB  odessey_cached_indicator_rgb;
static bool odessey_indicator_rgb_valid;

_Static_assert(sizeof(odessey_config_t) == sizeof(uint32_t), "ODESSEY config size changed.");
_Static_assert(sizeof(odessey_config_t) <= ERA_EEPROM_KEYBOARD_CONFIG_SIZE, "ODESSEY config exceeds ERA keyboard EEPROM storage.");

static uint8_t odessey_normalized_indicator_mode(uint8_t mode) {
    return mode <= ODESSEY_INDICATOR_NUM_LOCK ? mode : ODESSEY_INDICATOR_OFF;
}

static void odessey_config_set_indicator_mode(odessey_config_t *config, uint8_t mode) {
    config->indicator_mode             = odessey_normalized_indicator_mode(mode);
    config->indicator_mode_initialized = true;
}

static bool odessey_indicator_enabled(void) {
    return g_odessey_config.indicator_mode != ODESSEY_INDICATOR_OFF;
}

static bool odessey_indicator_led_active(led_t led_state) {
    switch (g_odessey_config.indicator_mode) {
        case ODESSEY_INDICATOR_CAPS_LOCK:
            return led_state.caps_lock;
        case ODESSEY_INDICATOR_SCROLL_LOCK:
            return led_state.scroll_lock;
        case ODESSEY_INDICATOR_NUM_LOCK:
            return led_state.num_lock;
        default:
            return false;
    }
}

static void odessey_config_apply_defaults(odessey_config_t *config) {
    config->raw = 0;
    odessey_config_set_indicator_mode(config, ODESSEY_INDICATOR_CAPS_LOCK);
    config->indicator_hsv.h = 0;
    config->indicator_hsv.s = 0;
    config->indicator_hsv.v = 255;
}

static bool odessey_config_is_valid(const odessey_config_t *config) {
    return config->indicator_mode_initialized && config->indicator_mode <= ODESSEY_INDICATOR_NUM_LOCK;
}

static void write_odessey_config_to_eeprom(const odessey_config_t *config) {
    era_eeprom_update_config(config, ERA_EEPROM_KEYBOARD_CONFIG_OFFSET, sizeof(*config));
}

static void read_odessey_config_from_eeprom(odessey_config_t *config) {
    if (era_eeprom_read_config(config, ERA_EEPROM_KEYBOARD_CONFIG_OFFSET, sizeof(*config)) != sizeof(*config) || !odessey_config_is_valid(config)) {
        odessey_config_apply_defaults(config);
        write_odessey_config_to_eeprom(config);
    }
}

static RGB odessey_indicator_rgb(void) {
    if (!odessey_indicator_rgb_valid || odessey_cached_indicator_hsv.h != g_odessey_config.indicator_hsv.h || odessey_cached_indicator_hsv.s != g_odessey_config.indicator_hsv.s || odessey_cached_indicator_hsv.v != g_odessey_config.indicator_hsv.v) {
        odessey_cached_indicator_hsv = g_odessey_config.indicator_hsv;
        odessey_cached_indicator_rgb = hsv_to_rgb(odessey_cached_indicator_hsv);
        odessey_indicator_rgb_valid  = true;
    }
    return odessey_cached_indicator_rgb;
}

static void odessey_set_indicator_rgb(uint8_t r, uint8_t g, uint8_t b) {
    rgblight_driver.set_color(ODESSEY_INDICATOR_LED_INDEX, r, g, b);
    rgblight_driver.flush();
}

static void odessey_apply_indicator(void) {
    if (!odessey_indicator_enabled() || !odessey_indicator_led_active(host_keyboard_led_state())) {
        odessey_set_indicator_rgb(0, 0, 0);
        return;
    }

    RGB rgb = odessey_indicator_rgb();
    odessey_set_indicator_rgb(rgb.r, rgb.g, rgb.b);
}

void keyboard_post_init_kb(void) {
    rgblight_set_effect_range(ODESSEY_UNDERGLOW_LED_START, ODESSEY_UNDERGLOW_LED_COUNT);
    keyboard_post_init_user();
    odessey_apply_indicator();
}

bool led_update_kb(led_t led_state) {
    bool res = led_update_user(led_state);
    if (res) {
        led_update_ports(led_state);
        odessey_apply_indicator();
    }
    return res;
}

void era_board_config_load(void) {
    read_odessey_config_from_eeprom(&g_odessey_config);
}

void era_board_config_reset(void) {
    odessey_config_apply_defaults(&g_odessey_config);
    write_odessey_config_to_eeprom(&g_odessey_config);
}

#ifdef VIA_ENABLE
#include "../../common/system/era_state_sync.h"

static void odessey_set_color(HSV *color, const uint8_t *data) {
    color->h = data[0];
    color->s = data[1];
}

static void odessey_get_color(const HSV *color, uint8_t *data) {
    data[0] = color->h;
    data[1] = color->s;
}

bool era_board_via_get_value(uint8_t *data) {
    uint8_t *value_id   = &data[0];
    uint8_t *value_data = &data[1];

    switch (*value_id) {
        case id_custom_indicator_select:
            *value_data = g_odessey_config.indicator_mode;
            return true;
        case id_custom_indicator_brightness:
            *value_data = g_odessey_config.indicator_hsv.v;
            return true;
        case id_custom_indicator_color:
            odessey_get_color(&g_odessey_config.indicator_hsv, value_data);
            return true;
        case id_custom_velocikey_enable:
#if defined(VELOCIKEY_ENABLE)
            *value_data = rgblight_velocikey_enabled() ? 1 : 0;
            return true;
#else
            *value_data = 0;
            return true;
#endif
        default:
            return false;
    }
}

bool era_board_via_set_value(uint8_t *data) {
    uint8_t *value_id   = &data[0];
    uint8_t *value_data = &data[1];
    odessey_config_t previous = g_odessey_config;
    bool             runtime_changed = false;

    switch (*value_id) {
        case id_custom_indicator_select:
            odessey_config_set_indicator_mode(&g_odessey_config, *value_data);
            break;
        case id_custom_indicator_brightness:
            g_odessey_config.indicator_hsv.v = *value_data;
            break;
        case id_custom_indicator_color:
            odessey_set_color(&g_odessey_config.indicator_hsv, value_data);
            break;
        case id_custom_velocikey_enable:
#if defined(VELOCIKEY_ENABLE)
            if ((value_data[0] != 0) != rgblight_velocikey_enabled()) {
                rgblight_velocikey_toggle();
                runtime_changed = true;
            }
            value_data[0] = rgblight_velocikey_enabled() ? 1 : 0;
            break;
#else
            return false;
#endif
        default:
            return false;
    }

    bool config_changed = memcmp(&previous, &g_odessey_config, sizeof(previous)) != 0;
    if (config_changed) {
        odessey_apply_indicator();
    }
    if (runtime_changed || config_changed) {
        era_state_sync_note_config_semantic_commit(ERA_EEPROM_KEYBOARD_CONFIG_OFFSET, sizeof(g_odessey_config));
    }
    return true;
}

void era_board_via_save(void) {
    write_odessey_config_to_eeprom(&g_odessey_config);
}
#endif
