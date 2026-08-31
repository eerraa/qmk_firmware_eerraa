// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_via_system.h"
#include "era_state_sync.h"

#include "eeprom.h"
#if defined(ERA_HOST_PEER_STORAGE_V1_ENABLE) && defined(EEPROM_CUSTOM)
#    include "../storage/era_eeprom_driver.h"
#    include "../storage/era_storage_layout.h"
#endif
/* EECONFIG_MAGIC is the one boot predicate CLEAN has to invalidate. The split
   agreement obtains reboot-durable PREPARED votes from both halves before
   creating a commit deadline, so a one-half reset cannot leave valid old
   storage able to repopulate the CLEANed half. */
#include "nvm_eeprom_eeconfig_internal.h"
#include "nvm_eeconfig.h"
#include "quantum.h"
#include "timer.h"
#include "via.h"

#if defined(ERA_VIA_BOOTLOADER_ENABLE) && defined(PROTOCOL_CHIBIOS)
#    include "usb_driver.h"
#    include "usb_endpoints.h"
extern usb_endpoint_in_t usb_endpoints_in[USB_ENDPOINT_IN_COUNT];
#endif

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

#ifdef ERA_VIA_BOOTLOADER_ENABLE
typedef enum {
    ERA_VIA_SYSTEM_BOOT_IDLE = 0,
    ERA_VIA_SYSTEM_BOOT_WAIT_SAVE,
    ERA_VIA_SYSTEM_BOOT_WAIT_STATE_SYNC,
    ERA_VIA_SYSTEM_BOOT_WAIT_RAW_IN_DRAIN,
} era_via_system_boot_state_t;

static era_via_system_boot_state_t boot_state;
static uint32_t                    boot_phase_started_ms;

#    ifdef ERA_VIA_SYSTEM_TEST
static bool boot_test_raw_hid_in_inactive = true;
#    endif

static bool era_via_system_boot_raw_hid_in_inactive(void) {
#    ifdef ERA_VIA_SYSTEM_TEST
    return boot_test_raw_hid_in_inactive;
#    elif defined(PROTOCOL_CHIBIOS)
    return usb_endpoint_in_is_inactive(&usb_endpoints_in[USB_ENDPOINT_IN_RAW]);
#    else
    /* ERA's VIA-bootloader boards are ChibiOS today. Keep the common unit
     * buildable on another QMK platform; there the protocol facts still gate
     * the jump, but there is no ChibiOS endpoint object to inspect. */
    return true;
#    endif
}

static void era_via_system_boot_arm(void) {
    /* A duplicate/retried SET must not extend the fallback forever. */
    if (boot_state != ERA_VIA_SYSTEM_BOOT_IDLE) {
        return;
    }
    boot_state            = ERA_VIA_SYSTEM_BOOT_WAIT_SAVE;
    boot_phase_started_ms = timer_read32();
}

static void era_via_system_boot_note_save(void) {
    if (boot_state != ERA_VIA_SYSTEM_BOOT_WAIT_SAVE) {
        return;
    }
    /* Receiving SAVE is proof that the host got the preceding SET response. */
    boot_state            = ERA_VIA_SYSTEM_BOOT_WAIT_STATE_SYNC;
    boot_phase_started_ms = timer_read32();
}

static void era_via_system_boot_note_state_sync_response(void) {
    if (boot_state == ERA_VIA_SYSTEM_BOOT_WAIT_STATE_SYNC) {
        /* This request can only follow the SAVE response in the normal VIA
         * lifecycle. era_state_sync_via_command() has already enqueued its OK
         * response when this transition runs; the task waits for that exact
         * RAW IN transfer to drain before leaving the image. */
        boot_state = ERA_VIA_SYSTEM_BOOT_WAIT_RAW_IN_DRAIN;
    }
}

static void era_via_system_boot_note_reconciliation_progress(bool state_sync_request) {
    if (boot_state != ERA_VIA_SYSTEM_BOOT_WAIT_STATE_SYNC || state_sync_request) {
        return;
    }
    /* SAVE can be followed by an arbitrarily sized config reconciliation before
     * normal State Sync polling resumes. Do not let the fallback cut through
     * that live request/response sequence: every non-State-Sync RAW request is
     * forward progress and receives a fresh inactivity window. State Sync does
     * not extend it; an OK response closes the normal lifecycle below, while an
     * unsupported/malformed poll therefore cannot phase-lock the fallback. */
    boot_phase_started_ms = timer_read32();
}

static bool era_via_system_boot_fallback_due(void) {
    if (boot_state != ERA_VIA_SYSTEM_BOOT_WAIT_SAVE && boot_state != ERA_VIA_SYSTEM_BOOT_WAIT_STATE_SYNC) {
        return false;
    }
    return timer_elapsed32(boot_phase_started_ms) >= ERA_VIA_SYSTEM_BOOT_PHASE_FALLBACK_MS;
}
#endif

/* QMK declares this hook nowhere: via.c defines it weak and calls it at the top
 * of raw_hid_receive() for every report, taking `true` as "fully handled,
 * response already sent". This override stamps the arrival and fully handles
 * only GET_KEYBOARD_VALUE selector 0x06 (state-sync revisions). Every other
 * report returns false so the VIA dispatcher keeps ownership.
 *
 * "Raw HID" is exact. The stamp is taken in via_command_kb(), the first call
 * raw_hid_receive() makes for every report on the RAW OUT endpoint, which is
 * the VIA interface and nothing else: the hid_listen console is an IN-only
 * endpoint (console_task() flushes a buffered IN, tmk_core/protocol/chibios/
 * usb_main.c) and cannot reach it, so a diagnostics profile printing at full
 * rate leaves both gates exactly as quiet as a release build. */
