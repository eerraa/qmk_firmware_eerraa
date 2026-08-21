// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ERA_VIA_SYSTEM_CHANNEL 9
#define ERA_VIA_SYSTEM_BOOTLOADER_VALUE_ID 1
#define ERA_VIA_SYSTEM_EEPROM_RESET_0_VALUE_ID 2
#define ERA_VIA_SYSTEM_EEPROM_RESET_1_VALUE_ID 3
#define ERA_VIA_SYSTEM_EEPROM_RESET_DONE_VALUE_ID 4

bool era_via_system_handle_via_command(uint8_t *data, uint8_t length);
void era_via_system_task(void);
/* Milliseconds since the last report on the VIA raw-HID endpoint. Every gate
 * that has to cut into the application's own pacing reads this one stamp. */
uint32_t era_via_system_raw_hid_quiet_ms(void);
/* The bound the policy below runs to when the application never falls silent.
 * Exported because a second unit sizes a window against it: the agreement's
 * tie-break has to outwait a peer whose own request is being held by this gate,
 * and deriving that from the gate rather than restating a number is what keeps
 * the two from drifting (`split/era_split_restart_agreement.c`). */
#ifndef ERA_VIA_SYSTEM_RESTART_DEFER_MAX_MS
#    define ERA_VIA_SYSTEM_RESTART_DEFER_MAX_MS 2000
#endif

/* **The one restart-quiet policy.** A cut that lands inside the VIA
 * application's own traffic is what produced the captured report failures, so
 * every cut this firmware makes into that traffic waits for the application to
 * fall silent and then for a bound. Three users -- the EEPROM CLEAN, the split
 * link's agreed switch, and the VIA Apply USB re-enumeration
 * (`split/era_split_via_link.c`) -- ask this, and none of them states the
 * interval: copies of one policy at the same two values is a policy that will
 * be changed in one place. The bootloader jump beside them
 * (`ERA_VIA_BOOTLOADER_ENABLE`) is not a cut in this sense and does not ask: it
 * leaves the device rather than returning it, and the owner asked for exactly
 * that.
 *
 * `requested_ms` is a timer_read32() stamp of when the restart was asked for,
 * and it is what makes the bound a bound rather than a hold: an application
 * that never goes quiet gets the previous behaviour, a cut into its traffic,
 * instead of a board that never restarts.
 *
 * The interval it waits for is the *application's* silence and not this half's
 * own work, so a caller whose request begins with a long local operation
 * restamps the raw-HID clock at the end of that operation rather than letting
 * the operation count as quiet. */
bool era_via_system_restart_quiet_ok(uint32_t requested_ms);

/* **The EEPROM clean, and it is one word.**
 *
 * `eeconfig_disable()` is two things: a whole-backing erase of about 0.4 s, and
 * a write of EECONFIG_MAGIC_NUMBER_OFF. Only the second has to survive the
 * restart, because the boot that follows opens with
 * `eeconfig_init_quantum()` (`quantum/eeconfig.c`), whose first statement is
 * `nvm_eeconfig_erase()` -- the same whole-backing erase. So the erase before
 * the reset was a duplicate of one that runs anyway, and it cost more than its
 * own 0.4 s: with the store erased, `via_init()` found the VIA magic invalid
 * and ran `eeconfig_init_via()` a few init steps before `quantum_init()` erased
 * that work and ran it again, writing the whole dynamic keymap twice on every
 * recovery boot.
 *
 * Invalidating instead leaves the store intact until the boot erases it once,
 * and every reader between `via_init()` and `quantum_init()` is unaffected:
 * each board family's `matrix_init_kb`/`via_init_kb` load is re-run by
 * `eeconfig_init_kb()` at `quantum_init()`, and the family that guards its own
 * strict reset already tests `nvm_eeconfig_is_enabled()` and defers to the
 * pending init for exactly this reason (`sirind/common/tomak_common.c`).
 *
 * It runs at the restart's commit instant and not when the command arrives,
 * which is the rule every agreed act follows: nothing is persisted until the
 * restart is actually happening. */
void era_via_system_eeprom_invalidate(void);

/* Whether this board's EEPROM clean is handed to the split agreement instead of
 * this unit's own cut. Weak here and answered "no", so a non-split board keeps
 * the whole path: quiet gate, invalidate, reset. `split/era_split_keyboard.c`
 * overrides it and hands the act to the agreement service, which from that
 * point owns the quiet gate, the two-half deadline, the prepare and the reset.
 *
 * It answers about *ownership* and not about success. A split board that
 * refuses the request -- because another restart is already pending -- has
 * still handed it off, and the right outcome there is that nothing happens
 * rather than one half resetting alone into an agreement it just cancelled. */
bool era_via_system_eeprom_clean_handed_off(void);
