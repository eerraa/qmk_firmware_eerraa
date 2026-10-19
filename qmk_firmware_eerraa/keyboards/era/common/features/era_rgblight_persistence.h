// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "eeprom.h"
#include "rgblight.h"

#ifdef VELOCIKEY_ENABLE
/* A runtime-equal SET still has to repair a previous failed write. The QMK
 * owner retains its runtime choice; only outer completion acknowledges it. */
static inline bool era_rgblight_velocikey_set_enabled(bool enabled) {
    if (!eeprom_begin_transaction()) return false;
    if (enabled != rgblight_velocikey_enabled()) {
        rgblight_velocikey_toggle();
    } else {
        eeconfig_update_rgblight_current();
    }
    return eeprom_end_transaction();
}
#endif
