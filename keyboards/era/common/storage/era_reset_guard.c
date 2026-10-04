// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_reset_guard.h"

#include <stdint.h>
#include "eeprom.h"
#include "eeconfig.h"
#include "nvm_eeprom_eeconfig_internal.h"
#include "nvm_eeconfig.h"
#include "era_eeprom_storage.h"

typedef struct __attribute__((packed)) {
    uint32_t reset_key32;
    uint32_t reset_key_inverse32;
    uint8_t  reserved_zero[8];
} era_reset_guard_t;

_Static_assert(sizeof(era_reset_guard_t) == ERA_EEPROM_RESET_GUARD_CONFIG_SIZE, "ERA reset guard size changed.");

static bool era_reset_guard_is_valid(void) {
    era_reset_guard_t guard;
    if (era_eeprom_read_config(&guard, ERA_EEPROM_RESET_GUARD_CONFIG_OFFSET, sizeof(guard)) != sizeof(guard)) {
        return false;
    }
    for (uint8_t i = 0; i < sizeof(guard.reserved_zero); i++) {
        if (guard.reserved_zero[i] != 0) {
            return false;
        }
    }
    return guard.reset_key32 == (uint32_t)ERA_EEPROM_RESET_KEY &&
           guard.reset_key_inverse32 == (uint32_t)~(uint32_t)ERA_EEPROM_RESET_KEY;
}

void era_reset_guard_write(void) {
    era_reset_guard_t guard = {
        .reset_key32         = (uint32_t)ERA_EEPROM_RESET_KEY,
        .reset_key_inverse32 = (uint32_t)~(uint32_t)ERA_EEPROM_RESET_KEY,
    };
    era_eeprom_update_config(&guard, ERA_EEPROM_RESET_GUARD_CONFIG_OFFSET, sizeof(guard));
}

bool era_reset_guard_check(void) {
    if (era_reset_guard_is_valid()) {
        return false;
    }

    /* The stored magic may survive a failed invalidation. Keep the boot gate
     * independent of those bytes until defaults have actually completed. */
    eeconfig_require_reset();

    /* An erase already pending -- EEPROM CLEAN, or a store QMK has never
     * initialised -- runs eeconfig_init() anyway, and that writes the guard
     * last.
     *
     * nvm_eeconfig_is_enabled() and not eeconfig_is_enabled(): the latter also
     * requires via_eeprom_is_valid(), which via_init() sets true before
     * quantum_init() reads it, so it can be false here and true there. */
    if (!nvm_eeconfig_is_enabled()) {
        return false;
    }

    /* Only the magic, not an erase here: quantum_init() then finds eeconfig
     * disabled and erases the whole logical EEPROM through eeconfig_init(),
     * VIA's region included, exactly as it does after EEPROM CLEAN. */
    eeprom_update_word(EECONFIG_MAGIC, EECONFIG_MAGIC_NUMBER_OFF);
    return true;
}
