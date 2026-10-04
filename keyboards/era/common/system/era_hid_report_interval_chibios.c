// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* The ChibiOS half of era_hid_report_interval: the system tick as the clock,
 * the endpoint queues as the sink, and the hooks the forked core files call
 * (tmk_core/protocol/chibios/usb_main.c, usb_driver.c; era_qmk_fork_ledger.md).
 * The rules are era_hid_report_contract.md, **Tap width and the report
 * interval**; nothing here decides a width. */

#include "era_hid_report_interval.h"

#if defined(ERA_HID_REPORT_INTERVAL_ENABLE)

#    include <ch.h>
#    include <hal.h>

#    include "host.h"
#    include "action_util.h"
#    include "keycode_config.h"
#    include "usb_device_state.h"
#    include "usb_main.h"

/* Declared per translation unit, as the ChibiOS protocol files do: the array
   is defined in tmk_core/protocol/chibios/usb_endpoints.c and no header
   exports it. */
extern usb_endpoint_in_t usb_endpoints_in[USB_ENDPOINT_IN_COUNT];

static bool era_hid_report_interval_ready;
static volatile bool era_hid_reopen;
static uint8_t era_hid_format = UINT8_MAX;
static volatile uint32_t era_hid_protocol_events;
static uint32_t era_hid_protocol_seen;
/* RESET/unconfigure end a session; pending SUSPEND does not. Keep this
 * independent of admission's stricter pending-event predicate. */
static volatile uint32_t era_hid_session_events;
static uint32_t era_hid_format_session;
/* A local format choice does not reset the host's old interface. Retire its
 * final held state separately from taps, which end at the format boundary.
 * One empty per keyboard lane is sufficient even across repeated toggles. */
static volatile uint8_t era_hid_retire_pending;
/* USB acceptance can still be flushed by same-session SUSPEND. Retain the
 * retired-interface obligation for that session, independently of a queued
 * empty. Only inactive lanes need rearming; reopen restores the active owner. */
static volatile uint8_t era_hid_retire_debt;
static volatile uint32_t era_hid_suspend_events;
static const uint8_t *era_hid_retire_dispatch;
static volatile bool era_hid_retire_reset;
extern keymap_config_t keymap_config;
bool era_usb_auxiliary_reports_pending(void);
void era_usb_auxiliary_reports_task(void);
static uint8_t era_hid_report_interval_current_format(void);

static void era_hid_report_interval_retire_apply_reset(void) {
    if (!era_hid_retire_reset) return;
    osalSysLock();
    if (era_hid_retire_reset) {
        era_hid_retire_reset = false;
        era_hid_retire_pending = 0;
        era_hid_retire_debt = 0;
    }
    osalSysUnlock();
}

static void era_hid_report_interval_retire_one(uint8_t bit, uint8_t endpoint, const void *report, size_t size) {
    uint32_t suspend = era_hid_suspend_events;
    era_hid_retire_dispatch = report;
    if (send_report(endpoint, (void *)report, size)) {
        osalSysLock();
        /* An ISR after admission may have flushed the packet and rearmed this
         * very bit. Do not erase that newer obligation on returning to Core0. */
        if (suspend == era_hid_suspend_events) era_hid_retire_pending &= (uint8_t)~bit;
        osalSysUnlock();
    }
    era_hid_retire_dispatch = NULL;
}

static void era_hid_report_interval_retire_task(void) {
    era_hid_report_interval_retire_apply_reset();
    if (!era_hid_retire_pending) return;
    static const report_keyboard_t empty_keyboard;
    if (era_hid_retire_pending & 1U) {
        era_hid_report_interval_retire_one(1U, USB_ENDPOINT_IN_KEYBOARD, &empty_keyboard, sizeof(empty_keyboard));
    }
#    ifdef NKRO_ENABLE
    static const report_nkro_t empty_nkro = {.report_id = REPORT_ID_NKRO};
    if (era_hid_retire_pending & 2U) {
        era_hid_report_interval_retire_one(2U, USB_ENDPOINT_IN_SHARED, &empty_nkro, sizeof(empty_nkro));
    }
#    endif
}

