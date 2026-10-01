// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_lighting_sleep.h"

#include <string.h>
#include "era_lighting_sleep_policy.h"
#include "../storage/era_eeprom_storage.h"
#ifdef ERA_RGB_SLEEP_MASTER_ENABLE
#    include "era_rgb_sleep.h"
#endif
#ifdef VIA_ENABLE
#    include "../system/era_common_via.h"
#    include "../system/era_state_sync.h"
#    include "../system/era_via_system.h"
#    include "via.h"
#endif
#ifndef ERA_LIGHTING_SLEEP_TEST
#    ifdef RGB_MATRIX_ENABLE
#        include "rgb_matrix.h"
#    endif
#    ifdef RGBLIGHT_ENABLE
#        include "rgblight.h"
#    endif
#    ifdef ERA_BACKLIGHT_EFFECT_ENABLE
#        include "era_backlight.h"
#    elif defined(ERA_BACKLIGHT_SLEEP_ENABLE)
#        include "backlight.h"
#    endif
#endif

#define ERA_LIGHTING_SLEEP_BACKLIGHT_OFF (1U << 0)

typedef struct __attribute__((packed)) {
    uint16_t rgb_timeout_seconds;
    uint16_t backlight_timeout_seconds;
    uint8_t  flags;
    uint8_t  reserved[3];
} era_lighting_sleep_config_t;

_Static_assert(sizeof(era_lighting_sleep_config_t) == ERA_EEPROM_LIGHTING_SLEEP_CONFIG_SIZE, "ERA lighting sleep config size changed.");

static era_lighting_sleep_config_t sleep_config;

static bool era_lighting_sleep_config_is_valid(const era_lighting_sleep_config_t *config) {
    if ((config->flags & (uint8_t)~ERA_LIGHTING_SLEEP_BACKLIGHT_OFF) != 0) {
        return false;
    }
    for (uint8_t i = 0; i < sizeof(config->reserved); i++) {
        if (config->reserved[i] != 0) {
            return false;
        }
    }
    return true;
}

static uint16_t era_lighting_sleep_seconds(uint16_t stored) {
    return stored == 0 ? ERA_LIGHTING_SLEEP_DEFAULT_SECONDS : stored;
}

/* SET persists at once, like the RGB switch beside it on the SYSTEM channel. */
static void era_lighting_sleep_commit(void) {
#ifdef VIA_ENABLE
    era_state_sync_note_config_semantic_commit(ERA_EEPROM_LIGHTING_SLEEP_CONFIG_OFFSET, sizeof(sleep_config));
#endif
    era_eeprom_update_config(&sleep_config, ERA_EEPROM_LIGHTING_SLEEP_CONFIG_OFFSET, sizeof(sleep_config));
}

void era_lighting_sleep_reload_from_eeprom(void) {
    if (era_eeprom_read_config(&sleep_config, ERA_EEPROM_LIGHTING_SLEEP_CONFIG_OFFSET, sizeof(sleep_config)) != sizeof(sleep_config) || !era_lighting_sleep_config_is_valid(&sleep_config)) {
        memset(&sleep_config, 0, sizeof(sleep_config));
    }
}

void era_lighting_sleep_init(void) {
    era_lighting_sleep_reload_from_eeprom();
}

uint16_t era_lighting_sleep_rgb_timeout_seconds(void) {
    return era_lighting_sleep_seconds(sleep_config.rgb_timeout_seconds);
}

bool era_lighting_sleep_set_rgb_timeout_seconds(uint16_t seconds) {
    if (seconds == 0) {
        return false;
    }
    if (era_lighting_sleep_rgb_timeout_seconds() != seconds) {
        sleep_config.rgb_timeout_seconds = seconds;
        era_lighting_sleep_commit();
    }
    return true;
}

bool era_lighting_sleep_backlight_enabled(void) {
    return (sleep_config.flags & ERA_LIGHTING_SLEEP_BACKLIGHT_OFF) == 0;
}

bool era_lighting_sleep_set_backlight_enabled(bool enabled) {
    if (era_lighting_sleep_backlight_enabled() != enabled) {
        sleep_config.flags = enabled ? 0 : ERA_LIGHTING_SLEEP_BACKLIGHT_OFF;
        era_lighting_sleep_commit();
    }
    return true;
}

uint16_t era_lighting_sleep_backlight_timeout_seconds(void) {
    return era_lighting_sleep_seconds(sleep_config.backlight_timeout_seconds);
}

