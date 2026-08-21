// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_via_system.h"

#include "eeprom.h"
/* EECONFIG_MAGIC, whose address is the core NVM eeprom backend's to state. ERA
   already stands on that layout -- storage/era_storage_adoption.h supplies and
   asserts EECONFIG_SIZE, and the whole ERA config block is placed against it --
   so naming one more symbol from it adds no coupling that was not already
   load-bearing, and it is a coupling the compiler checks: an upstream that
   moves or renames this fails the build by name rather than silently.

   Evaluated against two alternatives 2026-08-19 and kept as the better shape.
   The header is already this fork's seam and not merely upstream's internals:
   the changed-run engine and the `_kb` hooks ERA added to the core are declared
   in it (era_qmk_fork_ledger.md), and the two other ERA units that include it
   -- storage/era_eeprom_config_io.c for those hooks, split/era_host_peer_storage.c
   for the domain addresses its schema asserts by name -- stand on it the same
   way. An ERA wrapper header re-exporting the few symbols would hide which
   core name each unit stands on behind a second copy of the seam; a core-side
   `nvm_eeconfig_invalidate()` split out of `nvm_eeconfig_disable()` would spend
   one more fork edit to hide a one-word write. Neither removes the coupling,
   both cost a hop, and the compiler-checked name is what a rebase needs. */
#include "nvm_eeprom_eeconfig_internal.h"
#include "nvm_eeconfig.h"
#include "quantum.h"
#include "timer.h"
#include "via.h"

/* The restart waits for the VIA application to fall silent, and only then for
 * a bound. A fixed defer stood here (100 ms) and the application's own
 * traffic fell exactly on it: with the system page open, one CLEAN produced
 * eight failed report writes and one "device closed unexpectedly" inside a
 * 48 ms window at the cut, all fired at a handle the reset had just closed
 * (VIA-app-errors capture, 2026-08-15). Two groups of four read like a
 * re-fetch of the page's four controls; that is a reading of the host, not a
 * fact this gate depends on. The cut itself is required -- the storage
 * contract's restart requirement, era_host_peer_storage_contract.md -- so
 * what moves is *when* it lands: after the last raw HID command has been
 * followed by ERA_VIA_SYSTEM_RESTART_HID_QUIET_MS of nothing, so the
 * application's own pacing, whatever it is, is what the cut falls between.
 *
 * The bound is what keeps this a gate and not a hold: an application that
 * never goes quiet -- a page that polls -- reaches
 * ERA_VIA_SYSTEM_RESTART_DEFER_MAX_MS and gets the previous behaviour,
 * a cut into its traffic, rather than a board that never restarts. Both
 * count from the same origin, the requesting handler returning, whose
 * response leaves on the same pass.
 *
 * **Every cut this firmware makes into VIA traffic reads these two numbers,
 * and they are declared outside every feature guard for that reason.** The
 * split link's agreed switch carried its own pair at the same values, with an
 * assert whose message string was byte-identical to the one below -- a policy
 * copied rather than shared, which is a policy that gets changed in one place.
 * A board compiling none of the users pays two macros nobody expands.
 *
 * What "raw HID" is exactly, and why a diagnostics profile does not disturb
 * it, is at the stamp itself below. */
#ifndef ERA_VIA_SYSTEM_RESTART_HID_QUIET_MS
#    define ERA_VIA_SYSTEM_RESTART_HID_QUIET_MS 500
#endif
_Static_assert(ERA_VIA_SYSTEM_RESTART_HID_QUIET_MS <= ERA_VIA_SYSTEM_RESTART_DEFER_MAX_MS, "the quiet interval must fit inside the bound, or the bound is the only gate left");

#ifdef ERA_EEPROM_CLEAN_ENABLE
enum {
    ERA_VIA_SYSTEM_EEPROM_RESET_0_BIT    = 1U << 0,
    ERA_VIA_SYSTEM_EEPROM_RESET_1_BIT    = 1U << 1,
    ERA_VIA_SYSTEM_EEPROM_RESET_DONE_BIT = 1U << 2,
    ERA_VIA_SYSTEM_EEPROM_RESET_MASK     = ERA_VIA_SYSTEM_EEPROM_RESET_0_BIT | ERA_VIA_SYSTEM_EEPROM_RESET_1_BIT | ERA_VIA_SYSTEM_EEPROM_RESET_DONE_BIT,
};

