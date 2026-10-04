// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_socd.h"

#include <string.h>
#include "action_util.h"
#include "report.h"
#include "../storage/era_eeprom_storage.h"
#ifdef VIA_ENABLE
#    include "../system/era_state_sync.h"
#endif

typedef struct __attribute__((packed)) {
    uint8_t  enable;
    uint8_t  mode;
    uint16_t keycode[ERA_SOCD_PAIR_KEY_COUNT];
    uint8_t  reserved[2];
} era_socd_config_t;

_Static_assert(sizeof(era_socd_config_t) == ERA_EEPROM_SOCD_LR_CONFIG_SIZE, "ERA SOCD config size changed.");

static era_socd_config_t socd_config[ERA_SOCD_PAIR_COUNT];

/* SOCD resolves on the report about to reach the host, whoever put each usage
   there: a physical key, a Tap Dance, a macro. Per pair it remembers which of
   the two usages that report held last time (bit per key) and which one
   appeared most recently; -1 until one appears on its own. */
static uint8_t socd_seen[ERA_SOCD_PAIR_COUNT];
static int8_t  socd_last[ERA_SOCD_PAIR_COUNT];

static uint16_t era_socd_config_offset(uint8_t pair) {
    return pair == ERA_SOCD_PAIR_LR ? ERA_EEPROM_SOCD_LR_CONFIG_OFFSET : ERA_EEPROM_SOCD_UD_CONFIG_OFFSET;
}

static bool era_socd_mode_is_valid(uint8_t mode) {
    return mode >= ERA_SOCD_MODE_LAST_INPUT && mode <= ERA_SOCD_MODE_MAX;
}

/* The reserved bytes are part of the check: a block this firmware wrote always
   has them zero, so a stale block that happens to carry a legal mode byte still
   resets instead of being misread (era_host_peer_storage_contract.md,
   Stored-Data Compatibility). */
static bool era_socd_config_is_valid(const era_socd_config_t *config) {
    if (!config || !era_socd_mode_is_valid(config->mode)) {
        return false;
    }
    for (uint8_t i = 0; i < sizeof(config->reserved); i++) {
        if (config->reserved[i] != 0) {
            return false;
        }
    }
    return true;
}

static void era_socd_apply_defaults(era_socd_config_t *config) {
    memset(config, 0, sizeof(*config));
    config->mode = ERA_SOCD_MODE_LAST_INPUT;
}

static void era_socd_reset_pair_state(uint8_t pair) {
    if (pair < ERA_SOCD_PAIR_COUNT) {
        socd_seen[pair] = 0;
        socd_last[pair] = -1;
    }
}

static bool era_socd_keycode_can_report(uint16_t keycode) {
    return IS_BASIC_KEYCODE(keycode) || IS_MODIFIER_KEYCODE(keycode);
}

static bool era_socd_pair_shape_is_valid(const era_socd_config_t *config) {
    return config->enable && era_socd_keycode_can_report(config->keycode[0]) && era_socd_keycode_can_report(config->keycode[1]) && config->keycode[0] != config->keycode[1];
}

