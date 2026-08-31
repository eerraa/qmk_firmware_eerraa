// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* What this is and which boards take it is at the top of the header. What is
 * here is the two things that make twelve lines correct rather than plausible.
 *
 * **It writes the stored block, never the hardware.** `era_common_features_init()`
 * is reached from `matrix_init_kb()` and `via_init_kb()`, which
 * `keyboard_init()` (`quantum/keyboard.c`) runs at positions 470 and 462 --
 * both *before* `backlight_init()` at 505. So correcting eeconfig here is
 * enough: `backlight_init()` reads what this left and applies it, and nothing
 * touches a PWM driver that has not been started. That is the same ordering
 * trap `era_backlight.c` documents from the other side, answered by not
 * needing the hardware at all.
 *
 * **An invalid block is left alone on purpose.** `backlight_init()` writes
 * `BACKLIGHT_DEFAULT_ON` at `BACKLIGHT_LEVELS` when `valid` is clear, which is
 * already the answer this unit wants, and is the path a fresh EEPROM and every
 * EEPROM CLEAN take. Claiming the block here would take over that job for no
 * gain and would have to keep `BACKLIGHT_DEFAULT_*` in step by hand.
 *
 * The suspend path is deliberately untouched. `suspend_power_down_quantum()`
 * (`quantum/quantum.c`) drops the level to zero in RAM only, so a sleeping
 * host still darkens the indicators, and `suspend_wakeup_init_quantum()` calls
 * `backlight_init()` again on the way out -- which now reads a block this unit
 * has guaranteed is on.
 */

#include "era_backlight_always_on.h"

#if !defined(BACKLIGHT_ENABLE)
#    error "ERA_BACKLIGHT_ALWAYS_ON is a rule about QMK's backlight; era_common_qmk_rules.mk refuses the selector without BACKLIGHT_ENABLE."
#endif

#include "backlight.h"
#include "eeconfig.h"

void era_backlight_always_on_init(void) {
    backlight_config_t stored;
    eeconfig_read_backlight(&stored);

    if (!stored.valid || (stored.enable && stored.level > 0)) {
        return;
    }

    stored.enable = true;
    stored.level  = BACKLIGHT_LEVELS;
    eeconfig_update_backlight(&stored);
}
