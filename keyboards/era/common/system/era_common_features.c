// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "era_common_features.h"

#include "era_flash_slice.h"
#include "era_usb_session.h"

#ifdef ERA_TAP_DANCE_ENABLE
#    include "../features/era_tapdance.h"
#endif
#ifdef ERA_DEBOUNCE_ENABLE
#    include "era_matrix_debounce_config.h"
#endif
#ifdef ERA_KKUK_ENABLE
#    include "../features/era_kkuk.h"
#endif
#ifdef ERA_BACKLIGHT_EFFECT_ENABLE
#    include "../features/era_backlight.h"
#endif
#ifdef ERA_BACKLIGHT_ALWAYS_ON
#    include "../features/era_backlight_always_on.h"
#endif
#ifdef ERA_RGB_INDICATOR_ENABLE
#    include "../features/era_rgb_indicator.h"
#endif
#ifdef ERA_SOCD_ENABLE
#    include "../features/era_socd.h"
#endif
#ifdef ERA_TAPPING_CONFIG_ENABLE
#    include "../features/era_tapping.h"
#endif
#ifdef ERA_MOUSEKEY_CONFIG_ENABLE
#    include "../features/era_mousekey.h"
#endif
#ifdef ERA_VIA_SYSTEM_ENABLE
#    include "era_via_system.h"
#endif
#if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET) && defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING)
#    include "bootloader.h"
#endif

void era_common_features_init(void) {
#ifdef ERA_TAP_DANCE_ENABLE
    era_tapdance_init();
#endif
#ifdef ERA_SOCD_ENABLE
    era_socd_init();
#endif
#ifdef ERA_KKUK_ENABLE
    era_kkuk_init();
#endif
#ifdef ERA_DEBOUNCE_ENABLE
    era_matrix_debounce_config_init();
#endif
#ifdef ERA_TAPPING_CONFIG_ENABLE
    era_tapping_init();
#endif
#ifdef ERA_MOUSEKEY_CONFIG_ENABLE
    era_mousekey_init();
#endif
#ifdef ERA_BACKLIGHT_EFFECT_ENABLE
    era_backlight_init();
#endif
#ifdef ERA_BACKLIGHT_ALWAYS_ON
    /* Before `backlight_init()`, which is what makes it enough to repair the
       stored block and touch no hardware -- the unit's own header has the
       positions. */
    era_backlight_always_on_init();
#endif
#ifdef ERA_RGB_INDICATOR_ENABLE
    era_rgb_indicator_init();
#endif
}

void era_common_features_reload_from_eeprom(void) {
#ifdef ERA_TAP_DANCE_ENABLE
    era_tapdance_reload_from_eeprom();
#endif
#ifdef ERA_SOCD_ENABLE
    era_socd_reload_from_eeprom();
#endif
#ifdef ERA_KKUK_ENABLE
    era_kkuk_reload_from_eeprom();
#endif
#ifdef ERA_DEBOUNCE_ENABLE
    era_matrix_debounce_config_reload_from_eeprom();
#endif
#ifdef ERA_TAPPING_CONFIG_ENABLE
    era_tapping_reload_from_eeprom();
#endif
#ifdef ERA_MOUSEKEY_CONFIG_ENABLE
    era_mousekey_reload_from_eeprom();
#endif
#ifdef ERA_BACKLIGHT_EFFECT_ENABLE
    era_backlight_reload_from_eeprom();
#endif
#ifdef ERA_RGB_INDICATOR_ENABLE
    era_rgb_indicator_reload_from_eeprom();
#endif
}

void era_common_features_task(void) {
    /* The keyboard loop is running, so the pass a sliced erase yields to has
       something to scan. This is the arming condition rather than
       keyboard_post_init because the erase that matters most at boot -
       wear_leveling_init()'s consolidation - runs before matrix_init(). */
    era_flash_slice_arm();

    /* The frame-loss half of the ERA sleep decision. Here rather than in a
       board file because it is a fact about USB, not about a keyboard, and
       every ERA board reaches this function once per pass. */
    era_usb_session_task();

#ifdef ERA_VIA_SYSTEM_ENABLE
    era_via_system_task();
#endif
#ifdef ERA_KKUK_ENABLE
    era_kkuk_task();
#endif
#ifdef ERA_BACKLIGHT_EFFECT_ENABLE
    era_backlight_task();
#endif
#if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET) && defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING)
    rp2040_bootloader_double_tap_reset_task();
#endif
}

bool era_common_features_process_record(uint16_t keycode, keyrecord_t *record) {
#ifdef ERA_SOCD_ENABLE
    if (!era_socd_process_record(keycode, record)) {
        return false;
    }
#endif
#ifdef ERA_KKUK_ENABLE
    if (!era_kkuk_process_record(keycode, record)) {
        return false;
    }
#endif
#ifdef ERA_BACKLIGHT_EFFECT_ENABLE
    /* Last, and it never refuses: it watches the edge for the blink effects
       and consumes no keycode, so an earlier feature that swallows a record
       correctly suppresses the blink with it. */
    if (!era_backlight_process_record(keycode, record)) {
        return false;
    }
#endif
    return true;
}
