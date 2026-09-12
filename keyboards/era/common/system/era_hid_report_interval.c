// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_hid_report_interval.h"

#include <string.h>

typedef struct {
    uint8_t  lane;
    uint8_t  size;
    uint8_t  data[ERA_HID_REPORT_INTERVAL_REPORT_BYTES];
    uint32_t delay_after; /* ticks the host must wait after this report completes; 0 none */
} era_hid_report_entry_t;

enum {
    ERA_HID_REPORT_EDGE_NONE    = 0,
    ERA_HID_REPORT_EDGE_SUSPEND = 1,
    ERA_HID_REPORT_EDGE_RESET   = 2, /* outranks suspend when both arrive before one task */
};

static struct {
    uint32_t ticks_per_ms;

    /* ISR-owned: written by note_completed() only, read by the task. The
       stamp is written before the count so a reader that sees the count sees
       the stamp that belongs to it. */
    volatile uint32_t completed[ERA_HID_REPORT_LANE_COUNT];
    volatile uint32_t completed_at[ERA_HID_REPORT_LANE_COUNT];
    volatile uint8_t  edge;

    /* Task-owned. */
    uint32_t posted[ERA_HID_REPORT_LANE_COUNT];
    bool     last_post_valid;
    uint8_t  last_post_lane;
    uint32_t last_post_seq;
    bool     anchor_pending;
    uint8_t  anchor_lane;
    uint32_t anchor_target;
    uint32_t anchor_ticks;
    uint32_t anchor_since;
    bool     deadline_valid;
    uint32_t deadline;

    era_hid_report_entry_t backlog[ERA_HID_REPORT_INTERVAL_BACKLOG];
    uint8_t  head;
    uint8_t  count;
    uint32_t coalesced;
    uint32_t valve_trips;
} s;

static inline bool era_hid_report_interval_reached(uint32_t now, uint32_t at) {
    return (int32_t)(now - at) >= 0;
}

static inline bool era_hid_report_interval_gate_closed(void) {
    return s.anchor_pending || s.deadline_valid;
}

static inline era_hid_report_entry_t *era_hid_report_interval_entry(uint8_t offset) {
    return &s.backlog[(uint8_t)((s.head + offset) % ERA_HID_REPORT_INTERVAL_BACKLOG)];
}

static void era_hid_report_interval_extend_deadline(uint32_t at) {
    if (!s.deadline_valid || (int32_t)(at - s.deadline) > 0) {
        s.deadline = at;
    }
    s.deadline_valid = true;
}

void era_hid_report_interval_init(uint32_t ticks_per_ms) {
    memset(&s, 0, sizeof(s));
    s.ticks_per_ms = ticks_per_ms != 0 ? ticks_per_ms : 1;
}

bool era_hid_report_interval_hold(uint8_t lane, const void *data, uint8_t size) {
    if (lane >= ERA_HID_REPORT_LANE_COUNT || data == NULL || size == 0 || size > ERA_HID_REPORT_INTERVAL_REPORT_BYTES) {
        return false;
    }
    /* Open gate, nothing queued, no edge waiting: the ordinary path, one
       branch on the report send and nothing else. */
    if (!era_hid_report_interval_gate_closed() && s.count == 0 && s.edge == ERA_HID_REPORT_EDGE_NONE) {
        return false;
    }

    era_hid_report_entry_t *entry;
    if (s.count == ERA_HID_REPORT_INTERVAL_BACKLOG) {
        /* Full: the newest state replaces the tail. The host converges to the
           latest report; the width the tail carried survives with it. */
        entry = era_hid_report_interval_entry((uint8_t)(s.count - 1));
        s.coalesced++;
    } else {
        entry              = era_hid_report_interval_entry(s.count);
        entry->delay_after = 0;
        s.count++;
    }
    entry->lane = lane;
    entry->size = size;
    memcpy(entry->data, data, size);
    return true;
}

void era_hid_report_interval_note_posted(uint8_t lane) {
    if (lane < ERA_HID_REPORT_LANE_COUNT) {
        s.posted[lane]++;
    }
}

void era_hid_report_interval_note_keyboard_posted(uint8_t lane) {
    if (lane >= ERA_HID_REPORT_LANE_COUNT) {
        return;
    }
    s.last_post_valid = true;
    s.last_post_lane  = lane;
    s.last_post_seq   = s.posted[lane];
}

void era_hid_report_interval_note_completed(uint8_t lane, uint32_t now) {
    if (lane >= ERA_HID_REPORT_LANE_COUNT) {
        return;
    }
    s.completed_at[lane] = now;
    s.completed[lane]++;
}

