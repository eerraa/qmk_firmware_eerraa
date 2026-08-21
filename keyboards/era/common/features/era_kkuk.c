// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_kkuk.h"

#include <string.h>
#include "action_util.h"
#include "timer.h"
#ifdef ERA_SOCD_ENABLE
#    include "era_socd.h"
#else
/* KKUK asks SOCD which keycodes it owns so it never repeats one. With SOCD
   compiled out no keycode is SOCD-bound, so the interlock has a constant answer
   rather than a missing one -- which is the difference between KKUK composing
   with the selector and KKUK failing the link at an unrelated caller. If a
   second consumer of this API appears, the fallback belongs in era_socd.h
   instead of here. */
static inline bool era_socd_is_bound_keycode(uint16_t keycode) {
    (void)keycode;
    return false;
}
#endif
#include "../storage/era_eeprom_storage.h"

enum {
    ERA_KKUK_TIME_UNIT_MS        = 10,
    ERA_KKUK_DELAY_TICKS_MIN    = 5,
    ERA_KKUK_DELAY_TICKS_MAX    = 30,
    ERA_KKUK_REPEAT_TICKS_MIN   = 5,
    ERA_KKUK_REPEAT_TICKS_MAX   = 20,
    ERA_KKUK_DEFAULT_DELAY      = 20,
    ERA_KKUK_DEFAULT_REPEAT     = 8,
    ERA_KKUK_PULSE_IDLE         = 0,
    ERA_KKUK_PULSE_RESTORE      = 1,
};

typedef struct __attribute__((packed)) {
    uint8_t enable;
    uint8_t mode;
    uint8_t repeat_time;
    uint8_t delay_time;
} era_kkuk_config_t;

_Static_assert(sizeof(era_kkuk_config_t) == ERA_EEPROM_KKUK_CONFIG_SIZE, "ERA KKUK config size changed.");

static era_kkuk_config_t kkuk_config;
static bool                kkuk_repeat_requested;
static bool                kkuk_mode_active;
static uint32_t            kkuk_repeat_timer;
static uint32_t            kkuk_delay_timer;
static uint8_t             kkuk_key_count;
static uint8_t             kkuk_previous_count;
static uint8_t             kkuk_pulse_state;
static uint32_t            kkuk_last_pulse_ms;
static bool                kkuk_pulse_gap_active;
static report_keyboard_t   kkuk_last_report;

