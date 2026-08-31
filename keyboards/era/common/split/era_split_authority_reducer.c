// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "era_split_authority_reducer.h"

#include <string.h>

#include "../system/era_usb_session.h"
#include "action_util.h"
#include "atomic_util.h"
#include "split_util.h"
#include "timer.h"
#ifdef PROTOCOL_CHIBIOS
#    include <hal.h>
#    include "usb_main.h"
#endif


#ifndef ERA_SPLIT_AUTHORITY_SUSPEND_HOST_GRACE_MS
#    define ERA_SPLIT_AUTHORITY_SUSPEND_HOST_GRACE_MS 500
#endif

/* ERA derives no authority from VBUS. Neither selector is read by this file any
   more - the boot probe that used them is gone - but both guards stay, because
   they are what keeps VBUS out of the tree at all: QMK's own weak
   is_keyboard_master_impl() falls back to usb_vbus_state() when SPLIT_USB_DETECT
   is absent, and that fallback becomes live the moment the ERA override below
   is removed. */
#if defined(MCU_RP)
#    if !defined(SPLIT_USB_DETECT)
#        error "ERA RP2040 split authority requires SPLIT_USB_DETECT; VBUS role selection is forbidden."
#    endif

#    if defined(USB_VBUS_PIN)
#        error "ERA RP2040 split authority forbids USB_VBUS_PIN; VBUS role selection is forbidden."
#    endif
#endif

typedef struct {
    uint16_t usb_epoch;
#if defined(MCU_RP) && defined(PROTOCOL_CHIBIOS)
    uint32_t usb_suspend_started_ms;
#endif
    uint8_t  local_side;
    bool     initialized;
#if defined(MCU_RP) && defined(PROTOCOL_CHIBIOS)
    bool     usb_suspend_seen;
#endif
    bool     accepted_local_host_open;
} era_split_authority_state_t;

static era_split_authority_state_t era_split_authority_state = {
    .usb_epoch = 1,
};

static uint16_t era_split_authority_next_nonzero_u16(uint16_t value) {
    value++;
    return value == 0 ? 1 : value;
}

static uint32_t era_split_authority_now(void) {
    uint32_t now = timer_read32();
    return now == 0 ? 1 : now;
}

/* Read the physical hand pin, not is_keyboard_left(). The reducer is
   initialized from keyboard_pre_init_kb, which quantum/keyboard.c reaches from
   keyboard_setup() through keyboard_pre_init_quantum(); QMK does not fill
   split_config.left until split_pre_init(), which the same file calls one
   function later from keyboard_init(). So is_keyboard_left() answers a zeroed
   .bss until then and a LEFT half would latch RIGHT. That is not cosmetic: the
   mode planner derives
   the peer-unknown wire initiator from the side, so both halves would come up
   as responder and discovery would never start. era_split_usb_identity_init()
   already reads this same pin at this same point for the USB serial, which is
   why the pin - not the QMK projection - is the input this init instant has.

   Called once and latched: the pin read reconfigures the GPIO and waits 100 us,
   which does not belong in the 5 ms authority poll, and handedness cannot
   change while the board runs. */
static uint8_t era_split_authority_read_local_side(void) {
    return split_hand_pin_is_left() ? ERA_SPLIT_AUTHORITY_SIDE_LEFT : ERA_SPLIT_AUTHORITY_SIDE_RIGHT;
}

/* Both of these are one-line projections of the reducer, and that is the whole
   point: there is exactly one derivation of local USB authority, and QMK reads
   it rather than caching a boot-time answer beside it.
 *
 * is_keyboard_master_impl() must not simply be deleted, even though ERA's own
 * code never calls it. split_pre_init() calls it as its first statement, and
 * QMK's weak version calls usb_disconnect() on every half it decides is not
 * master -- both in quantum/split_common/split_util.c, and that call carries no
 * guard, so it is the live arm on any build that does not override the
 * predicate. Dropping the D+ pull-up at boot would make
 * the runtime reducer's later promotion to HOST unreachable - the half could
 * never enumerate. The split_config.master it fills is write-only: the only
 * reader is split_util.c's weak is_keyboard_master(), which ERA overrides here.
 */
bool is_keyboard_master_impl(void) {
    return is_keyboard_master();
}

bool is_keyboard_master(void) {
    return era_split_authority_state.accepted_local_host_open;
}

/* One owner for the remap since 2026-08-11; this file carried a verbatim copy
   of it. What it means, and why an unconfigured SUSPEND must read as INIT, is
   at the definition in system/era_usb_session.c. */
static uint8_t era_split_authority_read_qmk_configure_state(void) {
    return era_usb_session_configure_state();
}

#if defined(MCU_RP) && defined(PROTOCOL_CHIBIOS)
static bool era_split_authority_preserve_suspended_host_locked(uint32_t now, bool previous_host_open) {
    if (!previous_host_open) {
        return false;
    }

    /*
     * RP2040 ChibiOS USB can leave split-cable-powered USB removal looking like
     * a configured suspend. Preserve only the short suspend transition.
     */
    if (!era_split_authority_state.usb_suspend_seen) {
        era_split_authority_state.usb_suspend_seen       = true;
        era_split_authority_state.usb_suspend_started_ms = now;
    }

    return TIMER_DIFF_32(now, era_split_authority_state.usb_suspend_started_ms) < ERA_SPLIT_AUTHORITY_SUSPEND_HOST_GRACE_MS;
}

