// SPDX-License-Identifier: GPL-2.0-or-later
#include "era_tomak_sync_via.h"
#include "era_tomak_sync_admission.h"
#include "era_wire.h"
#include "era_usb_session.h"
#include "../split/era_split_authority_reducer.h"
#include "../split/era_split_scheduler_events.h"
#include "../split/era_tomak_sync_runtime.h"
#include "../split/era_split_sync_policy.h"
#include "via.h"
#include "raw_hid.h"
#include "timer.h"
#include "usb_device_state.h"
#include <string.h>
#ifdef ERA_TOMAK_SYNC_TIMING
#include "../split/era_host_peer_transaction.h"
#include "../split/era_host_peer_storage.h"
#include "print.h"
#include "sync_timer.h"
#endif

/* Only mutation classes enter the gate. Reads/capabilities/STATUS remain on
 * their normal path even while one write waits for a presentation boundary. */
static bool mutation(const uint8_t *request) {
    switch (request[0]) {
        case id_set_keyboard_value:
            return request[1] == id_layout_options;
        case id_dynamic_keymap_set_keycode:
        case id_dynamic_keymap_set_encoder:
        case id_dynamic_keymap_set_buffer:
        case id_dynamic_keymap_reset:
        case id_dynamic_keymap_macro_set_buffer:
        case id_dynamic_keymap_macro_reset:
            return true;
        case id_custom_set_value:
        case id_custom_save:
            if (request[1] == ERA_WIRE_CHANNELS_EERRAA_SYSTEM) {
                return false; /* RESET/boot lifecycle owns its own agreement. */
            }
            if (request[1] == ERA_WIRE_CHANNELS_EERRAA_MACRO_REPLACE) {
                return request[2] >= ERA_WIRE_MACRO_REPLACE_BEGIN && request[2] <= ERA_WIRE_MACRO_REPLACE_COMMIT;
            }
            return true;
        default:
            return false;
    }
}

static void failure(uint8_t *request) {
    /* Preserve the original echo. ERA has an explicit result byte; legacy
     * mutation commands already use id_unhandled for persistence failure. */
    if (request[0] == id_custom_set_value && request[1] == ERA_WIRE_CHANNELS_EERRAA_MACRO_REPLACE) {
        request[31] = ERA_WIRE_MACRO_REPLACE_RESULT_BUSY;
    } else {
        request[0] = id_unhandled;
    }
    raw_hid_send(request, 32);
}

bool era_tomak_sync_via_admit(uint8_t *request, uint8_t length) {
    if (length != 32) {
        return false;
    }
    if (request[0] == id_custom_set_value && request[1] == ERA_WIRE_CHANNELS_EERRAA_MACRO_REPLACE &&
        request[2] == ERA_WIRE_MACRO_REPLACE_ABORT) {
        era_authority_snapshot_t authority;
        era_split_authority_reducer_get_snapshot(&authority);
        /* The slot owns only a not-yet-executed BEGIN. Its matching ABORT
         * retires that request without ever staging an NVM transaction. Other
         * ABORTs retain the storage handler's nonce/committed-state checks. */
        if (authority.valid && authority.usb_state == ERA_AUTH_USB_HOST_OPEN &&
            era_usb_session_configure_state() == USB_DEVICE_STATE_CONFIGURED) {
            uint8_t prefix[11];
            memcpy(prefix, request, sizeof(prefix));
            prefix[2] = ERA_WIRE_MACRO_REPLACE_BEGIN;
            if (era_tomak_sync_admission_cancel_matching(prefix, sizeof(prefix), authority.usb_epoch)) {
                request[31] = ERA_WIRE_MACRO_REPLACE_RESULT_OK;
                raw_hid_send(request, length);
                return true;
            }
        }
        return false;
    }
    bool terminal_set = request[0] == id_custom_set_value && request[1] == ERA_WIRE_CHANNELS_EERRAA_SYSTEM &&
        request[3] != 0 &&
        (request[2] == ERA_WIRE_SYSTEM_BOOTLOADER || request[2] == ERA_WIRE_SYSTEM_EEPROM_RESET_0 ||
         request[2] == ERA_WIRE_SYSTEM_EEPROM_RESET_1 || request[2] == ERA_WIRE_SYSTEM_EEPROM_RESET_DONE);
    if (request[0] == id_eeprom_reset || request[0] == id_bootloader_jump || terminal_set) {
        era_tomak_sync_admission_cancel();
        era_tomak_sync_runtime_preempt();
        return false;
    }
    bool requested = false;
    (void)era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_EEPROM, &requested);
    if (!mutation(request) || !era_tomak_sync_runtime_paired() || !requested) {
        return false;
    }
    if (era_tomak_sync_admission_pending()) {
        failure(request); /* Never overwrite the first client's request. */
        return true;
    }
    if (era_tomak_sync_runtime_pair_ready()) {
        return false;
    }
    era_authority_snapshot_t authority;
    era_split_authority_reducer_get_snapshot(&authority);
    if (!authority.valid || authority.usb_state != ERA_AUTH_USB_HOST_OPEN ||
        era_usb_session_configure_state() != USB_DEVICE_STATE_CONFIGURED ||
        era_tomak_sync_runtime_failed()) {
        failure(request);
        return true;
    }
    /* Client timeout is 5000ms. This bounds admission only, leaving the
     * existing handler/response budget separate; no automatic retry assumed. */
    (void)era_tomak_sync_admission_hold(request, authority.usb_epoch, timer_read32(), 500);
    era_split_transport_scheduler_mark_maintenance_due(ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE);
    return true;
}

