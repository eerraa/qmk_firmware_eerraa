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
#ifdef VIA_ENABLE
#    include "../system/era_state_sync.h"
#endif

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
    /* `kkuk_pulse_state` is deliberately left alone. A pulse that has already
       sent the empty report owes the host its restore whatever the config
       change was, and the state has only IDLE and RESTORE in it -- so there is
       nothing here for a config change to set it to that it is not already. */
    kkuk_repeat_requested = false;
    kkuk_mode_active      = false;
    kkuk_repeat_timer     = timer_read32();
    kkuk_delay_timer      = timer_read32();
    kkuk_key_count        = 0;
    kkuk_previous_count   = 0;
    kkuk_last_pulse_ms    = 0;
    kkuk_pulse_gap_active = false;
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

/* The pulse has to snapshot whichever report `clear_keys()` is about to empty,
   and that is not always the 6KRO one: `clear_keys_from_report()`
   (`tmk_core/protocol/report.c`) clears `nkro_report->bits` while NKRO is
   negotiated and `keyboard_report->keys` otherwise. A 6KRO-only snapshot left
   the NKRO restore with nothing to put back, and `send_nkro_report()`
   (`quantum/action_util.c`) then dropped that restore as unchanged against the
   empty report the pulse had just sent -- so every held key stayed released at
   the host until it was pressed again.

   Both formats are saved rather than the active one, because the predicate is
   QMK's and a second copy of it here could disagree with the copy
   `clear_keys()` reads. Writing back the format that was not cleared restores
   its own bytes and changes nothing. Only the key arrays move: `clear_keys()`
   leaves mods alone and every send recomputes them. */
static void era_kkuk_start_empty_pulse(void) {
    uint8_t saved_keys[KEYBOARD_REPORT_KEYS];
    memcpy(saved_keys, keyboard_report->keys, sizeof(saved_keys));
#ifdef NKRO_ENABLE
    uint8_t saved_bits[NKRO_REPORT_BITS];
    memcpy(saved_bits, nkro_report->bits, sizeof(saved_bits));
#endif

    clear_keys();
    send_keyboard_report();

    /* Keep QMK's internal report current; only the host restore is deferred. */
    memcpy(keyboard_report->keys, saved_keys, sizeof(saved_keys));
#ifdef NKRO_ENABLE
    memcpy(nkro_report->bits, saved_bits, sizeof(saved_bits));
#endif

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

static void era_kkuk_publish_runtime_change(void) {
    era_kkuk_reset_runtime();
#ifdef VIA_ENABLE
    era_state_sync_note_config_semantic_commit(ERA_EEPROM_KKUK_CONFIG_OFFSET, sizeof(kkuk_config));
#endif
}

void era_kkuk_set_enabled(bool enabled) {
    uint8_t next = enabled ? 1 : 0;
    if (kkuk_config.enable == next) {
        return;
    }
    kkuk_config.enable = next;
    era_kkuk_publish_runtime_change();
}

void era_kkuk_set_delay_ticks(uint8_t delay_ticks) {
    uint8_t next = era_kkuk_clamp_ticks(delay_ticks, ERA_KKUK_DELAY_TICKS_MIN, ERA_KKUK_DELAY_TICKS_MAX);
    if (kkuk_config.delay_time == next) {
        return;
    }
    kkuk_config.delay_time = next;
    era_kkuk_publish_runtime_change();
}

void era_kkuk_set_repeat_ticks(uint8_t repeat_ticks) {
    uint8_t next = era_kkuk_clamp_ticks(repeat_ticks, ERA_KKUK_REPEAT_TICKS_MIN, ERA_KKUK_REPEAT_TICKS_MAX);
    if (kkuk_config.repeat_time == next) {
        return;
    }
    kkuk_config.repeat_time = next;
    era_kkuk_publish_runtime_change();
}

void era_kkuk_set_mode(uint8_t mode) {
    if (mode == ERA_KKUK_MODE_REPORT_PULSE) {
        if (kkuk_config.mode == ERA_KKUK_MODE_REPORT_PULSE) {
            return;
        }
        kkuk_config.mode = ERA_KKUK_MODE_REPORT_PULSE;
        era_kkuk_publish_runtime_change();
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
