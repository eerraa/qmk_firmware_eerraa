// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef VIA_ENABLE
#    include "via.h"

/* **The value-command protocol, once.** Everything a channel-owning handler
   does after it has decided the command is addressed to it: claim a save,
   refuse a report too short to carry a value id, and answer a set by writing
   back what the value actually became. Callers keep their own addressing --
   which is where they genuinely differ, single channel against two against a
   value-id filter on a shared one -- and hand over only the three operations.

   The readback on the set arm is the part that earns the fold. It is an ERA
   convention rather than VIA's: a set may clamp or refuse, and VIA renders
   whatever comes back, so a handler that skips it shows the value the user
   asked for instead of the one the firmware holds. Five handlers spelled it
   out and it is the kind of line a sixth omits without anything failing.

   `channel_id` reaches the callbacks because it is part of the command, and
   one caller needs it: era_socd_via.c owns two channels and derives its pair
   from which one arrived. The others ignore it.

   Three units stay off it, and the reason is not the same for all three, which
   this paragraph used to claim. **The readback argument holds for exactly one
   case in the tree**: era_via_system.c's EEPROM-CLEAN confirm ids, where the
   set that completes the pair consumes the confirm mask
   (era_via_system_trigger_eeprom_reset_if_confirmed()), so the rail's
   set-then-get would report 0 where 1 was sent. It does not hold for the
   bootloader id beside it -- set(1) never returns and set(0)'s readback is the
   same 0 -- nor for era_split_via_sync.c, whose getter reads the live policy
   bit and would echo exactly what the setter stored. What actually keeps those
   two off the rail is that era_via_system.c is not only a value-command unit
   (it claims id_custom_save for the whole system channel), and that
   era_split_via_sync.c has simply never been on it. era_nkro_via.c echoes the
   effective bit itself for a third reason, and that one is as stated -- its set
   is not a plain store: it clears the keyboard around the write, and echoes
   what the bit became rather than what was asked. */
/* **`length` is not a length and nothing reads it.** `raw_hid_task()`
   (`tmk_core/protocol/chibios/usb_main.c`) declares `uint8_t buffer[RAW_EPSIZE]`
   and calls `raw_hid_receive(buffer, sizeof(buffer))` -- `sizeof`, a
   compile-time constant, not the number of bytes the host actually sent. So
   every ERA custom-value callback is entered with
   `length == RAW_EPSIZE == 32`, always, and `quantum/via.c` passes it down
   unchanged. State Sync's separate fixed-envelope command still validates the
   full 32-byte envelope before reading it (`system/era_state_sync.c`).

   ERA carried 33 `length` tests across the custom-value surface and not one of
   them could fire. They are gone, and none of them is coming back on a short-packet
   argument: a short OUT packet still arrives as 32 with the tail of an
   uninitialised stack buffer behind it, so a length test never saw the case a
   reader assumes it guards. Upstream agrees by construction -- neither
   `via_qmk_rgb_matrix_command()` nor a stock keyboard's
   `via_custom_value_command_kb()` tests `length` at all before indexing
   `data[3]`.

   What replaces them is a `_Static_assert` on `RAW_EPSIZE` in
   `system/era_common_via.c`, which unlike the tests can actually fail: ERA
   reads `data[0..4]` -- command, channel, value id, and a two-byte value for
   `era_socd_via.c`'s keycodes -- so a transport that ever made the report
   smaller stops the build instead of reading past it. It lives in the `.c`
   because `RAW_EPSIZE` comes from `usb_descriptor.h` and this header is
   included by board files that have no business pulling the USB descriptor in.

   The parameter itself stays on these signatures because it mirrors QMK's
   callback the whole way down, and a reader who wonders why nothing reads it
   finds this paragraph. */
typedef bool (*era_common_via_value_fn_t)(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length);
typedef void (*era_common_via_save_fn_t)(uint8_t channel_id);

enum {
    ERA_COMMON_VIA_LEGACY_TERM_UNIT_MS = 10,
    ERA_COMMON_VIA_LEGACY_TERM_STEP_MS = 20,
    ERA_COMMON_VIA_LEGACY_TERM_MIN_MS  = 100,
    ERA_COMMON_VIA_LEGACY_TERM_MAX_MS  = 500,
};

static inline uint16_t era_common_via_get_u16_be(const uint8_t *data) {
    return ((uint16_t)data[0] << 8) | data[1];
}

static inline void era_common_via_put_u16_be(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static inline uint16_t era_common_via_legacy_term_ms(uint8_t units) {
    return (uint16_t)units * ERA_COMMON_VIA_LEGACY_TERM_UNIT_MS;
}

static inline uint8_t era_common_via_legacy_term_units(uint16_t term_ms) {
    if (term_ms < ERA_COMMON_VIA_LEGACY_TERM_MIN_MS) {
        term_ms = ERA_COMMON_VIA_LEGACY_TERM_MIN_MS;
    } else if (term_ms > ERA_COMMON_VIA_LEGACY_TERM_MAX_MS) {
        term_ms = ERA_COMMON_VIA_LEGACY_TERM_MAX_MS;
    }
    uint16_t projected = ERA_COMMON_VIA_LEGACY_TERM_MIN_MS + (((term_ms - ERA_COMMON_VIA_LEGACY_TERM_MIN_MS) / ERA_COMMON_VIA_LEGACY_TERM_STEP_MS) * ERA_COMMON_VIA_LEGACY_TERM_STEP_MS);
    return (uint8_t)(projected / ERA_COMMON_VIA_LEGACY_TERM_UNIT_MS);
}

static inline bool era_common_via_value_command(uint8_t *data, uint8_t length, era_common_via_save_fn_t save, era_common_via_value_fn_t set, era_common_via_value_fn_t get) {
    uint8_t *command_id = &data[0];
    uint8_t  channel_id = data[1];

    if (*command_id == id_custom_save) {
        save(channel_id);
        return true;
    }

    uint8_t *value_id   = &data[2];
    uint8_t *value_data = &data[3];

    switch (*command_id) {
        case id_custom_set_value:
            if (!set(channel_id, *value_id, value_data, length)) {
                return false;
            }
            get(channel_id, *value_id, value_data, length);
            return true;
        case id_custom_get_value:
            return get(channel_id, *value_id, value_data, length);
        default:
            return false;
    }
}
#endif

/* The keyboard channel's deferred save fan-out, called by the quiet gate in
   system/era_board_hooks.c and by nothing else. It is separate from the router
   below because a VIA save carries no value id: the router has to answer the
   command, and the gate has to be able to run the same list half a second
   later. */
void era_common_via_keyboard_channel_save(void);

bool era_common_via_handle_system_command(uint8_t *data, uint8_t length);
bool era_common_via_handle_feature_command(uint8_t *data, uint8_t length);
bool era_common_via_handle_keyboard_channel_command(uint8_t *data, uint8_t length);
bool era_common_via_handle_command(uint8_t *data, uint8_t length);