void era_tomak_sync_via_task(void) {
    if (!era_tomak_sync_admission_pending()) {
        return;
    }
    era_authority_snapshot_t authority;
    era_split_authority_reducer_get_snapshot(&authority);
    if (!authority.valid || authority.usb_state != ERA_AUTH_USB_HOST_OPEN ||
        era_usb_session_configure_state() != USB_DEVICE_STATE_CONFIGURED) {
        era_tomak_sync_admission_cancel();
        return;
    }
    uint8_t request[32];
#ifdef ERA_TOMAK_SYNC_TIMING
    /* Capture while the admission slot still owns the pending fact. Taking an
     * expired request retires it, so a later read alone would hide its onset. */
    uint8_t indicator_before_take = era_host_peer_storage_indicator_diag();
    bool exclusive_before_take = era_host_peer_storage_route_exclusive();
#endif
    era_tomak_admission_result_t result = era_tomak_sync_admission_take(
        request, authority.usb_epoch, timer_read32(),
        era_tomak_sync_runtime_pair_ready() || era_tomak_sync_runtime_failed());
    if (result == ERA_TOMAK_ADMISSION_EXECUTE) {
        if (era_tomak_sync_runtime_failed()) {
            failure(request);
        } else {
            via_command_execute(request, sizeof(request));
        }
    } else if (result == ERA_TOMAK_ADMISSION_EXPIRED) {
        failure(request);
#ifdef ERA_TOMAK_SYNC_TIMING
        /* Failure-only observation after replying: no timing cost on a
         * successful upload and no additional admission/presentation state. */
        era_tomak_sync_binding_t binding;
        era_tomak_sync_wire_t proof;
        era_tomak_sync_runtime_snapshot(&binding, &proof);
        uprintf("tm_gate storage before=%02X after=%02X exclusive=%u\n",
                indicator_before_take, era_host_peer_storage_indicator_diag(), exclusive_before_take);
        uprintf("tm_gate timeout cmd=%u op=%u source=%u anchor=%u paired=%u ready=%u failed=%u\n",
                request[0], request[2],
                sync_timer_is_time_source(),
                era_host_peer_transaction_time_anchor_adopted(),
                era_tomak_sync_runtime_paired(), era_tomak_sync_runtime_pair_ready(),
                era_tomak_sync_runtime_failed());
        uprintf("tm_gate local=%u/%u peer=%u/%u usb=%u/%u/%u seq=%u proof=%u target=%u deadline=%lu\n",
                binding.local_owner, binding.local_relation, binding.peer_owner, binding.peer_relation,
                binding.peer_usb_epoch, binding.peer_host_open, binding.peer_host_close,
                proof.sequence, proof.operation, proof.target, (unsigned long)proof.deadline_ms);
#endif
    }
}