static void era_split_authority_clear_suspend_tracking_locked(void) {
    era_split_authority_state.usb_suspend_seen       = false;
    era_split_authority_state.usb_suspend_started_ms = 0;
}
#endif

static bool era_split_authority_host_open_for_state_locked(uint8_t configure_state, bool sof_fresh, bool previous_host_open, uint32_t now) {
    /* Firmware USB bounce is not a host unplug. Without this hold, the
       unconfigured window steps usb_epoch and rotates a DUAL-HOST pair. */
    if (era_usb_session_firmware_reattach_hold() && previous_host_open) {
        return true;
    }
    if (configure_state == USB_DEVICE_STATE_SUSPEND) {
#if defined(MCU_RP) && defined(PROTOCOL_CHIBIOS)
        return era_split_authority_preserve_suspended_host_locked(now, previous_host_open);
#else
        return previous_host_open;
#endif
    }

#if defined(MCU_RP) && defined(PROTOCOL_CHIBIOS)
    era_split_authority_clear_suspend_tracking_locked();
#else
    (void)now;
#endif
    return configure_state == USB_DEVICE_STATE_CONFIGURED && sof_fresh;
}

static void era_split_authority_set_host_open_locked(bool host_open) {
    if (era_split_authority_state.accepted_local_host_open == host_open) {
        return;
    }

    era_split_authority_state.accepted_local_host_open = host_open;
    era_split_authority_state.usb_epoch = era_split_authority_next_nonzero_u16(era_split_authority_state.usb_epoch);
}

static void era_split_authority_reduce_locked(uint8_t qmk_configure_state, uint8_t local_side, uint32_t now, bool sof_fresh) {
    era_split_authority_state.local_side = local_side;
    bool previous_host_open = era_split_authority_state.accepted_local_host_open;
    era_split_authority_set_host_open_locked(era_split_authority_host_open_for_state_locked(qmk_configure_state, sof_fresh, previous_host_open, now));
}

void era_split_authority_reducer_init(void) {
    if (era_split_authority_state.initialized) {
        return;
    }

    /* Outside the atomic block: the pin read waits 100 us, and nothing else can
       observe the reducer before the store below publishes `initialized`. */
    uint8_t local_side = era_split_authority_read_local_side();

    ATOMIC_BLOCK_RESTORESTATE {
        if (!era_split_authority_state.initialized) {
            era_split_authority_state.local_side  = local_side;
            era_split_authority_state.initialized = true;
        }
    }
}

bool era_split_authority_reducer_task(void) {
    uint8_t qmk_configure_state = era_split_authority_read_qmk_configure_state();
    /* Sampled HERE, adjacent to the evaluation below, and never read as a value
       some other pass left behind. The freshness window is 10 ms; a housekeeping
       pass stalled inside a sliced flash erase would hand this a stale age, and
       a false not-fresh closes host_open, steps usb_epoch and churns the wire
       role. The _Static_assert on POLL_PERIOD <= FRESH_MS bounds the cadence;
       nothing but this adjacency bounds the staleness.

       Unavailable means FRESH here and not-lost in the sleep decision -- the two
       answers are opposite on purpose, which is why the shared API reports an
       age plus an availability rather than a boolean. Collapsing it to one
       boolean silently inverts one of the two. */
    uint32_t sof_age_ms = 0;
    bool     sof_fresh  = !era_usb_session_sample_frame_age(&sof_age_ms) ||
                         sof_age_ms < ERA_SPLIT_AUTHORITY_SOF_FRESH_MS;
    uint32_t now = era_split_authority_now();
    uint8_t local_side = era_split_authority_state.local_side;
    if (local_side == ERA_SPLIT_AUTHORITY_SIDE_UNKNOWN) {
        local_side = era_split_authority_read_local_side();
    }

    bool hid_reopened;
    bool authority_changed;
    ATOMIC_BLOCK_RESTORESTATE {
        bool     previous_initialized = era_split_authority_state.initialized;
        uint8_t  previous_side = era_split_authority_state.local_side;
        bool     previous_host_open = era_split_authority_state.accepted_local_host_open;
        uint16_t previous_usb_epoch = era_split_authority_state.usb_epoch;

        era_split_authority_state.initialized = true;
        era_split_authority_reduce_locked(qmk_configure_state, local_side, now, sof_fresh);
        hid_reopened = !previous_host_open && era_split_authority_state.accepted_local_host_open;
        authority_changed = previous_initialized != era_split_authority_state.initialized ||
                            previous_side != era_split_authority_state.local_side ||
                            previous_host_open != era_split_authority_state.accepted_local_host_open ||
                            previous_usb_epoch != era_split_authority_state.usb_epoch;
    }

    if (hid_reopened) {
        send_keyboard_report();
    }
    return authority_changed;
}

void era_split_authority_reducer_get_snapshot(era_authority_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    ATOMIC_BLOCK_RESTORESTATE {
        snapshot->valid = era_split_authority_state.initialized;
        snapshot->is_left = era_split_authority_state.local_side == ERA_SPLIT_AUTHORITY_SIDE_LEFT;
        if (era_split_authority_state.accepted_local_host_open) {
            snapshot->usb_state = ERA_AUTH_USB_HOST_OPEN;
        } else if (era_split_authority_state.initialized) {
            snapshot->usb_state = ERA_AUTH_USB_NO_HOST;
        } else {
            snapshot->usb_state = ERA_AUTH_USB_UNKNOWN;
        }
        snapshot->usb_epoch = era_split_authority_state.usb_epoch;
    }
}