bool era_lighting_sleep_set_backlight_timeout_seconds(uint16_t seconds) {
    if (seconds == 0) {
        return false;
    }
    if (era_lighting_sleep_backlight_timeout_seconds() != seconds) {
        sleep_config.backlight_timeout_seconds = seconds;
        era_lighting_sleep_commit();
    }
    return true;
}

#ifndef SPLIT_KEYBOARD
#    ifdef ERA_LIGHTING_SLEEP_TEST
void era_lighting_sleep_test_rgb_gate(bool sleep);
void era_lighting_sleep_test_backlight_gate(bool sleep);
#    endif

#    ifdef ERA_RGB_SLEEP_MASTER_ENABLE
/* An output gate, never a config change: a VIA save during sleep stores the
   user's lighting, not an off state. */
static void era_lighting_sleep_apply_rgb(bool sleep) {
#        if defined(ERA_LIGHTING_SLEEP_TEST)
    era_lighting_sleep_test_rgb_gate(sleep);
#        else
#            ifdef RGB_MATRIX_ENABLE
    if (rgb_matrix_get_suspend_state() != sleep) {
        rgb_matrix_set_suspend_state(sleep);
    }
#            endif
#            ifdef RGBLIGHT_ENABLE
    rgblight_set_output_suspend_state(sleep);
#            endif
#        endif
}
#    endif

#    ifdef ERA_BACKLIGHT_SLEEP_ENABLE
static bool backlight_asleep;
#        if !defined(ERA_LIGHTING_SLEEP_TEST) && !defined(ERA_BACKLIGHT_EFFECT_ENABLE)
static uint8_t backlight_seen;

static uint8_t era_lighting_sleep_backlight_state(void) {
    uint8_t state = get_backlight_level() & 0x3F;
    if (is_backlight_enabled()) {
        state |= 0x80;
    }
#            ifdef BACKLIGHT_BREATHING
    if (is_backlight_breathing()) {
        state |= 0x40;
    }
#            endif
    return state;
}
#        endif

/* The whole PWM rail goes dark; the stored level stays the user's. Boards with
   the ERA effect engine gate it there. On the others a brightness change made
   while asleep writes the PWM, so the gate darkens it again when it sees one. */
static void era_lighting_sleep_apply_backlight(bool sleep) {
#        if defined(ERA_LIGHTING_SLEEP_TEST)
    era_lighting_sleep_test_backlight_gate(sleep);
    backlight_asleep = sleep;
#        elif defined(ERA_BACKLIGHT_EFFECT_ENABLE)
    if (backlight_asleep != sleep) {
        backlight_asleep = sleep;
        era_backlight_set_sleep(sleep);
    }
#        else
    if (sleep) {
        uint8_t state = era_lighting_sleep_backlight_state();
        if (!backlight_asleep || state != backlight_seen) {
#            ifdef BACKLIGHT_BREATHING
            if (is_breathing()) {
                breathing_disable();
            }
#            endif
            backlight_set(0);
            backlight_seen = state;
        }
    } else if (backlight_asleep) {
        backlight_set(is_backlight_enabled() ? get_backlight_level() : 0);
#            ifdef BACKLIGHT_BREATHING
        if (is_backlight_breathing()) {
            breathing_enable();
        }
#            endif
    }
    backlight_asleep = sleep;
#        endif
}
#    endif

void era_lighting_sleep_update(bool explicit_suspend, bool frames_lost, uint32_t matrix_idle_ms) {
#    ifdef ERA_RGB_SLEEP_MASTER_ENABLE
    era_lighting_sleep_apply_rgb(era_lighting_sleep_policy_requested(explicit_suspend, frames_lost, era_rgb_sleep_enabled(), era_lighting_sleep_rgb_timeout_seconds(), matrix_idle_ms));
#    endif
#    ifdef ERA_BACKLIGHT_SLEEP_ENABLE
    era_lighting_sleep_apply_backlight(era_lighting_sleep_policy_requested(explicit_suspend, frames_lost, era_lighting_sleep_backlight_enabled(), era_lighting_sleep_backlight_timeout_seconds(), matrix_idle_ms));
#    endif
    (void)explicit_suspend;
    (void)frames_lost;
    (void)matrix_idle_ms;
}
#endif

