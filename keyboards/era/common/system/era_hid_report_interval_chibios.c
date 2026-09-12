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
#    include "usb_main.h"

/* Declared per translation unit, as the ChibiOS protocol files do: the array
   is defined in tmk_core/protocol/chibios/usb_endpoints.c and no header
   exports it. */
extern usb_endpoint_in_t usb_endpoints_in[USB_ENDPOINT_IN_COUNT];

static bool era_hid_report_interval_ready;

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
}

/* --- the requester's entry: QMK's host layer ----------------------------- */

void host_keyboard_delay(uint16_t delay_ms) {
    era_hid_report_interval_ensure_ready();
    era_hid_report_interval_request(delay_ms, era_hid_report_interval_now());
}

/* --- the unit's sink ------------------------------------------------------ */

bool era_hid_report_interval_platform_post(uint8_t lane, const uint8_t *data, uint8_t size) {
    usb_endpoint_in_lut_t lut      = era_hid_report_interval_lut_of(lane);
    usb_endpoint_in_t    *endpoint = &usb_endpoints_in[lut];
    bool                  room;

    osalSysLock();
    /* A queue that timed out is one the host is not reading; posting to it is
       the driver's own drop-and-reset path, which is better than holding the
       backlog for a host that will not come back. */
    room = usbGetDriverStateI(&USB_DRIVER) == USB_ACTIVE && (!obqIsFullI(&endpoint->obqueue) || endpoint->timed_out);
    osalSysUnlock();
    if (!room) {
        return false;
    }
    /* With room, send_report() returns without waiting. Its own failure path
       is the driver's existing one: the report is gone with the queue, as it
       would have been on the direct path, so the entry is consumed. */
    (void)send_report(lut, (void *)data, size);
    return true;
}

/* --- the per-pass service, from the ERA class skeleton ------------------- */

void era_hid_report_interval_service(void) {
    if (!era_hid_report_interval_active()) {
        return;
    }
    era_hid_report_interval_ensure_ready();
    era_hid_report_interval_task(era_hid_report_interval_now());
}

#endif /* ERA_HID_REPORT_INTERVAL_ENABLE */
