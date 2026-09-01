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
/* RGB Sleep controls continue the shared SYSTEM value-id range after
   sync (5..7) and link (8..9). The stock VIA definition uses the minute preset;
   Custom VIA uses the exact-seconds uint16 value on split TOMAK boards. Value
   12 is the common master toggle for every RGB-capable ERA board: when off,
   idle timeout, USB suspend, and frame-loss/host-loss RGB sleep are all off. */
#define ERA_VIA_SYSTEM_RGB_SLEEP_PRESET_VALUE_ID 10
#define ERA_VIA_SYSTEM_RGB_SLEEP_EXACT_SECONDS_VALUE_ID 11
#define ERA_VIA_SYSTEM_RGB_SLEEP_ENABLE_VALUE_ID 12

bool era_via_system_handle_via_command(uint8_t *data, uint8_t length);
void era_via_system_task(void);

/* Jump-to-BOOT is a terminal VIA action, but its SET is not a terminal VIA
 * transaction. The normal custom-control lifecycle is SET -> SAVE -> State
 * Sync reconciliation. Each observed successor proves that the previous
 * response reached the host, so the boot path advances on those protocol facts
 * and uses this timeout only when a host omits one of them. It deliberately
 * does not use the 500 ms restart-quiet predicate below: State Sync itself may
 * poll on that cadence. The timeout restarts when SAVE arrives, then while
 * non-State-Sync reconciliation traffic makes progress; a late SAVE therefore
 * gets a complete reconciliation window without letting State Sync polling
 * starve the fallback. */
#ifndef ERA_VIA_SYSTEM_BOOT_PHASE_FALLBACK_MS
#    define ERA_VIA_SYSTEM_BOOT_PHASE_FALLBACK_MS 2000
#endif

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
 * (`ERA_VIA_BOOTLOADER_ENABLE`) does not use this policy: it waits for explicit
 * SET/SAVE/State-Sync lifecycle facts and the RAW IN drain instead, because a
 * 500 ms State Sync poll can phase-lock a 500 ms quiet predicate.
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

/* **The EEPROM clean is one invalidated word before reset.**
 *
 * `eeconfig_disable()` is two things: a whole-backing erase of about 0.4 s, and
 * a write of EECONFIG_MAGIC_NUMBER_OFF. Only the second belongs here. The boot
 * that follows observes the invalid magic in `quantum_init()` and opens
 * `eeconfig_init_quantum()` (`quantum/eeconfig.c`) with the whole-backing erase,
 * then rebuilds VIA's keymap and macro regions.
 *
 * The split safety property is not supplied by doing that erase early. It is
 * supplied by `split/era_split_restart_agreement.c`: while a relation is
 * serviced, both halves quarantine storage and report a checked PREPARED vote
 * before the first commit deadline exists. A device run showed why the old
 * one-half fallback was invalid: the untouched half retained its macros and
 * storage convergence put them back after the commanded half rebooted.
 *
 * A serviced storage-enabled split runs it in the deadline-free PREPARE phase;
 * a split half with no serviced relation runs it immediately before its local
 * reset. That path returns the reboot-checked physical boot-playback result.
 * The weak non-split fallback performs the ordinary one-word update and
 * immediate logical readback instead. */
bool era_via_system_eeprom_invalidate(void);

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

#ifdef ERA_VIA_SYSTEM_TEST
/* Host-test seams only. Production reads the ChibiOS RAW IN endpoint directly. */
void era_via_system_test_reset(void);
void era_via_system_test_set_raw_hid_in_inactive(bool inactive);
#endif