static uint8_t era_kkuk_clamp_ticks(uint8_t value, uint8_t min, uint8_t max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static void era_kkuk_apply_defaults(era_kkuk_config_t *config) {
    memset(config, 0, sizeof(*config));
    config->mode        = ERA_KKUK_MODE_REPORT_PULSE;
    config->repeat_time = ERA_KKUK_DEFAULT_REPEAT;
    config->delay_time  = ERA_KKUK_DEFAULT_DELAY;
}

static bool era_kkuk_config_is_valid(const era_kkuk_config_t *config) {
    return config && config->mode == ERA_KKUK_MODE_REPORT_PULSE;
}

static bool era_kkuk_normalize_config(era_kkuk_config_t *config) {
    bool dirty = false;

    if (config->enable > 1) {
        config->enable = 1;
        dirty          = true;
    }

    uint8_t delay_time = era_kkuk_clamp_ticks(config->delay_time, ERA_KKUK_DELAY_TICKS_MIN, ERA_KKUK_DELAY_TICKS_MAX);
    if (delay_time != config->delay_time) {
        config->delay_time = delay_time;
        dirty              = true;
    }

    uint8_t repeat_time = era_kkuk_clamp_ticks(config->repeat_time, ERA_KKUK_REPEAT_TICKS_MIN, ERA_KKUK_REPEAT_TICKS_MAX);
    if (repeat_time != config->repeat_time) {
        config->repeat_time = repeat_time;
        dirty               = true;
    }

    return dirty;
}

static void era_kkuk_reset_runtime(void) {
    bool restore_pending = kkuk_pulse_state == ERA_KKUK_PULSE_RESTORE;

    kkuk_repeat_requested = false;
    kkuk_mode_active      = false;
    kkuk_repeat_timer     = timer_read32();
    kkuk_delay_timer      = timer_read32();
    kkuk_key_count        = 0;
    kkuk_previous_count   = 0;
    kkuk_last_pulse_ms    = 0;
    kkuk_pulse_gap_active = false;

    if (!restore_pending) {
        kkuk_pulse_state = ERA_KKUK_PULSE_IDLE;
        memset(&kkuk_last_report, 0, sizeof(kkuk_last_report));
    }
}

void era_kkuk_save_config(void) {
    era_eeprom_update_config(&kkuk_config, ERA_EEPROM_KKUK_CONFIG_OFFSET, sizeof(kkuk_config));
}

static bool era_kkuk_load_from_eeprom(bool write_defaults) {
    era_kkuk_config_t previous = kkuk_config;
    bool                dirty    = false;

    if (era_eeprom_read_config(&kkuk_config, ERA_EEPROM_KKUK_CONFIG_OFFSET, sizeof(kkuk_config)) != sizeof(kkuk_config) || !era_kkuk_config_is_valid(&kkuk_config)) {
        era_kkuk_apply_defaults(&kkuk_config);
        dirty = true;
    } else {
        dirty = era_kkuk_normalize_config(&kkuk_config);
    }

    if (write_defaults && dirty) {
        era_kkuk_save_config();
    }

    if (write_defaults || memcmp(&previous, &kkuk_config, sizeof(kkuk_config)) != 0) {
        era_kkuk_reset_runtime();
    }
    return true;
}

static bool era_kkuk_can_start_pulse(uint16_t repeat_ms) {
    return !kkuk_pulse_gap_active || timer_elapsed32(kkuk_last_pulse_ms) >= repeat_ms;
}

static void era_kkuk_start_empty_pulse(void) {
    memcpy(&kkuk_last_report, keyboard_report, sizeof(kkuk_last_report));
    clear_keys();
    send_keyboard_report();
    // Keep QMK's internal report current; only the host restore is deferred.
    memcpy(keyboard_report, &kkuk_last_report, sizeof(kkuk_last_report));
    kkuk_pulse_state = ERA_KKUK_PULSE_RESTORE;
}

static void era_kkuk_finish_restore_pulse(void) {
    send_keyboard_report();
    kkuk_last_pulse_ms    = timer_read32();
    kkuk_pulse_gap_active = true;
    kkuk_pulse_state      = ERA_KKUK_PULSE_IDLE;
}

void era_kkuk_init(void) {
    era_kkuk_load_from_eeprom(true);
}

void era_kkuk_reload_from_eeprom(void) {
    era_kkuk_load_from_eeprom(false);
}

void era_kkuk_task(void) {
    if (kkuk_pulse_state == ERA_KKUK_PULSE_RESTORE) {
        era_kkuk_finish_restore_pulse();
        return;
    }

    if (!kkuk_config.enable) {
        return;
    }

    uint16_t delay_ms  = (uint16_t)kkuk_config.delay_time * ERA_KKUK_TIME_UNIT_MS;
    uint16_t repeat_ms = (uint16_t)kkuk_config.repeat_time * ERA_KKUK_TIME_UNIT_MS;

    if (!kkuk_mode_active) {
        if (kkuk_key_count >= 2 && timer_elapsed32(kkuk_delay_timer) >= delay_ms) {
            kkuk_mode_active  = true;
            kkuk_repeat_timer = timer_read32() + ERA_KKUK_TIME_UNIT_MS;
        }
    } else if (kkuk_key_count == 0) {
        kkuk_mode_active      = false;
        kkuk_repeat_requested = false;
    }

    if (timer_elapsed32(kkuk_repeat_timer) < repeat_ms) {
        return;
    }

    if (kkuk_mode_active) {
        if (kkuk_key_count >= 2 || (kkuk_key_count == 1 && kkuk_previous_count == 2)) {
            kkuk_repeat_requested = true;
        }
    }

    kkuk_previous_count = kkuk_key_count;

    if (!kkuk_repeat_requested) {
        kkuk_repeat_timer = timer_read32();
        return;
    }

    if (!era_kkuk_can_start_pulse(repeat_ms)) {
        return;
    }

    kkuk_repeat_requested = false;
    kkuk_repeat_timer     = timer_read32();
    era_kkuk_start_empty_pulse();
}

bool era_kkuk_process_record(uint16_t keycode, keyrecord_t *record) {
    if (!record || !kkuk_config.enable || era_socd_is_bound_keycode(keycode)) {
        return true;
    }

    if (IS_BASIC_KEYCODE(keycode)) {
        if (record->event.pressed) {
            if (kkuk_key_count < UINT8_MAX) {
                kkuk_key_count++;
            }
        } else if (kkuk_key_count > 0) {
            kkuk_key_count--;
        }
        kkuk_delay_timer = timer_read32();
    }

    return true;
}

void era_kkuk_set_enabled(bool enabled) {
    kkuk_config.enable = enabled ? 1 : 0;
    era_kkuk_reset_runtime();
}

void era_kkuk_set_delay_ticks(uint8_t delay_ticks) {
    kkuk_config.delay_time = era_kkuk_clamp_ticks(delay_ticks, ERA_KKUK_DELAY_TICKS_MIN, ERA_KKUK_DELAY_TICKS_MAX);
    era_kkuk_reset_runtime();
}

void era_kkuk_set_repeat_ticks(uint8_t repeat_ticks) {
    kkuk_config.repeat_time = era_kkuk_clamp_ticks(repeat_ticks, ERA_KKUK_REPEAT_TICKS_MIN, ERA_KKUK_REPEAT_TICKS_MAX);
    era_kkuk_reset_runtime();
}

void era_kkuk_set_mode(uint8_t mode) {
    if (mode == ERA_KKUK_MODE_REPORT_PULSE) {
        kkuk_config.mode = ERA_KKUK_MODE_REPORT_PULSE;
        era_kkuk_reset_runtime();
    }
}

bool era_kkuk_get_enabled(void) {
    return kkuk_config.enable != 0;
}

uint8_t era_kkuk_get_delay_ticks(void) {
    return kkuk_config.delay_time;
}

uint8_t era_kkuk_get_repeat_ticks(void) {
    return kkuk_config.repeat_time;
}

uint8_t era_kkuk_get_mode(void) {
    return kkuk_config.mode;
}
