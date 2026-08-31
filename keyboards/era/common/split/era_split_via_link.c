// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_via_link.h"

#include "../system/era_usb_session.h"
#include "../system/era_via_system.h"
#include "era_split_link.h"
#include "era_split_restart_agreement.h"
#include "timer.h"
#include "via.h"
#if defined(PROTOCOL_CHIBIOS)
#    include "usb_main.h"
#endif

/* The dropdown labels name the baud, so a build that moved the rate would ship
 * a page that lies about what it does. Medium and Low are derived from this
 * value, so all three labels move together and one refusal covers the set.
 * No apostrophe in the message: the preprocessor lexes the text of a skipped
 * group, so one reads as an unterminated character constant and -Werror fails
 * every build this refusal is not for. */
#if SERIAL_USART_SPEED != 460800
#    error "The SYSTEM page states the link speeds as 460800 / 230400 / 115200. A board that changes ERA_SPLIT_SERIAL_USART_SPEED must restate the three labels in its VIA definition before this check is relaxed."
#endif

static bool     g_era_split_via_link_reattach_pending;
static uint32_t g_era_split_via_link_reattach_requested_ms;

static bool era_split_via_link_value_id(uint8_t value_id) {
    return value_id == ERA_SPLIT_VIA_LINK_LEVEL_VALUE_ID || value_id == ERA_SPLIT_VIA_LINK_APPLY_VALUE_ID;
}

void era_split_via_link_schedule_reattach(void) {
    g_era_split_via_link_reattach_pending      = true;
    g_era_split_via_link_reattach_requested_ms = timer_read32();
}

void era_split_via_link_task(void) {
    if (!g_era_split_via_link_reattach_pending) {
        return;
    }
    /* The bounce blocks in `restart_usb_driver()` (`wait_ms(50)`). Landing
       that inside T_commit misses the shared-clock apply. */
    if (era_split_restart_agreement_in_flight()) {
        return;
    }
    if (!era_via_system_restart_quiet_ok(g_era_split_via_link_reattach_requested_ms)) {
        return;
    }
    g_era_split_via_link_reattach_pending = false;
#if defined(PROTOCOL_CHIBIOS)
    era_usb_session_note_firmware_reattach();
    restart_usb_driver(&USB_DRIVER);
#endif
}

static bool era_split_via_link_set_value(uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)length;
    switch (value_id) {
        case ERA_SPLIT_VIA_LINK_LEVEL_VALUE_ID:
            /* The dropdown alone changes nothing on the wire and nothing in
               EEPROM. Refusing an out-of-range index rather than clamping is
               what keeps the read-back exact: a clamped write would answer the
               next get with a level the owner did not pick. */
            return era_split_link_set_pending_level(value_data[0]);
        case ERA_SPLIT_VIA_LINK_APPLY_VALUE_ID:
            /* A toggle-as-action, so only the rising edge means anything. The
               owner instruction is to hide a control the current setting makes
               inert, and this is the one that cannot be hidden -- VIA compares
               value ids on one page and the running level is not one of them --
               so the inertness lives in the firmware: an apply whose pending
               level already matches the running one does nothing at all. */
            if (value_data[0]) {
                /* The bounce is armed at commit, not here. An inert, refused,
                   or expired request must leave Enable on. */
                (void)era_split_link_request_apply();
            }
            return true;
        default:
            return false;
    }
}

static bool era_split_via_link_get_value(uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)length;
    switch (value_id) {
        case ERA_SPLIT_VIA_LINK_LEVEL_VALUE_ID:
            value_data[0] = era_split_link_pending_level();
            return true;
        case ERA_SPLIT_VIA_LINK_APPLY_VALUE_ID:
            /* The same shape the DFU and clean-confirm toggles take: an action
               has no state to report, so it reads back off. */
            value_data[0] = 0;
            return true;
        default:
            return false;
    }
}

bool era_split_via_link_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || data[1] != ERA_VIA_SYSTEM_CHANNEL || !era_split_via_link_value_id(data[2])) {
        return false;
    }

    uint8_t *command_id = &data[0];
    uint8_t *value_id   = &data[2];
    uint8_t *value_data = &data[3];

    switch (*command_id) {
        case id_custom_set_value:
            return era_split_via_link_set_value(*value_id, value_data, length);
        case id_custom_get_value:
            return era_split_via_link_get_value(*value_id, value_data, length);
        /* Claimed with nothing to do, and unreachable for a well-formed VIA
           report: a save command is `[id_custom_save, channel_id]` with no
           value id, so byte 2 is host padding and the gate above admits only 8
           or 9. It stays for the reason era_split_via_sync.c keeps its own --
           the pending level is held in RAM by design, so a save has nothing to
           flush. */
        case id_custom_save:
            return true;
        default:
            return false;
    }
}
