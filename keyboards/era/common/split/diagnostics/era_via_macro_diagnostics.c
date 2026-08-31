// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_via_macro_diagnostics.h"

#include <stdbool.h>
#include <string.h>

#include <ch.h>

#include "timer.h"
#include "usb_driver.h"
#include "usb_endpoints.h"
#include "via.h"

#include "../era_host_peer_storage.h"

extern usb_endpoint_in_t usb_endpoints_in[USB_ENDPOINT_IN_COUNT];

typedef struct {
    uint32_t interval_start_ms;
    uint32_t request_start_ms;
    uint32_t send_start_ms;
    uint32_t response_pending_ms;
    uint32_t completion_ms;
    bool     current_request_is_macro;
    bool     application_interval_clean;
} era_via_macro_diagnostics_runtime_t;

static era_via_macro_diagnostics_t         g_era_via_macro_diagnostics;
static era_via_macro_diagnostics_runtime_t g_era_via_macro_diagnostics_runtime;

static uint16_t era_via_macro_diagnostics_elapsed16(uint32_t elapsed_ms) {
    return elapsed_ms < UINT16_MAX ? (uint16_t)elapsed_ms : UINT16_MAX;
}

static void era_via_macro_diagnostics_add_duration(uint32_t elapsed_ms, uint32_t *total_ms, uint16_t *max_ms) {
    *total_ms += elapsed_ms;
    uint16_t bounded_ms = era_via_macro_diagnostics_elapsed16(elapsed_ms);
    if (bounded_ms > *max_ms) {
        *max_ms = bounded_ms;
    }
}

void era_via_macro_diagnostics_task(void) {
    if (!g_era_via_macro_diagnostics.response_pending ||
        !usb_endpoint_in_is_inactive(&usb_endpoints_in[USB_ENDPOINT_IN_RAW])) {
        return;
    }

    uint32_t now_ms     = timer_read32();
    uint32_t elapsed_ms = now_ms - g_era_via_macro_diagnostics_runtime.response_pending_ms;
    era_via_macro_diagnostics_add_duration(elapsed_ms,
                                           &g_era_via_macro_diagnostics.drain_total_ms,
                                           &g_era_via_macro_diagnostics.drain_max_ms);
    g_era_via_macro_diagnostics.drain_count++;
    g_era_via_macro_diagnostics.response_pending = 0;
    g_era_via_macro_diagnostics.completion_valid = g_era_via_macro_diagnostics_runtime.application_interval_clean ? 1 : 0;
    g_era_via_macro_diagnostics_runtime.completion_ms = now_ms;
}