void era_hid_report_interval_request(uint16_t ms, uint32_t now) {
    if (ms == 0) {
        return;
    }
    if (ms > ERA_HID_REPORT_INTERVAL_REQUEST_MAX_MS) {
        ms = ERA_HID_REPORT_INTERVAL_REQUEST_MAX_MS;
    }
    uint32_t ticks = (uint32_t)ms * s.ticks_per_ms;

    if (s.count != 0) {
        /* The tap's own report is still held here: its width rides with it
           and starts when that report completes. */
        era_hid_report_entry_t *tail = era_hid_report_interval_entry((uint8_t)(s.count - 1));
        if (tail->delay_after < ticks) {
            tail->delay_after = ticks;
        }
        return;
    }
    if (!s.last_post_valid) {
        /* Nothing reached the transport, so there is nothing to make wide. */
        return;
    }

    uint8_t lane = s.last_post_lane;
    if ((int32_t)(s.completed[lane] - s.last_post_seq) >= 0) {
        /* Already at the host: the time since counts toward the interval. */
        uint32_t at = s.completed_at[lane] + ticks;
        if (!era_hid_report_interval_reached(now, at)) {
            era_hid_report_interval_extend_deadline(at);
        }
        return;
    }

    /* Still in flight: anchor on its completion. A second request for the
       same report only widens it. */
    if (s.anchor_pending && s.anchor_lane == lane && s.anchor_target == s.last_post_seq) {
        if (s.anchor_ticks < ticks) {
            s.anchor_ticks = ticks;
        }
        return;
    }
    s.anchor_pending = true;
    s.anchor_lane    = lane;
    s.anchor_target  = s.last_post_seq;
    s.anchor_ticks   = ticks;
    s.anchor_since   = now;
}

void era_hid_report_interval_note_session_edge(bool keep_backlog) {
    uint8_t edge = keep_backlog ? ERA_HID_REPORT_EDGE_SUSPEND : ERA_HID_REPORT_EDGE_RESET;
    if (edge > s.edge) {
        s.edge = edge;
    }
}

bool era_hid_report_interval_active(void) {
    return era_hid_report_interval_gate_closed() || s.count != 0 || s.edge != ERA_HID_REPORT_EDGE_NONE;
}

static void era_hid_report_interval_apply_edge(uint8_t edge, uint32_t now) {
    /* The queues behind this unit were emptied: whatever was in flight is
       gone, so the counters meet and no completion is owed for it. */
    for (uint8_t lane = 0; lane < ERA_HID_REPORT_LANE_COUNT; lane++) {
        s.posted[lane] = s.completed[lane];
    }
    s.last_post_valid = false;
    if (edge == ERA_HID_REPORT_EDGE_RESET) {
        s.head           = 0;
        s.count          = 0;
        s.anchor_pending = false;
        s.deadline_valid = false;
        return;
    }
    /* Suspend keeps the backlog. A width still waiting on a completion that
       will never come is measured from now instead. */
    if (s.anchor_pending) {
        s.anchor_pending = false;
        era_hid_report_interval_extend_deadline(now + s.anchor_ticks);
    }
}

void era_hid_report_interval_task(uint32_t now) {
    uint8_t edge = s.edge;
    if (edge != ERA_HID_REPORT_EDGE_NONE) {
        s.edge = ERA_HID_REPORT_EDGE_NONE;
        era_hid_report_interval_apply_edge(edge, now);
    }

    if (s.anchor_pending) {
        uint8_t lane = s.anchor_lane;
        if ((int32_t)(s.completed[lane] - s.anchor_target) >= 0) {
            era_hid_report_interval_extend_deadline(s.completed_at[lane] + s.anchor_ticks);
            s.anchor_pending = false;
        } else if (era_hid_report_interval_reached(now, s.anchor_since + ERA_HID_REPORT_INTERVAL_ANCHOR_LIMIT_MS * s.ticks_per_ms)) {
            /* The host stopped taking reports. The width is given up; the
               keyboard is not. */
            s.anchor_pending = false;
            s.valve_trips++;
        }
    }
    if (s.deadline_valid && era_hid_report_interval_reached(now, s.deadline)) {
        s.deadline_valid = false;
    }

    while (!era_hid_report_interval_gate_closed() && s.count != 0) {
        era_hid_report_entry_t *entry = era_hid_report_interval_entry(0);
        if (!era_hid_report_interval_platform_post(entry->lane, entry->data, entry->size)) {
            /* No room, or a transport that is not active: order is kept and
               the next task retries. */
            break;
        }
        uint8_t  lane  = entry->lane;
        uint32_t delay = entry->delay_after;
        s.head = (uint8_t)((s.head + 1) % ERA_HID_REPORT_INTERVAL_BACKLOG);
        s.count--;
        /* The platform counted the post; this is now the latest keyboard
           report, and a width it carried starts when it completes. */
        s.last_post_valid = true;
        s.last_post_lane  = lane;
        s.last_post_seq   = s.posted[lane];
        if (delay != 0) {
            s.anchor_pending = true;
            s.anchor_lane    = lane;
            s.anchor_target  = s.posted[lane];
            s.anchor_ticks   = delay;
            s.anchor_since   = now;
        }
    }
}

void era_hid_report_interval_get_diagnostics(era_hid_report_interval_diagnostics_t *out) {
    if (out == NULL) {
        return;
    }
    out->coalesced   = s.coalesced;
    out->valve_trips = s.valve_trips;
    out->backlog     = s.count;
    out->gate_closed = era_hid_report_interval_gate_closed();
}