/* How long a partially confirmed erase stays armed. Measured from the first
 * confirm bit and never extended by the later ones: an idle timeout would let
 * anything that periodically touches one of these value ids hold the window
 * open forever, which is the defect this window exists to close. Ten seconds is
 * generous for three deliberate clicks in the VIA UI, and expiring too early
 * costs only a retry. */
#ifndef ERA_VIA_SYSTEM_EEPROM_RESET_CONFIRM_WINDOW_MS
#    define ERA_VIA_SYSTEM_EEPROM_RESET_CONFIRM_WINDOW_MS 10000
#endif

static uint8_t  eeprom_reset_confirm;
static bool     eeprom_reset_pending;
static uint32_t eeprom_reset_requested_ms;
static uint32_t eeprom_reset_confirm_started_ms;
#endif

/* The raw-HID quiet stamp, and it is deliberately outside every feature guard.
 * Three cuts read it -- the EEPROM CLEAN, the split link's agreed switch, and
 * the VIA Apply USB re-enumeration -- through one predicate and one pair of
 * bounds, and one stamp is what makes them answer about the same traffic. A
 * board compiling none of them pays one weak override that records a timestamp
 * nobody reads, which is cheaper than a second gate keyed to a second feature
 * macro. */
static uint32_t via_last_raw_hid_ms;

/* QMK declares this hook nowhere: via.c defines it weak and calls it at the top
 * of raw_hid_receive() for every report, taking `true` as "fully handled,
 * response already sent". This override handles nothing -- it records the
 * arrival and hands the report straight back to the VIA dispatcher.
 *
 * "Raw HID" is exact. The stamp is taken in via_command_kb(), the first call
 * raw_hid_receive() makes for every report on the RAW OUT endpoint, which is
 * the VIA interface and nothing else: the hid_listen console is an IN-only
 * endpoint (console_task() flushes a buffered IN, tmk_core/protocol/chibios/
 * usb_main.c) and cannot reach it, so a diagnostics profile printing at full
 * rate leaves both gates exactly as quiet as a release build. */
bool via_command_kb(uint8_t *data, uint8_t length);
bool via_command_kb(uint8_t *data, uint8_t length) {
    (void)data;
    (void)length;
    via_last_raw_hid_ms = timer_read32();
    return false;
}

uint32_t era_via_system_raw_hid_quiet_ms(void) {
    /* raw_hid_task() drains the RAW OUT endpoint earlier in the same main-loop
     * pass (quantum/main.c), so a command that has arrived is already stamped
     * by the time a gate reads this. */
    return timer_elapsed32(via_last_raw_hid_ms);
}

bool era_via_system_restart_quiet_ok(uint32_t requested_ms) {
    return era_via_system_raw_hid_quiet_ms() >= ERA_VIA_SYSTEM_RESTART_HID_QUIET_MS ||
           timer_elapsed32(requested_ms) >= ERA_VIA_SYSTEM_RESTART_DEFER_MAX_MS;
}

void era_via_system_eeprom_invalidate(void) {
    /* The raw driver write and not nvm_eeprom_update_changed_word(): the
       wrapper notifies nvm_eeprom_changed_kb(), which is how the ERA storage
       engine learns a domain moved, and a store about to be erased must not
       arm a transfer of itself on its way out. */
    eeprom_update_word(EECONFIG_MAGIC, EECONFIG_MAGIC_NUMBER_OFF);
}

__attribute__((weak)) bool era_via_system_eeprom_clean_handed_off(void) {
    return false;
}

#ifdef ERA_EEPROM_CLEAN_ENABLE
static uint8_t era_via_system_eeprom_reset_bit(uint8_t value_id) {
    switch (value_id) {
        case ERA_VIA_SYSTEM_EEPROM_RESET_0_VALUE_ID:
            return ERA_VIA_SYSTEM_EEPROM_RESET_0_BIT;
        case ERA_VIA_SYSTEM_EEPROM_RESET_1_VALUE_ID:
            return ERA_VIA_SYSTEM_EEPROM_RESET_1_BIT;
        case ERA_VIA_SYSTEM_EEPROM_RESET_DONE_VALUE_ID:
            return ERA_VIA_SYSTEM_EEPROM_RESET_DONE_BIT;
        default:
            return 0;
    }
}