/* A key in both pairs has no single answer in the report, so both pairs stand down. */
static bool era_socd_pair_runs(uint8_t pair) {
    if (!era_socd_pair_shape_is_valid(&socd_config[pair])) {
        return false;
    }
    for (uint8_t other = 0; other < ERA_SOCD_PAIR_COUNT; other++) {
        if (other == pair || !era_socd_pair_shape_is_valid(&socd_config[other])) {
            continue;
        }
        for (uint8_t i = 0; i < ERA_SOCD_PAIR_KEY_COUNT; i++) {
            for (uint8_t j = 0; j < ERA_SOCD_PAIR_KEY_COUNT; j++) {
                if (socd_config[pair].keycode[i] == socd_config[other].keycode[j]) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* One view over the 6KRO array and the NKRO bitmap, so both reports resolve alike. */
typedef struct {
    uint8_t *mods;
    uint8_t *keys;
    uint8_t *bits;
} era_socd_report_t;

static bool era_socd_report_has(const era_socd_report_t *report, uint16_t keycode) {
    if (IS_MODIFIER_KEYCODE(keycode)) {
        return (*report->mods & MOD_BIT(keycode)) != 0;
    }
    uint8_t code = (uint8_t)keycode;
#ifdef NKRO_ENABLE
    if (report->bits) {
        return (code >> 3) < NKRO_REPORT_BITS && (report->bits[code >> 3] & (1 << (code & 7))) != 0;
    }
#endif
    for (uint8_t i = 0; i < KEYBOARD_REPORT_KEYS; i++) {
        if (report->keys[i] == code) {
            return true;
        }
    }
    return false;
}

static void era_socd_report_drop(era_socd_report_t *report, uint16_t keycode) {
    if (IS_MODIFIER_KEYCODE(keycode)) {
        *report->mods &= (uint8_t)~MOD_BIT(keycode);
        return;
    }
    uint8_t code = (uint8_t)keycode;
#ifdef NKRO_ENABLE
    if (report->bits) {
        if ((code >> 3) < NKRO_REPORT_BITS) {
            report->bits[code >> 3] &= (uint8_t)~(1 << (code & 7));
        }
        return;
    }
#endif
    for (uint8_t i = 0; i < KEYBOARD_REPORT_KEYS; i++) {
        if (report->keys[i] == code) {
            report->keys[i] = 0;
        }
    }
}

/* Which key of a pair holding both is reported; -1 reports neither. */
static int8_t era_socd_winner(uint8_t pair) {
    int8_t last = socd_last[pair];
    switch (socd_config[pair].mode) {
        case ERA_SOCD_MODE_LAST_INPUT:
            return last;
        case ERA_SOCD_MODE_FIRST_INPUT:
            return last < 0 ? -1 : (int8_t)(1 - last);
        case ERA_SOCD_MODE_KEY_0_PRIORITY:
            return 0;
        case ERA_SOCD_MODE_KEY_1_PRIORITY:
            return 1;
        default:
            return -1;
    }
}

static void era_socd_filter(era_socd_report_t *report) {
    for (uint8_t pair = 0; pair < ERA_SOCD_PAIR_COUNT; pair++) {
        if (!era_socd_pair_runs(pair)) {
            era_socd_reset_pair_state(pair);
            continue;
        }

        const era_socd_config_t *config = &socd_config[pair];
        uint8_t                  seen   = (era_socd_report_has(report, config->keycode[0]) ? 1 : 0) | (era_socd_report_has(report, config->keycode[1]) ? 2 : 0);
        uint8_t                  rose   = seen & (uint8_t)~socd_seen[pair];

        /* Both appearing in one report -- a KKUK restore, a tap that carries
           both -- is not an ordering, so the previous answer stands. */
        if (rose == 1) {
            socd_last[pair] = 0;
        } else if (rose == 2) {
            socd_last[pair] = 1;
        }
        socd_seen[pair] = seen;

        if (seen != 3) {
            continue;
        }
        int8_t winner = era_socd_winner(pair);
        for (uint8_t i = 0; i < ERA_SOCD_PAIR_KEY_COUNT; i++) {
            if ((int8_t)i != winner) {
                era_socd_report_drop(report, config->keycode[i]);
            }
        }
    }
}

void keyboard_report_filter(report_keyboard_t *report) {
    era_socd_report_t view = {.mods = &report->mods, .keys = report->keys, .bits = NULL};
    era_socd_filter(&view);
}

#ifdef NKRO_ENABLE
void nkro_report_filter(report_nkro_t *report) {
    era_socd_report_t view = {.mods = &report->mods, .keys = NULL, .bits = report->bits};
    era_socd_filter(&view);
}
#endif

/* A changed pair starts fresh and the host sees the result now: releasing a
   key the old setting hid needs no further input. */
static void era_socd_pair_changed(uint8_t pair) {
    era_socd_reset_pair_state(pair);
#ifdef VIA_ENABLE
    era_state_sync_note_config_semantic_commit(era_socd_config_offset(pair), sizeof(socd_config[pair]));
#endif
    send_keyboard_report();
}

bool era_socd_save_pair(uint8_t pair) {
    if (pair >= ERA_SOCD_PAIR_COUNT) {
        return false;
    }
    return era_eeprom_update_config(&socd_config[pair], era_socd_config_offset(pair), sizeof(socd_config[pair])) == sizeof(socd_config[pair]);
}

static bool era_socd_load_pair(uint8_t pair, bool write_defaults) {
    if (pair >= ERA_SOCD_PAIR_COUNT) {
        return false;
    }

    era_socd_config_t previous = socd_config[pair];

    if (era_eeprom_read_config(&socd_config[pair], era_socd_config_offset(pair), sizeof(socd_config[pair])) != sizeof(socd_config[pair]) || !era_socd_config_is_valid(&socd_config[pair])) {
        era_socd_apply_defaults(&socd_config[pair]);
        if (write_defaults) {
            era_socd_save_pair(pair);
        }
    } else if (socd_config[pair].enable > 1) {
        socd_config[pair].enable = 1;
    }

    return memcmp(&previous, &socd_config[pair], sizeof(socd_config[pair])) != 0;
}

void era_socd_init(void) {
    for (uint8_t pair = 0; pair < ERA_SOCD_PAIR_COUNT; pair++) {
        era_socd_load_pair(pair, true);
        era_socd_reset_pair_state(pair);
    }
}

void era_socd_reload_from_eeprom(void) {
    bool changed = false;
    for (uint8_t pair = 0; pair < ERA_SOCD_PAIR_COUNT; pair++) {
        if (era_socd_load_pair(pair, false)) {
            era_socd_reset_pair_state(pair);
            changed = true;
        }
    }
    if (changed) {
        send_keyboard_report();
    }
}

bool era_socd_is_bound_keycode(uint16_t keycode) {
    for (uint8_t pair = 0; pair < ERA_SOCD_PAIR_COUNT; pair++) {
        if (!era_socd_pair_runs(pair)) {
            continue;
        }
        for (uint8_t i = 0; i < ERA_SOCD_PAIR_KEY_COUNT; i++) {
            if (keycode == socd_config[pair].keycode[i]) {
                return true;
            }
        }
    }
    return false;
}

bool era_socd_set_enabled(uint8_t pair, bool enabled) {
    if (pair >= ERA_SOCD_PAIR_COUNT) {
        return false;
    }
    uint8_t next = enabled ? 1 : 0;
    if (socd_config[pair].enable != next) {
        socd_config[pair].enable = next;
        era_socd_pair_changed(pair);
    }
    return true;
}

bool era_socd_get_enabled(uint8_t pair) {
    if (pair >= ERA_SOCD_PAIR_COUNT) {
        return false;
    }
    return socd_config[pair].enable != 0;
}

bool era_socd_set_keycode(uint8_t pair, uint8_t key_index, uint16_t keycode) {
    if (pair >= ERA_SOCD_PAIR_COUNT || key_index >= ERA_SOCD_PAIR_KEY_COUNT) {
        return false;
    }
    if (socd_config[pair].keycode[key_index] != keycode) {
        socd_config[pair].keycode[key_index] = keycode;
        era_socd_pair_changed(pair);
    }
    return true;
}

uint16_t era_socd_get_keycode(uint8_t pair, uint8_t key_index) {
    if (pair >= ERA_SOCD_PAIR_COUNT || key_index >= ERA_SOCD_PAIR_KEY_COUNT) {
        return KC_NO;
    }
    return socd_config[pair].keycode[key_index];
}

/* An unknown mode leaves the setting as it is; the VIA answer then shows it. */
bool era_socd_set_mode(uint8_t pair, uint8_t mode) {
    if (pair >= ERA_SOCD_PAIR_COUNT) {
        return false;
    }
    if (era_socd_mode_is_valid(mode) && socd_config[pair].mode != mode) {
        socd_config[pair].mode = mode;
        era_socd_pair_changed(pair);
    }
    return true;
}

uint8_t era_socd_get_mode(uint8_t pair) {
    if (pair >= ERA_SOCD_PAIR_COUNT) {
        return 0;
    }
    return socd_config[pair].mode;
}