static inline uint32_t era_hid_report_interval_now(void) {
    /* The RP2040 port's system tick is the 1 MHz hardware timer read, safe in
       every context, and wraps in 71 minutes: far beyond any width. */
    return (uint32_t)chVTGetSystemTimeX();
}

static void era_hid_report_interval_ensure_ready(void) {
    if (!era_hid_report_interval_ready) {
        era_hid_report_interval_init((uint32_t)TIME_MS2I(1));
        era_hid_report_interval_ready = true;
    }
}

void era_hid_report_interval_platform_init(void) {
    era_hid_report_interval_ensure_ready();
}

static int era_hid_report_interval_lane_of(uint8_t endpoint_lut) {
    if (endpoint_lut == USB_ENDPOINT_IN_KEYBOARD) {
        return ERA_HID_REPORT_LANE_KEYBOARD;
    }
#    if defined(SHARED_EP_ENABLE)
    if (endpoint_lut == USB_ENDPOINT_IN_SHARED) {
        return ERA_HID_REPORT_LANE_SHARED;
    }
#    endif
    return -1;
}

static usb_endpoint_in_lut_t era_hid_report_interval_lut_of(uint8_t lane) {
#    if defined(SHARED_EP_ENABLE)
    if (lane == ERA_HID_REPORT_LANE_SHARED) {
        return USB_ENDPOINT_IN_SHARED;
    }
#    endif
    return USB_ENDPOINT_IN_KEYBOARD;
}

/* --- hooks called by the forked core files ------------------------------ */

bool era_hid_report_interval_hold_report(uint8_t endpoint_lut, const void *report, size_t size) {
    int lane = era_hid_report_interval_lane_of(endpoint_lut);
    if (lane < 0 || size > ERA_HID_REPORT_INTERVAL_REPORT_BYTES) {
        return false;
    }
    era_hid_report_interval_ensure_ready();
    era_hid_report_interval_retire_task();
    uint32_t session = era_hid_session_events;
    bool owned = era_hid_report_interval_hold((uint8_t)lane, report, (uint8_t)size);
    /* A current-format output already owned in the new session establishes
     * its lifetime, even before the reopen task. Do not delete that fresh
     * prefix merely to consume a same-format reset on the next service. */
    if (owned && session == era_hid_session_events && !era_hid_retire_reset &&
        era_hid_protocol_events == era_hid_protocol_seen &&
        era_hid_format == era_hid_report_interval_current_format() &&
        (lane == ERA_HID_REPORT_LANE_SHARED ? era_hid_format == 2 : era_hid_format != 2))
        era_hid_format_session = session;
    return owned;
}

void era_hid_report_interval_note_report_posted(uint8_t endpoint_lut) {
    int lane = era_hid_report_interval_lane_of(endpoint_lut);
    if (lane >= 0) {
        era_hid_report_interval_note_posted((uint8_t)lane);
    }
}

void era_hid_report_interval_note_keyboard_report_posted(uint8_t endpoint_lut) {
    int lane = era_hid_report_interval_lane_of(endpoint_lut);
    if (lane >= 0) {
        era_hid_report_interval_note_keyboard_posted((uint8_t)lane);
    }
}

/* Only these endpoints have an owner that retries an unaccepted void output.
 * RAW and other independent producers keep their existing timed transport. */
bool era_hid_report_interval_endpoint_owned(const void *endpoint) {
    if (endpoint == &usb_endpoints_in[USB_ENDPOINT_IN_KEYBOARD]) return true;
#    if defined(SHARED_EP_ENABLE)
    if (endpoint == &usb_endpoints_in[USB_ENDPOINT_IN_SHARED]) return true;
#    endif
#    if defined(MOUSE_ENABLE)
    if (endpoint == &usb_endpoints_in[USB_ENDPOINT_IN_MOUSE]) return true;
#    endif
    return false;
}

/* Successful whole-buffer admission, USB lock held before posting. */
void era_hid_report_interval_endpoint_posted_i(const void *endpoint) {
    if (endpoint == &usb_endpoints_in[USB_ENDPOINT_IN_KEYBOARD]) {
        era_hid_report_interval_note_posted(ERA_HID_REPORT_LANE_KEYBOARD); return;
    }
#    if defined(SHARED_EP_ENABLE)
    if (endpoint == &usb_endpoints_in[USB_ENDPOINT_IN_SHARED]) era_hid_report_interval_note_posted(ERA_HID_REPORT_LANE_SHARED);
#    endif
}