#ifdef VIA_ENABLE
static bool era_lighting_sleep_value_id_is_ours(uint8_t value_id) {
    switch (value_id) {
#    ifdef ERA_RGB_SLEEP_MASTER_ENABLE
        case ERA_VIA_SYSTEM_RGB_SLEEP_PRESET_VALUE_ID:
        case ERA_VIA_SYSTEM_RGB_SLEEP_EXACT_SECONDS_VALUE_ID:
            return true;
#    endif
#    ifdef ERA_BACKLIGHT_SLEEP_ENABLE
        case ERA_VIA_SYSTEM_BACKLIGHT_SLEEP_ENABLE_VALUE_ID:
        case ERA_VIA_SYSTEM_BACKLIGHT_SLEEP_PRESET_VALUE_ID:
        case ERA_VIA_SYSTEM_BACKLIGHT_SLEEP_EXACT_SECONDS_VALUE_ID:
            return true;
#    endif
        default:
            return false;
    }
}

static void era_lighting_sleep_via_get(uint8_t value_id, uint8_t *value) {
    switch (value_id) {
        case ERA_VIA_SYSTEM_RGB_SLEEP_PRESET_VALUE_ID:
            value[0] = era_lighting_sleep_policy_preset_minutes(era_lighting_sleep_rgb_timeout_seconds());
            break;
        case ERA_VIA_SYSTEM_RGB_SLEEP_EXACT_SECONDS_VALUE_ID:
            era_common_via_put_u16_be(value, era_lighting_sleep_rgb_timeout_seconds());
            break;
        case ERA_VIA_SYSTEM_BACKLIGHT_SLEEP_ENABLE_VALUE_ID:
            value[0] = era_lighting_sleep_backlight_enabled() ? 1 : 0;
            break;
        case ERA_VIA_SYSTEM_BACKLIGHT_SLEEP_PRESET_VALUE_ID:
            value[0] = era_lighting_sleep_policy_preset_minutes(era_lighting_sleep_backlight_timeout_seconds());
            break;
        case ERA_VIA_SYSTEM_BACKLIGHT_SLEEP_EXACT_SECONDS_VALUE_ID:
            era_common_via_put_u16_be(value, era_lighting_sleep_backlight_timeout_seconds());
            break;
        default:
            break;
    }
}

static bool era_lighting_sleep_via_set(uint8_t value_id, const uint8_t *value) {
    switch (value_id) {
        case ERA_VIA_SYSTEM_RGB_SLEEP_PRESET_VALUE_ID:
            return era_lighting_sleep_policy_preset_valid(value[0]) && era_lighting_sleep_set_rgb_timeout_seconds((uint16_t)value[0] * 60U);
        case ERA_VIA_SYSTEM_RGB_SLEEP_EXACT_SECONDS_VALUE_ID:
            return era_lighting_sleep_set_rgb_timeout_seconds(era_common_via_get_u16_be(value));
        case ERA_VIA_SYSTEM_BACKLIGHT_SLEEP_ENABLE_VALUE_ID:
            return era_lighting_sleep_set_backlight_enabled(value[0] != 0);
        case ERA_VIA_SYSTEM_BACKLIGHT_SLEEP_PRESET_VALUE_ID:
            return era_lighting_sleep_policy_preset_valid(value[0]) && era_lighting_sleep_set_backlight_timeout_seconds((uint16_t)value[0] * 60U);
        case ERA_VIA_SYSTEM_BACKLIGHT_SLEEP_EXACT_SECONDS_VALUE_ID:
            return era_lighting_sleep_set_backlight_timeout_seconds(era_common_via_get_u16_be(value));
        default:
            return false;
    }
}

/* SYSTEM channel ids 10, 11 and 13..15; the RGB switch (12) is era_rgb_sleep.c.
   A value a stock or app control could not have sent is refused unchanged. */
bool era_lighting_sleep_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || length < 5 || data[1] != ERA_VIA_SYSTEM_CHANNEL || !era_lighting_sleep_value_id_is_ours(data[2])) {
        return false;
    }
    switch (data[0]) {
        case id_custom_set_value:
            if (!era_lighting_sleep_via_set(data[2], &data[3])) {
                return false;
            }
            era_lighting_sleep_via_get(data[2], &data[3]);
            return true;
        case id_custom_get_value:
            era_lighting_sleep_via_get(data[2], &data[3]);
            return true;
        default:
            return false;
    }
}
#endif
