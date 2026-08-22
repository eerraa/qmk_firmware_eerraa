// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_socd.h"

#include <string.h>
#include "action_util.h"
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
static bool                socd_pressed[ERA_SOCD_PAIR_COUNT][ERA_SOCD_PAIR_KEY_COUNT];

static uint16_t era_socd_config_offset(uint8_t pair) {
    return pair == ERA_SOCD_PAIR_LR ? ERA_EEPROM_SOCD_LR_CONFIG_OFFSET : ERA_EEPROM_SOCD_UD_CONFIG_OFFSET;
}

/* The reserved bytes are part of the check and not decoration. This block is
   validated by its mode byte alone, which has one legal value, so any two bytes
   that happen to read {x, 1} pass -- and the 2026-08-18 regrouping moved this
   pair onto bytes an older image wrote as something else, where exactly that
   was reachable. A block this firmware wrote always has them zero: defaults
   memset the struct and no setter touches them. So the test costs a stored
   config nothing and turns a misread into a clean reset
   (era_source_map.md, Stored-Data Compatibility). */
static bool era_socd_config_is_valid(const era_socd_config_t *config) {
    if (!config || config->mode != ERA_SOCD_MODE_LAST_INPUT_WINS) {
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
    config->mode = ERA_SOCD_MODE_LAST_INPUT_WINS;
}

static void era_socd_reset_pair_state(uint8_t pair) {
    if (pair < ERA_SOCD_PAIR_COUNT) {
        memset(socd_pressed[pair], 0, sizeof(socd_pressed[pair]));
    }
}

void era_socd_save_pair(uint8_t pair) {
    if (pair >= ERA_SOCD_PAIR_COUNT) {
        return;
    }
    era_eeprom_update_config(&socd_config[pair], era_socd_config_offset(pair), sizeof(socd_config[pair]));
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
    }

    if (write_defaults || memcmp(&previous, &socd_config[pair], sizeof(socd_config[pair])) != 0) {
        era_socd_reset_pair_state(pair);
    }
    return true;
}

static bool era_socd_keycode_can_report(uint16_t keycode) {
    return IS_BASIC_KEYCODE(keycode) || IS_MODIFIER_KEYCODE(keycode);
}

static bool era_socd_pair_can_report(const era_socd_config_t *config) {
    return era_socd_keycode_can_report(config->keycode[0]) && era_socd_keycode_can_report(config->keycode[1]);
}

static void era_socd_add_report_key(uint16_t keycode) {
    if (IS_BASIC_KEYCODE(keycode)) {
        add_key((uint8_t)keycode);
    } else if (IS_MODIFIER_KEYCODE(keycode)) {
        add_mods(MOD_BIT((uint8_t)keycode));
    }
}

static void era_socd_del_report_key(uint16_t keycode) {
    if (IS_BASIC_KEYCODE(keycode)) {
        del_key((uint8_t)keycode);
    } else if (IS_MODIFIER_KEYCODE(keycode)) {
        del_mods(MOD_BIT((uint8_t)keycode));
    }
}

static bool era_socd_process_pair(uint8_t pair, uint16_t keycode, keyrecord_t *record) {
    era_socd_config_t *config = &socd_config[pair];

    if (!config->enable || config->mode != ERA_SOCD_MODE_LAST_INPUT_WINS || !era_socd_pair_can_report(config)) {
        return true;
    }

    for (uint8_t i = 0; i < ERA_SOCD_PAIR_KEY_COUNT; i++) {
        if (keycode != config->keycode[i]) {
            continue;
        }

        uint8_t other = 1 - i;
        if (record->event.pressed) {
            socd_pressed[pair][i] = true;
            if (socd_pressed[pair][other]) {
                era_socd_del_report_key(config->keycode[other]);
            }
        } else {
            socd_pressed[pair][i] = false;
            if (socd_pressed[pair][other]) {
                era_socd_add_report_key(config->keycode[other]);
            }
        }
        break;
    }

    return true;
}

void era_socd_init(void) {
    for (uint8_t pair = 0; pair < ERA_SOCD_PAIR_COUNT; pair++) {
        era_socd_load_pair(pair, true);
    }
}

void era_socd_reload_from_eeprom(void) {
    for (uint8_t pair = 0; pair < ERA_SOCD_PAIR_COUNT; pair++) {
        era_socd_load_pair(pair, false);
    }
}

bool era_socd_process_record(uint16_t keycode, keyrecord_t *record) {
    if (!record) {
        return true;
    }

    for (uint8_t pair = 0; pair < ERA_SOCD_PAIR_COUNT; pair++) {
        if (!era_socd_process_pair(pair, keycode, record)) {
            return false;
        }
    }
    return true;
}

bool era_socd_is_bound_keycode(uint16_t keycode) {
    for (uint8_t pair = 0; pair < ERA_SOCD_PAIR_COUNT; pair++) {
        era_socd_config_t *config = &socd_config[pair];
        if (!config->enable || config->mode != ERA_SOCD_MODE_LAST_INPUT_WINS) {
            continue;
        }
        for (uint8_t i = 0; i < ERA_SOCD_PAIR_KEY_COUNT; i++) {
            if (keycode == config->keycode[i]) {
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
    if (socd_config[pair].enable == next) {
        return true;
    }
    socd_config[pair].enable = next;
    era_socd_reset_pair_state(pair);
#ifdef VIA_ENABLE
    era_state_sync_note_config_semantic_commit(era_socd_config_offset(pair), sizeof(socd_config[pair]));
#endif
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
    if (socd_config[pair].keycode[key_index] == keycode) {
        return true;
    }
    socd_config[pair].keycode[key_index] = keycode;
    era_socd_reset_pair_state(pair);
#ifdef VIA_ENABLE
    era_state_sync_note_config_semantic_commit(era_socd_config_offset(pair), sizeof(socd_config[pair]));
#endif
    return true;
}

uint16_t era_socd_get_keycode(uint8_t pair, uint8_t key_index) {
    if (pair >= ERA_SOCD_PAIR_COUNT || key_index >= ERA_SOCD_PAIR_KEY_COUNT) {
        return KC_NO;
    }
    return socd_config[pair].keycode[key_index];
}

bool era_socd_set_mode(uint8_t pair, uint8_t mode) {
    if (pair >= ERA_SOCD_PAIR_COUNT || mode != ERA_SOCD_MODE_LAST_INPUT_WINS) {
        return false;
    }
    if (socd_config[pair].mode == ERA_SOCD_MODE_LAST_INPUT_WINS) {
        return true;
    }
    socd_config[pair].mode = ERA_SOCD_MODE_LAST_INPUT_WINS;
    era_socd_reset_pair_state(pair);
#ifdef VIA_ENABLE
    era_state_sync_note_config_semantic_commit(era_socd_config_offset(pair), sizeof(socd_config[pair]));
#endif
    return true;
}

uint8_t era_socd_get_mode(uint8_t pair) {
    if (pair >= ERA_SOCD_PAIR_COUNT) {
        return 0;
    }
    return socd_config[pair].mode;
}