/* ISR context, system lock held by the caller: plain stores only. */
void era_hid_report_interval_endpoint_completed_i(const void *endpoint) {
    if (endpoint == &usb_endpoints_in[USB_ENDPOINT_IN_KEYBOARD]) {
        era_hid_report_interval_note_completed(ERA_HID_REPORT_LANE_KEYBOARD, era_hid_report_interval_now());
        return;
    }
#    if defined(SHARED_EP_ENABLE)
    if (endpoint == &usb_endpoints_in[USB_ENDPOINT_IN_SHARED]) {
        era_hid_report_interval_note_completed(ERA_HID_REPORT_LANE_SHARED, era_hid_report_interval_now());
    }
#    endif
}

void era_hid_report_interval_session_edge_i(bool keep_backlog) {
    era_hid_report_interval_note_session_edge(keep_backlog);
    era_hid_reopen = true;
    if (keep_backlog) {
        ++era_hid_suspend_events;
        if (!era_hid_retire_reset && era_hid_protocol_events == era_hid_protocol_seen) {
            uint8_t active_lane = era_hid_format == 2 ? 2U : 1U;
            era_hid_retire_pending |= era_hid_retire_debt & (uint8_t)~active_lane;
        }
    } else {
        ++era_hid_session_events;
        era_hid_retire_reset = true;
        era_hid_retire_debt = 0;
    }
}

void era_hid_report_interval_protocol_edge_i(void) {
    ++era_hid_protocol_events;
    era_hid_reopen = true;
}

static uint8_t era_hid_report_interval_current_format(void) {
    uint8_t format = usb_device_state_get_protocol() == USB_PROTOCOL_BOOT ? 0 : 1;
#    ifdef NKRO_ENABLE
    if (host_can_send_nkro() && keymap_config.nkro) format = 2;
#    endif
    return format;
}

/* The task's earlier session/format sample is not an admission grant: a USB
 * ISR may have ended that lifetime before this lock. Auxiliary reports only
 * wait for session accounting, never for keyboard format or tap intervals. */
bool era_hid_report_interval_endpoint_allowed_i(const void *endpoint, const uint8_t *data) {
    bool keyboard = endpoint == &usb_endpoints_in[USB_ENDPOINT_IN_KEYBOARD];
#    if defined(SHARED_EP_ENABLE)
    bool shared = endpoint == &usb_endpoints_in[USB_ENDPOINT_IN_SHARED];
    if (shared && data[0] == REPORT_ID_NKRO) keyboard = true;
#    else
    bool shared = false;
    (void)data;
#    endif
    if (!keyboard && !shared) return true;
    if (!era_hid_report_interval_session_current()) return false;
    if (!keyboard) return true;
    if (era_hid_protocol_events != era_hid_protocol_seen || era_hid_retire_reset) return false;
    /* Only this dispatch can send the terminal empty through a retired lane.
     * Idle storage and ordinary producers cannot borrow the exception. */
    if (data == era_hid_retire_dispatch) return true;
    uint8_t current = era_hid_report_interval_current_format();
    return !era_hid_retire_pending && era_hid_format == current &&
           (shared ? current == 2 : current != 2);
}

/* --- the requester's entry: QMK's host layer ----------------------------- */

void host_keyboard_delay(uint16_t delay_ms) {
    era_hid_report_interval_ensure_ready();
    era_hid_report_interval_request(delay_ms, era_hid_report_interval_now());
}

/* --- the unit's sink ------------------------------------------------------ */

bool era_hid_report_interval_platform_post(uint8_t lane, const uint8_t *data, uint8_t size) {
    return send_report(era_hid_report_interval_lut_of(lane), (void *)data, size);
}
/* --- the per-pass service, from the ERA class skeleton ------------------- */