void era_via_macro_diagnostics_receive(uint8_t command_id) {
    /* Close a response which completed between housekeeping passes before
     * classifying the time to this new request. */
    era_via_macro_diagnostics_task();

    bool     macro  = command_id == id_dynamic_keymap_macro_set_buffer;
    uint32_t now_ms = timer_read32();

    if (!macro) {
        if (g_era_via_macro_diagnostics_runtime.application_interval_clean &&
            (g_era_via_macro_diagnostics.response_pending || g_era_via_macro_diagnostics.completion_valid)) {
            g_era_via_macro_diagnostics.intervening_raw_count++;
        }
        g_era_via_macro_diagnostics_runtime.application_interval_clean = false;
        g_era_via_macro_diagnostics.completion_valid = 0;
        g_era_via_macro_diagnostics_runtime.current_request_is_macro = false;
        g_era_via_macro_diagnostics.request_open = 0;
        return;
    }

    uint16_t elapsed_ms = era_via_macro_diagnostics_elapsed16(now_ms - g_era_via_macro_diagnostics_runtime.interval_start_ms);
    if (g_era_via_macro_diagnostics.receive_count == 0) {
        g_era_via_macro_diagnostics.first_receive_elapsed_ms = elapsed_ms;
    } else {
        uint16_t previous_ms = g_era_via_macro_diagnostics.last_receive_elapsed_ms;
        uint16_t gap_ms      = elapsed_ms >= previous_ms ? (uint16_t)(elapsed_ms - previous_ms) : UINT16_MAX;
        g_era_via_macro_diagnostics.receive_gap_last_ms = gap_ms;
        if (gap_ms > g_era_via_macro_diagnostics.receive_gap_max_ms) {
            g_era_via_macro_diagnostics.receive_gap_max_ms = gap_ms;
        }
        if (gap_ms >= ERA_HOST_PEER_STORAGE_DIRTY_QUIET_MS &&
            g_era_via_macro_diagnostics.receive_gap_over_quiet_count < UINT16_MAX) {
            g_era_via_macro_diagnostics.receive_gap_over_quiet_count++;
        }
    }

    if (g_era_via_macro_diagnostics.completion_valid) {
        era_via_macro_diagnostics_add_duration(now_ms - g_era_via_macro_diagnostics_runtime.completion_ms,
                                               &g_era_via_macro_diagnostics.application_total_ms,
                                               &g_era_via_macro_diagnostics.application_max_ms);
        g_era_via_macro_diagnostics.completion_valid = 0;
    }

    if (g_era_via_macro_diagnostics.response_pending) {
        g_era_via_macro_diagnostics.response_overlap_count++;
    }

    g_era_via_macro_diagnostics.receive_count++;
    g_era_via_macro_diagnostics.last_receive_elapsed_ms = elapsed_ms;
    g_era_via_macro_diagnostics.request_open = 1;
    g_era_via_macro_diagnostics_runtime.request_start_ms = now_ms;
    g_era_via_macro_diagnostics_runtime.current_request_is_macro = true;
}

void era_via_macro_diagnostics_response_begin(void) {
    if (!g_era_via_macro_diagnostics_runtime.current_request_is_macro) {
        return;
    }

    era_via_macro_diagnostics_task();
    uint32_t now_ms = timer_read32();
    if (g_era_via_macro_diagnostics.request_open) {
        era_via_macro_diagnostics_add_duration(now_ms - g_era_via_macro_diagnostics_runtime.request_start_ms,
                                               &g_era_via_macro_diagnostics.handler_total_ms,
                                               &g_era_via_macro_diagnostics.handler_max_ms);
    }
    g_era_via_macro_diagnostics_runtime.send_start_ms = now_ms;
}

void era_via_macro_diagnostics_response_end(void) {
    if (!g_era_via_macro_diagnostics_runtime.current_request_is_macro) {
        return;
    }

    uint32_t now_ms = timer_read32();
    era_via_macro_diagnostics_add_duration(now_ms - g_era_via_macro_diagnostics_runtime.send_start_ms,
                                           &g_era_via_macro_diagnostics.send_total_ms,
                                           &g_era_via_macro_diagnostics.send_max_ms);
    g_era_via_macro_diagnostics.response_count++;
    if (!g_era_via_macro_diagnostics.response_pending) {
        g_era_via_macro_diagnostics_runtime.response_pending_ms = now_ms;
    }
    g_era_via_macro_diagnostics_runtime.application_interval_clean = true;
    g_era_via_macro_diagnostics.response_pending = 1;
    g_era_via_macro_diagnostics.request_open = 0;
    g_era_via_macro_diagnostics_runtime.current_request_is_macro = false;
}

void era_via_macro_diagnostics_get_snapshot(era_via_macro_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }
    era_via_macro_diagnostics_task();
    *snapshot = g_era_via_macro_diagnostics;
}

void era_via_macro_diagnostics_reset(void) {
    memset(&g_era_via_macro_diagnostics, 0, sizeof(g_era_via_macro_diagnostics));
    memset(&g_era_via_macro_diagnostics_runtime, 0, sizeof(g_era_via_macro_diagnostics_runtime));
    g_era_via_macro_diagnostics_runtime.interval_start_ms = timer_read32();
}
