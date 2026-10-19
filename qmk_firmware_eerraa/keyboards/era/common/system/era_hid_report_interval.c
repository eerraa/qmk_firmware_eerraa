// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_hid_report_interval.h"

#include <string.h>

typedef struct {
    uint8_t  lane;
    uint8_t  size;
    uint8_t  data[ERA_HID_REPORT_INTERVAL_REPORT_BYTES];
    uint32_t delay_after; /* ticks the host must wait after this report completes; 0 none */
    uint32_t ticket;
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
    volatile uint32_t reset_events, suspend_events;
    uint32_t reset_seen, suspend_seen;

    /* Task-owned. */
    uint32_t posted[ERA_HID_REPORT_LANE_COUNT];
    bool     last_post_valid;
    uint8_t  last_post_lane;
    uint32_t last_post_seq;
    uint32_t next_ticket, producer_ticket, producer_post_ticket;
    uint32_t pressure_serial;
    bool     anchor_pending;
    uint8_t  anchor_lane;
    uint32_t anchor_target;
    uint32_t anchor_ticks;
    uint32_t anchor_since;
    bool     deadline_valid;
    uint32_t deadline;

    era_hid_report_entry_t backlog[ERA_HID_REPORT_INTERVAL_BACKLOG];
    era_hid_report_entry_t suffix;
    bool suffix_valid;
    uint8_t  head;
    uint8_t  count;
    uint32_t coalesced;
    uint32_t valve_trips;
} s;

static void era_hid_report_interval_apply_edge(uint8_t edge, uint32_t now);

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
    /* ISR publishes counters; consuming a sampled event cannot erase an
     * intervening higher-priority reset. Suspend is consumed only on resume. */
    uint32_t resets = s.reset_events;
    if (resets != s.reset_seen) {
        uint32_t suspends = s.suspend_events;
        era_hid_report_interval_apply_edge(ERA_HID_REPORT_EDGE_RESET, 0);
        s.reset_seen = resets; s.suspend_seen = suspends;
    }
#ifdef ERA_MACRO_ENABLE
    /* Check AFTER reset consumption: otherwise a report built by the old
     * macro could become the first retained prefix of the new USB session.
     * A later ISR leaves a pending edge that retires this entry normally. */
    bool era_macro_report_source_current(void);
    if (!era_macro_report_source_current()) return true;
#endif
    uint32_t ticket = ++s.next_ticket;
    s.producer_ticket = ticket;
    /* This owner takes every valid void output. A normal BUSY keeps it here;
     * action_util's last_report has already advanced and cannot retry it. */
    if (!era_hid_report_interval_gate_closed() && s.count == 0 && !s.suffix_valid &&
        era_hid_report_interval_platform_post(lane, data, size)) {
        era_hid_report_interval_note_keyboard_posted(lane);
        s.producer_post_ticket = ticket;
        return true;
    }

    era_hid_report_entry_t *entry;
    if (s.suffix_valid || s.count == ERA_HID_REPORT_INTERVAL_BACKLOG) {
        /* Prefix is immutable. Even after it partially drains, new reports
         * replace only the suffix until its materialization boundary. */
        entry = &s.suffix;
        if (!s.suffix_valid) ++s.pressure_serial;
        s.suffix_valid = true;
        entry->delay_after = 0;
        s.coalesced++;
    } else {
        entry              = era_hid_report_interval_entry(s.count);
        entry->delay_after = 0;
        s.count++;
    }
    entry->lane = lane;
    entry->size = size;
    entry->ticket = ticket;
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

    if (s.suffix_valid && s.suffix.ticket == s.producer_ticket) {
        if (s.suffix.delay_after < ticks) s.suffix.delay_after = ticks;
        return;
    }
    if (s.count != 0 && era_hid_report_interval_entry(s.count - 1)->ticket == s.producer_ticket) {
        /* The tap's own report is still held here: its width rides with it
           and starts when that report completes. */
        era_hid_report_entry_t *tail = era_hid_report_interval_entry((uint8_t)(s.count - 1));
        if (tail->delay_after < ticks) {
            tail->delay_after = ticks;
        }
        return;
    }
    if (!s.last_post_valid || s.producer_post_ticket != s.producer_ticket) {
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
    if (keep_backlog) ++s.suspend_events;
    else ++s.reset_events;
}

bool era_hid_report_interval_active(void) {
    return era_hid_report_interval_gate_closed() || s.count != 0 || s.suffix_valid || s.reset_events != s.reset_seen || s.suspend_events != s.suspend_seen;
}
bool era_hid_report_interval_session_current(void) {
    return s.reset_events == s.reset_seen && s.suspend_events == s.suspend_seen;
}
bool era_hid_report_interval_pressured(void) { return s.suffix_valid; }
uint32_t era_hid_report_interval_pressure_serial(void) { return s.pressure_serial; }
void era_hid_report_interval_format_edge(void) {
    s.head = s.count = 0; s.suffix_valid = false;
    s.anchor_pending = s.deadline_valid = s.last_post_valid = false;
    /* Completion counters include reports already admitted by the endpoint;
     * format change cannot retract them and must not reset those counters. */
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
        s.suffix_valid   = false;
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
    uint32_t resets = s.reset_events, suspends = s.suspend_events;
    if (resets != s.reset_seen || suspends != s.suspend_seen) {
        era_hid_report_interval_apply_edge(resets != s.reset_seen ? ERA_HID_REPORT_EDGE_RESET : ERA_HID_REPORT_EDGE_SUSPEND, now);
        s.reset_seen = resets; s.suspend_seen = suspends;
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

    if (!era_hid_report_interval_gate_closed() && !s.count && s.suffix_valid) {
        *era_hid_report_interval_entry(0) = s.suffix;
        s.count = 1; s.suffix_valid = false;
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
        if (entry->ticket == s.producer_ticket) s.producer_post_ticket = entry->ticket;
        if (delay != 0) {
            s.anchor_pending = true;
            s.anchor_lane    = lane;
            s.anchor_target  = s.posted[lane];
            s.anchor_ticks   = delay;
            s.anchor_since   = now;
        }
        if (!era_hid_report_interval_gate_closed() && !s.count && s.suffix_valid) {
            *era_hid_report_interval_entry(0) = s.suffix;
            s.count = 1; s.suffix_valid = false;
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