bool era_hid_report_interval_select_format(uint8_t format) {
    era_hid_report_interval_ensure_ready();
    uint32_t events = era_hid_protocol_events;
    uint32_t session = era_hid_session_events;
    if (format == era_hid_format && events == era_hid_protocol_seen &&
        session == era_hid_format_session && !era_hid_retire_reset) return false;
    osalSysLock();
    /* The caller's format argument predates this lock. Never pair a stale
     * argument with a newer host event serial and consume that host edge. */
    if (format != era_hid_report_interval_current_format()) {
        osalSysUnlock();
        return false;
    }
    events = era_hid_protocol_events;
    session = era_hid_session_events;
    if (era_hid_retire_reset) {
        era_hid_retire_reset = false;
        era_hid_retire_pending = 0;
        era_hid_retire_debt = 0;
    }
    if (format == era_hid_format && events == era_hid_protocol_seen) {
        era_hid_format_session = session;
        osalSysUnlock();
        return false;
    }
    if (events != era_hid_protocol_seen) {
        /* Host protocol changes end the old format lifetime, including ABA. */
        era_hid_retire_pending = 0;
        era_hid_retire_debt = 0;
    } else if (era_hid_format != UINT8_MAX && era_hid_format != format &&
               session == era_hid_format_session) {
        era_hid_retire_pending |= era_hid_format == 2 ? 2U : 1U;
        era_hid_retire_debt |= era_hid_format == 2 ? 2U : 1U;
    }
    era_hid_report_interval_format_edge();
    era_hid_format = format; era_hid_protocol_seen = events;
    era_hid_format_session = session;
    osalSysUnlock();
    return true;
}

/* Idle storage belongs to the last accepted payload, not the live QMK owner
 * or the current format lifetime. Rebuild keyboard idle through the ordinary
 * producer, and let pending releases/widths drain before requesting it again. */
bool era_hid_report_interval_handle_idle(uint8_t endpoint_lut, uint8_t report_id) {
    bool keyboard = endpoint_lut == USB_ENDPOINT_IN_KEYBOARD;
#    if defined(SHARED_EP_ENABLE) && defined(NKRO_ENABLE)
    bool nkro = endpoint_lut == USB_ENDPOINT_IN_SHARED && report_id == REPORT_ID_NKRO;
#    else
    bool nkro = false;
    (void)report_id;
#    endif
    if (!keyboard && !nkro) return false;
    uint8_t current = era_hid_report_interval_current_format();
    if (!era_hid_reopen && !era_hid_retire_pending && !era_hid_retire_reset &&
        !era_hid_report_interval_active() && era_hid_report_interval_session_current() &&
        era_hid_protocol_events == era_hid_protocol_seen && era_hid_format == current &&
        (nkro ? current == 2 : current != 2)) resend_keyboard_report();
    return true;
}

void era_hid_report_interval_service(void) {
    /* Idle needs only scalar state: no USB lock, clock or endpoint walk.
     * Keep the format comparison for QMK/VIA/storage NKRO changes that do
     * not arrive through the USB ISR. An ISR after this test stays pending
     * for the next pass; endpoint admission still checks the live lifetime. */
    if (!era_hid_reopen && !era_hid_retire_pending && !era_hid_retire_reset &&
        !era_hid_report_interval_active() && !era_usb_auxiliary_reports_pending() &&
        era_hid_format == era_hid_report_interval_current_format()) return;
    era_hid_report_interval_ensure_ready();
    bool active;
    osalSysLock();
    active = usbGetDriverStateI(&USB_DRIVER) == USB_ACTIVE &&
             !bqIsSuspendedX(&usb_endpoints_in[USB_ENDPOINT_IN_KEYBOARD].obqueue);
    osalSysUnlock();
    if (!active) return;
    if (era_hid_report_interval_select_format(era_hid_report_interval_current_format())) era_hid_reopen = true;
    era_hid_report_interval_retire_task();
    if (era_hid_report_interval_active()) era_hid_report_interval_task(era_hid_report_interval_now());
    osalSysLock();
    bool reopen = era_hid_reopen;
    era_hid_reopen = false;
    osalSysUnlock();
    if (reopen) force_send_keyboard_report();
    era_usb_auxiliary_reports_task();
}
#endif /* ERA_HID_REPORT_INTERVAL_ENABLE */
