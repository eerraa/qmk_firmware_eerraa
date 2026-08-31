// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_matrix_debounce_config.h"

#include <string.h>

#include "debounce.h"
#include "../storage/era_eeprom_storage.h"
#include "era_matrix_debounce_runtime.h"
#ifdef VIA_ENABLE
#    include "era_state_sync.h"
#endif

#define ERA_MATRIX_DEBOUNCE_CONFIG_SIGNATURE 0x434E4244UL
#define ERA_MATRIX_DEBOUNCE_CONFIG_VERSION   1U

typedef struct __attribute__((packed)) {
    uint8_t  mode;
    uint8_t  pre_ms;
    uint8_t  post_ms;
    uint8_t  version;
    uint32_t signature;
} era_matrix_debounce_storage_config_t;

_Static_assert(sizeof(era_matrix_debounce_storage_config_t) == ERA_EEPROM_DEBOUNCE_CONFIG_SIZE, "ERA debounce config size changed.");
_Static_assert(ERA_EEPROM_DEBOUNCE_CONFIG_OFFSET + ERA_EEPROM_DEBOUNCE_CONFIG_SIZE <= ERA_EEPROM_CONFIG_SIZE, "ERA debounce config exceeds ERA EEPROM storage.");

static era_matrix_debounce_storage_config_t debounce_config;
static bool                                 debounce_config_loaded;

static void era_matrix_debounce_config_apply_defaults(era_matrix_debounce_storage_config_t *config) {
    era_matrix_debounce_config_t runtime_config;
    era_matrix_debounce_default_config(&runtime_config);

    memset(config, 0, sizeof(*config));
    config->mode      = runtime_config.mode;
    config->pre_ms    = runtime_config.pre_ms;
    config->post_ms   = runtime_config.post_ms;
    config->version   = ERA_MATRIX_DEBOUNCE_CONFIG_VERSION;
    config->signature = ERA_MATRIX_DEBOUNCE_CONFIG_SIGNATURE;
}

static bool era_matrix_debounce_config_is_valid(const era_matrix_debounce_storage_config_t *config) {
    if (!config || config->signature != ERA_MATRIX_DEBOUNCE_CONFIG_SIGNATURE || config->version != ERA_MATRIX_DEBOUNCE_CONFIG_VERSION) {
        return false;
    }
    if (config->mode >= ERA_MATRIX_DEBOUNCE_PROFILE_COUNT) {
        return false;
    }
    return config->pre_ms >= ERA_MATRIX_DEBOUNCE_MIN_DELAY_MS && config->pre_ms <= ERA_MATRIX_DEBOUNCE_MAX_DELAY_MS &&
           config->post_ms >= ERA_MATRIX_DEBOUNCE_MIN_DELAY_MS && config->post_ms <= ERA_MATRIX_DEBOUNCE_MAX_DELAY_MS;
}

static bool era_matrix_debounce_config_normalize(era_matrix_debounce_storage_config_t *config) {
    era_matrix_debounce_storage_config_t previous = *config;

    if (config->mode >= ERA_MATRIX_DEBOUNCE_PROFILE_COUNT) {
        config->mode = ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED;
    }

    config->pre_ms    = era_matrix_debounce_clamp_delay(config->pre_ms);
    config->post_ms   = era_matrix_debounce_clamp_delay(config->post_ms);
    config->version   = ERA_MATRIX_DEBOUNCE_CONFIG_VERSION;
    config->signature = ERA_MATRIX_DEBOUNCE_CONFIG_SIGNATURE;

    if (config->mode == ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED) {
        config->post_ms = config->pre_ms;
    }

    return memcmp(&previous, config, sizeof(*config)) != 0;
}

static void era_matrix_debounce_config_apply_runtime(void) {
    era_matrix_debounce_config_t runtime_config = {
        .mode    = debounce_config.mode,
        .pre_ms  = debounce_config.pre_ms,
        .post_ms = debounce_config.post_ms,
    };

    era_matrix_debounce_apply_config(&runtime_config);
}

static void era_matrix_debounce_config_ensure_loaded(void) {
    if (!debounce_config_loaded) {
        era_matrix_debounce_config_apply_defaults(&debounce_config);
        debounce_config_loaded = true;
        era_matrix_debounce_config_apply_runtime();
    }
}

void era_matrix_debounce_config_save(void) {
    era_matrix_debounce_config_ensure_loaded();
    era_eeprom_update_config(&debounce_config, ERA_EEPROM_DEBOUNCE_CONFIG_OFFSET, sizeof(debounce_config));
}