bool via_command_kb(uint8_t *data, uint8_t length);
bool via_command_kb(uint8_t *data, uint8_t length) {
    via_last_raw_hid_ms = timer_read32();
#ifdef ERA_VIA_BOOTLOADER_ENABLE
    bool state_sync_request = data != NULL && length >= 2 && data[0] == id_get_keyboard_value && data[1] == ERA_STATE_SYNC_KEYBOARD_VALUE;
    era_via_system_boot_note_reconciliation_progress(state_sync_request);
#endif
    bool handled = era_state_sync_via_command(data, length);
#ifdef ERA_VIA_BOOTLOADER_ENABLE
    /* The State Sync unit has already converted the request buffer to the
     * response and enqueued it. Only an OK envelope closes the normal boot
     * lifecycle; malformed/unsupported envelopes fall back rather than being
     * mistaken for reconciliation. */
    if (handled && data[0] == id_get_keyboard_value && data[1] == ERA_STATE_SYNC_KEYBOARD_VALUE && data[3] == ERA_STATE_SYNC_STATUS_OK) {
        era_via_system_boot_note_state_sync_response();
    }
#endif
    return handled;
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

bool era_via_system_eeprom_invalidate(void) {
    /* PREPARE is exactly one result-bearing word. The ERA NVM path does not
       claim success from the live RAM image: it replays the production bank
       parser and proves that an ordinary next boot recovers MAGIC_OFF. QMK's
       following eeconfig_init_quantum() then calls nvm_eeconfig_erase(), whose
       custom-driver format is the whole-store ERA NVM format, before defaults
       are rebuilt. */
    const uint16_t invalid_magic = EECONFIG_MAGIC_NUMBER_OFF;

#if defined(ERA_HOST_PEER_STORAGE_V1_ENABLE) && defined(EEPROM_CUSTOM)
    era_nvm_result_t result = era_eeprom_driver_prepare_reboot_word(ERA_STORAGE_EECONFIG_MAGIC_ADDR, invalid_magic);
    return result == ERA_NVM_RESULT_OK || result == ERA_NVM_RESULT_NO_CHANGE;
#else
    eeprom_update_word(EECONFIG_MAGIC, invalid_magic);
    return eeprom_read_word(EECONFIG_MAGIC) == invalid_magic;
#endif
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

        /* Nothing is written here. The three confirms only hand the request to
         * the split agreement. That service applies the quiet gate, bilateral
         * physical boot-replay proof, and shared commit in order. */
        if (era_via_system_eeprom_clean_handed_off()) {
            return;
        }

        eeprom_reset_pending      = true;
        eeprom_reset_requested_ms = timer_read32();
    }
}
#endif

void era_via_system_task(void) {
#ifdef ERA_VIA_BOOTLOADER_ENABLE
    if (boot_state != ERA_VIA_SYSTEM_BOOT_IDLE) {
        bool lifecycle_complete = boot_state == ERA_VIA_SYSTEM_BOOT_WAIT_RAW_IN_DRAIN;
        if (!lifecycle_complete && !era_via_system_boot_fallback_due()) {
            return;
        }

        /* Even a fallback cuts only after the currently queued/transmitting
         * RAW IN response is gone. WAIT_SAVE has a fixed deadline, so a 500 ms
         * pre-SAVE poll cannot starve it. WAIT_STATE_SYNC is instead measured
         * from the last non-State-Sync reconciliation request, so it never cuts
         * ongoing config traffic; State Sync itself does not extend that timer.
         * The final predicate asks for endpoint drain rather than 500 ms of
         * silence. */
        if (!era_via_system_boot_raw_hid_in_inactive()) {
            return;
        }

        boot_state = ERA_VIA_SYSTEM_BOOT_IDLE;
        reset_keyboard();
        return;
    }
#endif

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
    /* A non-split board has no peer vote: ordinary logical invalidation, then
       QMK's controlled-reset path. */
    if (!era_via_system_eeprom_invalidate()) {
        return;
    }
    soft_reset_keyboard();
#endif
}

static bool era_via_system_set_value(uint8_t value_id, uint8_t *value_data, uint8_t length) {
#ifdef ERA_VIA_BOOTLOADER_ENABLE
    if (value_id == ERA_VIA_SYSTEM_BOOTLOADER_VALUE_ID) {
        if (value_data[0]) {
            era_via_system_boot_arm();
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
#ifdef ERA_VIA_BOOTLOADER_ENABLE
        era_via_system_boot_note_save();
#endif
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

#ifdef ERA_VIA_SYSTEM_TEST
void era_via_system_test_reset(void) {
    via_last_raw_hid_ms = 0;
#    ifdef ERA_VIA_BOOTLOADER_ENABLE
    boot_state                    = ERA_VIA_SYSTEM_BOOT_IDLE;
    boot_phase_started_ms         = 0;
    boot_test_raw_hid_in_inactive = true;
#    endif
#    ifdef ERA_EEPROM_CLEAN_ENABLE
    eeprom_reset_confirm            = 0;
    eeprom_reset_pending            = false;
    eeprom_reset_requested_ms       = 0;
    eeprom_reset_confirm_started_ms = 0;
#    endif
}

void era_via_system_test_set_raw_hid_in_inactive(bool inactive) {
#    ifdef ERA_VIA_BOOTLOADER_ENABLE
    boot_test_raw_hid_in_inactive = inactive;
#    else
    (void)inactive;
#    endif
}
#endif
