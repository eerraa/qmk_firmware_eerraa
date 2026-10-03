// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "era_mousekey.h"
#include <stddef.h>
#include <string.h>
#include "mousekey.h"
#include "../storage/era_eeprom_storage.h"
#ifdef VIA_ENABLE
#    include "../system/era_state_sync.h"
#endif
#if !defined(ERA_MOUSEKEY_RUNTIME_DELTA)
#    error ERA mouse settings require ERA_MOUSEKEY_RUNTIME_DELTA
#endif
#define ERA_MOUSEKEY_SIGNATURE 0x4B53554DUL
#define ERA_MOUSEKEY_VERSION 2U

/* Persisted MOUSE v2: integer report counts and real millisecond durations.
 * v1 is rejected, and the release reset key invalidates the complete store. */
typedef struct __attribute__((packed)) {
    uint16_t cursor_ramp_ms;
    uint16_t wheel_ramp_ms;
    uint8_t interval;
    uint8_t move_delta;
    uint8_t max_speed;
    uint8_t wheel_interval;
    uint8_t wheel_max_speed;
    uint8_t wheel_delta;
    uint8_t version;
    uint8_t reserved;
    uint32_t signature;
} era_mousekey_config_t;
_Static_assert(sizeof(era_mousekey_config_t) == 16, "MOUSE storage size");
_Static_assert(offsetof(era_mousekey_config_t, version) == 10, "MOUSE version offset");
_Static_assert(offsetof(era_mousekey_config_t, signature) == 12, "MOUSE signature offset");

static era_mousekey_config_t cfg;

static uint8_t clamp_byte(uint8_t value, uint8_t maximum) {
    return value < 1 ? 1 : value > maximum ? maximum : value;
}
static void defaults(void) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.cursor_ramp_ms = 1000;
    cfg.wheel_ramp_ms = MOUSEKEY_WHEEL_TIME_TO_MAX * MOUSEKEY_WHEEL_INTERVAL;
    cfg.interval = 10;
    cfg.move_delta = 4;
    cfg.max_speed = 16;
    cfg.wheel_interval = MOUSEKEY_WHEEL_INTERVAL;
    cfg.wheel_delta = MOUSEKEY_WHEEL_DELTA;
    cfg.wheel_max_speed = MOUSEKEY_WHEEL_DELTA * MOUSEKEY_WHEEL_MAX_SPEED;
    cfg.version = ERA_MOUSEKEY_VERSION;
    cfg.signature = ERA_MOUSEKEY_SIGNATURE;
}
static void normalize(void) {
    cfg.interval = clamp_byte(cfg.interval, 255);
    cfg.wheel_interval = clamp_byte(cfg.wheel_interval, 255);
    cfg.move_delta = clamp_byte(cfg.move_delta, MOUSEKEY_MOVE_MAX);
    cfg.max_speed = clamp_byte(cfg.max_speed, MOUSEKEY_MOVE_MAX);
    cfg.wheel_delta = clamp_byte(cfg.wheel_delta, MOUSEKEY_WHEEL_MAX);
    cfg.wheel_max_speed = clamp_byte(cfg.wheel_max_speed, MOUSEKEY_WHEEL_MAX);
    cfg.reserved = 0;
}
static void apply_runtime(void) {
    mk_delay = MOUSEKEY_DELAY / 10;
    mk_wheel_delay = MOUSEKEY_WHEEL_DELAY / 10;
    mk_interval = cfg.interval;
    mk_wheel_interval = cfg.wheel_interval;
    mk_move_delta = cfg.move_delta;
    mk_wheel_delta = cfg.wheel_delta;
    mk_cursor_top = cfg.max_speed;
    mk_wheel_top = cfg.wheel_max_speed;
    mk_cursor_ramp_ms = cfg.cursor_ramp_ms;
    mk_wheel_ramp_ms = cfg.wheel_ramp_ms;
    /* Legacy runtime variables are retained for unrelated QMK consumers. */
    mk_max_speed = cfg.cursor_ramp_ms ? (cfg.max_speed + cfg.move_delta / 2) / cfg.move_delta : 1;
    mk_wheel_max_speed = cfg.wheel_ramp_ms ? (cfg.wheel_max_speed + cfg.wheel_delta / 2) / cfg.wheel_delta : 1;
    mk_time_to_max = cfg.cursor_ramp_ms ? 1 : 0;
    mk_wheel_time_to_max = cfg.wheel_ramp_ms ? 1 : 0;
}
static uint16_t nearest(uint16_t value, const uint16_t *choices, uint8_t count) {
    uint16_t best = choices[0];
    uint16_t distance = value > best ? value - best : best - value;
    for (uint8_t i = 1; i < count; ++i) {
        uint16_t d = value > choices[i] ? value - choices[i] : choices[i] - value;
        if (d < distance) { best = choices[i]; distance = d; }
    }
    return best;
}
static uint8_t legacy_get(uint8_t id) {
    static const uint16_t constant[] = {2,4,6,8,10,12,16,24};
    static const uint16_t start[] = {1,2,4,8};
    static const uint16_t top[] = {8,16,24,32,48,64};
    static const uint16_t ramp[] = {500,750,1000,1250,1500,2000};
    static const uint16_t cursor_interval[] = {5,8,10,16,20};
    static const uint16_t wheel_interval[] = {20,40,80,160};
    switch (id) {
        case 1: return cfg.cursor_ramp_ms ? nearest(cfg.move_delta,start,(sizeof(start) / sizeof(start[0]))) : nearest(cfg.move_delta,constant,(sizeof(constant) / sizeof(constant[0])));
        case 2: return nearest(cfg.max_speed,top,(sizeof(top) / sizeof(top[0])));
        case 3: return cfg.cursor_ramp_ms ? nearest(cfg.cursor_ramp_ms,ramp,(sizeof(ramp) / sizeof(ramp[0]))) / 50U : 0;
        case 4: return nearest(cfg.interval,cursor_interval,(sizeof(cursor_interval) / sizeof(cursor_interval[0])));
        case 5: return nearest(cfg.wheel_interval,wheel_interval,(sizeof(wheel_interval) / sizeof(wheel_interval[0])));
        case 6: return cfg.wheel_ramp_ms == 0 ? 0 : cfg.wheel_max_speed <= (4 + MOUSEKEY_WHEEL_DELTA * MOUSEKEY_WHEEL_MAX_SPEED) / 2 ? 1 : 2;
        default: return 0;
    }
}
static uint16_t exact_get(uint8_t id) {
    switch (id) {
        case 8: return cfg.move_delta;
        case 9: return cfg.max_speed;
        case 10: return cfg.cursor_ramp_ms;
        case 11: return cfg.interval;
        case 12: return cfg.wheel_interval;
        case 13: return cfg.wheel_max_speed;
        case 14: return cfg.wheel_ramp_ms;
        default: return 0;
    }
}
static bool assign(uint8_t id, uint16_t value) {
    switch (id) {
        case 1: cfg.move_delta = clamp_byte(value, MOUSEKEY_MOVE_MAX); break;
        case 2: cfg.max_speed = clamp_byte(value, MOUSEKEY_MOVE_MAX); break;
        case 3: cfg.cursor_ramp_ms = value * 50U; break;
        case 4: cfg.interval = clamp_byte(value, 255); break;
        case 5: cfg.wheel_interval = clamp_byte(value, 255); break;
        case 6:
            cfg.wheel_max_speed = value == 0 ? cfg.wheel_delta : value == 1 ? 4 : MOUSEKEY_WHEEL_DELTA * MOUSEKEY_WHEEL_MAX_SPEED;
            cfg.wheel_ramp_ms = value == 0 ? 0 : MOUSEKEY_WHEEL_TIME_TO_MAX * cfg.wheel_interval;
            break;
        case 8: if (value < 1 || value > MOUSEKEY_MOVE_MAX) return false; cfg.move_delta = value; break;
        case 9: if (value < 1 || value > MOUSEKEY_MOVE_MAX) return false; cfg.max_speed = value; break;
        case 10: cfg.cursor_ramp_ms = value; break;
        case 11: if (value < 1 || value > 255) return false; cfg.interval = value; break;
        case 12: if (value < 1 || value > 255) return false; cfg.wheel_interval = value; break;
        case 13: if (value < 1 || value > MOUSEKEY_WHEEL_MAX) return false; cfg.wheel_max_speed = value; break;
        case 14: cfg.wheel_ramp_ms = value; break;
        default: return false;
    }
    return true;
}