static bool era_matrix_debounce_config_load_from_eeprom(bool write_defaults) {
    bool dirty = false;

    if (era_eeprom_read_config(&debounce_config, ERA_EEPROM_DEBOUNCE_CONFIG_OFFSET, sizeof(debounce_config)) != sizeof(debounce_config) ||
        !era_matrix_debounce_config_is_valid(&debounce_config)) {
        era_matrix_debounce_config_apply_defaults(&debounce_config);
        dirty = true;
    } else {
        dirty = era_matrix_debounce_config_normalize(&debounce_config);
    }

    debounce_config_loaded = true;
    era_matrix_debounce_config_apply_runtime();

    if (write_defaults && dirty) {
        era_matrix_debounce_config_save();
    }
    return true;
}

void era_matrix_debounce_config_init(void) {
    era_matrix_debounce_config_load_from_eeprom(true);
}

void era_matrix_debounce_config_reload_from_eeprom(void) {
    era_matrix_debounce_config_load_from_eeprom(false);
}

#ifdef VIA_ENABLE
static void era_matrix_debounce_publish_if_changed(const era_matrix_debounce_storage_config_t *previous) {
    if (memcmp(previous, &debounce_config, sizeof(*previous)) != 0) {
        era_state_sync_note_config_semantic_commit(ERA_EEPROM_DEBOUNCE_CONFIG_OFFSET, sizeof(debounce_config));
    }
}
#else
static void era_matrix_debounce_publish_if_changed(const era_matrix_debounce_storage_config_t *previous) {
    (void)previous;
}
#endif

bool era_matrix_debounce_config_set_mode(uint8_t mode) {
    if (mode >= ERA_MATRIX_DEBOUNCE_PROFILE_COUNT) {
        return false;
    }

    era_matrix_debounce_config_ensure_loaded();
    era_matrix_debounce_storage_config_t previous = debounce_config;
    debounce_config.mode = mode;
    if (mode == ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED) {
        debounce_config.pre_ms  = era_matrix_debounce_clamp_delay(debounce_config.pre_ms);
        debounce_config.post_ms = debounce_config.pre_ms;
    } else if (mode == ERA_MATRIX_DEBOUNCE_PROFILE_FAST) {
        debounce_config.post_ms = era_matrix_debounce_clamp_delay(debounce_config.post_ms);
    } else {
        debounce_config.pre_ms  = era_matrix_debounce_clamp_delay(debounce_config.pre_ms);
        debounce_config.post_ms = era_matrix_debounce_clamp_delay(debounce_config.post_ms);
    }
    era_matrix_debounce_config_apply_runtime();
    era_matrix_debounce_publish_if_changed(&previous);
    return true;
}

bool era_matrix_debounce_config_set_single_delay(uint8_t delay_ms) {
    era_matrix_debounce_config_ensure_loaded();
    if (debounce_config.mode != ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED) {
        return false;
    }

    era_matrix_debounce_storage_config_t previous = debounce_config;
    debounce_config.pre_ms  = era_matrix_debounce_clamp_delay(delay_ms);
    debounce_config.post_ms = debounce_config.pre_ms;
    era_matrix_debounce_config_apply_runtime();
    era_matrix_debounce_publish_if_changed(&previous);
    return true;
}

bool era_matrix_debounce_config_set_press_delay(uint8_t delay_ms) {
    era_matrix_debounce_config_ensure_loaded();
    if (debounce_config.mode != ERA_MATRIX_DEBOUNCE_PROFILE_ADVANCED) {
        return false;
    }

    era_matrix_debounce_storage_config_t previous = debounce_config;
    debounce_config.pre_ms = era_matrix_debounce_clamp_delay(delay_ms);
    era_matrix_debounce_config_apply_runtime();
    era_matrix_debounce_publish_if_changed(&previous);
    return true;
}

bool era_matrix_debounce_config_set_release_delay(uint8_t delay_ms) {
    era_matrix_debounce_config_ensure_loaded();
    if (debounce_config.mode == ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED) {
        return false;
    }

    era_matrix_debounce_storage_config_t previous = debounce_config;
    debounce_config.post_ms = era_matrix_debounce_clamp_delay(delay_ms);
    era_matrix_debounce_config_apply_runtime();
    era_matrix_debounce_publish_if_changed(&previous);
    return true;
}

uint8_t era_matrix_debounce_config_get_mode(void) {
    era_matrix_debounce_config_ensure_loaded();
    return debounce_config.mode;
}

uint8_t era_matrix_debounce_config_get_press_delay(void) {
    era_matrix_debounce_config_ensure_loaded();
    return debounce_config.pre_ms;
}

uint8_t era_matrix_debounce_config_get_release_delay(void) {
    era_matrix_debounce_config_ensure_loaded();
    return debounce_config.post_ms;
}

void debounce_init(void) {
    era_matrix_debounce_init();
    if (debounce_config_loaded) {
        era_matrix_debounce_config_apply_runtime();
    }
}

bool debounce(matrix_row_t raw[], matrix_row_t cooked[], bool changed) {
    return era_matrix_debounce_update(raw, cooked, changed);
}
