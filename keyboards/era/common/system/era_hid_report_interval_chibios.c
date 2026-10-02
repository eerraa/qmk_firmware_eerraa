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
extern keymap_config_t keymap_config;
void era_usb_auxiliary_reports_task(void);

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
    return era_hid_report_interval_hold((uint8_t)lane, report, (uint8_t)size);
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
    return era_hid_report_interval_session_current() &&
           (!keyboard || (era_hid_protocol_events == era_hid_protocol_seen &&
                          era_hid_format == era_hid_report_interval_current_format()));
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
    if (format == era_hid_format && events == era_hid_protocol_seen) return false;
    era_hid_report_interval_format_edge();
    era_hid_format = format; era_hid_protocol_seen = events;
    return true;
}
void era_hid_report_interval_service(void) {
    era_hid_report_interval_ensure_ready();
    bool active;
    osalSysLock();
    active = usbGetDriverStateI(&USB_DRIVER) == USB_ACTIVE &&
             !bqIsSuspendedX(&usb_endpoints_in[USB_ENDPOINT_IN_KEYBOARD].obqueue);
    osalSysUnlock();
    if (!active) return;
    if (era_hid_report_interval_select_format(era_hid_report_interval_current_format())) era_hid_reopen = true;
    if (era_hid_report_interval_active()) era_hid_report_interval_task(era_hid_report_interval_now());
    osalSysLock();
    bool reopen = era_hid_reopen;
    era_hid_reopen = false;
    osalSysUnlock();
    if (reopen) force_send_keyboard_report();
    era_usb_auxiliary_reports_task();
}
#endif /* ERA_HID_REPORT_INTERVAL_ENABLE */