static bool set_value(uint8_t id, uint16_t value) {
    era_mousekey_config_t previous = cfg;
    if (!assign(id, value)) return false;
    apply_runtime();
#ifdef VIA_ENABLE
    if (memcmp(&previous, &cfg, sizeof(cfg)) != 0)
        era_state_sync_note_config_semantic_commit(ERA_EEPROM_MOUSEKEY_CONFIG_OFFSET, sizeof(cfg));
#else
    (void)previous;
#endif
    return true;
}
void era_mousekey_save_config(void) {
    era_eeprom_update_config(&cfg, ERA_EEPROM_MOUSEKEY_CONFIG_OFFSET, sizeof(cfg));
}
static void load(bool persist) {
    if (era_eeprom_read_config(&cfg, ERA_EEPROM_MOUSEKEY_CONFIG_OFFSET, sizeof(cfg)) != sizeof(cfg) || cfg.version != ERA_MOUSEKEY_VERSION || cfg.signature != ERA_MOUSEKEY_SIGNATURE) {
        defaults();
        if (persist) era_mousekey_save_config();
    } else {
        era_mousekey_config_t previous = cfg;
        normalize();
        if (persist && memcmp(&previous,&cfg,sizeof(cfg))) era_mousekey_save_config();
    }
    apply_runtime();
}
void era_mousekey_init(void) { load(true); }
void era_mousekey_reload_from_eeprom(void) { load(false); }
bool era_mousekey_set_exact(uint8_t id, uint16_t value) { return id >= 8 && id <= 14 && set_value(id,value); }
uint16_t era_mousekey_get_exact(uint8_t id) { return exact_get(id); }
void era_mousekey_set_cursor_min_speed(uint8_t value) { set_value(1,value); }
uint8_t era_mousekey_get_cursor_min_speed(void) { return legacy_get(1); }
void era_mousekey_set_cursor_max_speed(uint8_t value) { set_value(2,value); }
uint8_t era_mousekey_get_cursor_max_speed(void) { return legacy_get(2); }
void era_mousekey_set_cursor_acceleration(uint8_t value) { set_value(3,value); }
uint8_t era_mousekey_get_cursor_acceleration(void) { return legacy_get(3); }
void era_mousekey_set_cursor_interval_ms(uint8_t value) { set_value(4,value); }
uint8_t era_mousekey_get_cursor_interval_ms(void) { return legacy_get(4); }
void era_mousekey_set_wheel_interval_ms(uint8_t value) { set_value(5,value); }
uint8_t era_mousekey_get_wheel_interval_ms(void) { return legacy_get(5); }
void era_mousekey_set_wheel_acceleration(uint8_t value) { set_value(6,value); }
uint8_t era_mousekey_get_wheel_acceleration(void) { return legacy_get(6); }