static void era_via_system_trigger_eeprom_reset_if_confirmed(void) {
    if ((eeprom_reset_confirm & ERA_VIA_SYSTEM_EEPROM_RESET_MASK) == ERA_VIA_SYSTEM_EEPROM_RESET_MASK) {
        if (eeprom_reset_pending) {
            return;
        }
        eeprom_reset_confirm = 0;

        /* Nothing is written here. The three confirms have been given, and what
         * that buys is a restart; the store is invalidated at the instant the
         * restart happens, which on a split board is an instant both halves
         * agreed on. The quiet interval therefore runs from the confirming
         * report, with no local work in front of it to anchor around. */
        if (era_via_system_eeprom_clean_handed_off()) {
            return;
        }

        eeprom_reset_pending      = true;
        eeprom_reset_requested_ms = timer_read32();
    }
}
#endif

void era_via_system_task(void) {
#ifdef ERA_EEPROM_CLEAN_ENABLE
    if (eeprom_reset_confirm != 0 && timer_elapsed32(eeprom_reset_confirm_started_ms) >= ERA_VIA_SYSTEM_EEPROM_RESET_CONFIRM_WINDOW_MS) {
        eeprom_reset_confirm = 0;
    }

    if (!eeprom_reset_pending) {
        return;
    }

    if (!era_via_system_restart_quiet_ok(eeprom_reset_requested_ms)) {
        return;
    }

    eeprom_reset_pending = false;
    /* The same order the agreed path takes at its commit: prepare, then reset,
       with nothing between them. */
    era_via_system_eeprom_invalidate();
    soft_reset_keyboard();
#endif
}

static bool era_via_system_set_value(uint8_t value_id, uint8_t *value_data, uint8_t length) {
#ifdef ERA_VIA_BOOTLOADER_ENABLE
    if (value_id == ERA_VIA_SYSTEM_BOOTLOADER_VALUE_ID) {
        if (value_data[0]) {
            reset_keyboard();
        }
        return true;
    }
#endif

#ifdef ERA_EEPROM_CLEAN_ENABLE
    uint8_t bit = era_via_system_eeprom_reset_bit(value_id);
    if (!bit) {
        return false;
    }

    if (value_data[0]) {
        if (eeprom_reset_confirm == 0) {
            eeprom_reset_confirm_started_ms = timer_read32();
        }
        eeprom_reset_confirm |= bit;
        era_via_system_trigger_eeprom_reset_if_confirmed();
    } else {
        eeprom_reset_confirm &= (uint8_t)~bit;
    }

    return true;
#else
    return false;
#endif
}

static bool era_via_system_get_value(uint8_t value_id, uint8_t *value_data, uint8_t length) {
#ifdef ERA_VIA_BOOTLOADER_ENABLE
    if (value_id == ERA_VIA_SYSTEM_BOOTLOADER_VALUE_ID) {
        value_data[0] = 0;
        return true;
    }
#endif

#ifdef ERA_EEPROM_CLEAN_ENABLE
    uint8_t bit = era_via_system_eeprom_reset_bit(value_id);
    if (!bit) {
        return false;
    }

    value_data[0] = (eeprom_reset_confirm & bit) ? 1 : 0;
    return true;
#else
    return false;
#endif
}

bool era_via_system_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || data[1] != ERA_VIA_SYSTEM_CHANNEL) {
        return false;
    }

    uint8_t *command_id = &data[0];

    if (*command_id == id_custom_save) {
        return true;
    }

    uint8_t *value_id   = &data[2];
    uint8_t *value_data = &data[3];

    switch (*command_id) {
        case id_custom_set_value:
            return era_via_system_set_value(*value_id, value_data, length);
        case id_custom_get_value:
            return era_via_system_get_value(*value_id, value_data, length);
        default:
            return false;
    }
}
