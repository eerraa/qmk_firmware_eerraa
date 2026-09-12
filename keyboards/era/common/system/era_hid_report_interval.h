// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The host-visible width of a synthesized tap, kept as a minimum interval
 * between keyboard reports on the USB transport instead of a wait in the scan
 * loop. Rules: era_hid_report_contract.md, **Tap width and the report
 * interval**. The width every requester asks for is decided upstream of this
 * unit (quantum/action.c tap_code_wait(), features/era_tapdance.c); this unit
 * only makes the requested interval true at the host without stalling
 * anything.
 *
 * **What the interval is.** At least `ms` between the moment the latest
 * keyboard-class report reached the host and the moment the next one is
 * handed to the endpoint. QMK's logical key state is never delayed: the
 * release is registered at once, and the report that carries it waits here,
 * in order, with every keyboard report that follows it. Mouse and EXTRA
 * reports are never held; on the shared endpoint they are counted so
 * completion order stays exact.
 *
 * **Where the clock comes from.** This unit is clock- and platform-agnostic.
 * `now` is the platform's own monotonic counter in any unit, given at init as
 * ticks per millisecond; comparisons are wrap-safe over half its range. The
 * ChibiOS platform half (era_hid_report_interval_chibios.c) passes the system
 * tick, answers the two platform functions at the bottom and owns the ISR
 * completion hook. A host test passes milliseconds and a recording sink.
 *
 * **Anchoring.** A request attaches to the keyboard report the tap just
 * posted: if that report is still held here, to its backlog entry; if it is
 * in flight, to its completion (posted/completed counters per lane, the
 * completion stamped from the ISR); if it already completed, the time since
 * that completion counts toward the interval. Repeated requests take the
 * larger interval.
 *
 * **What cannot wedge input.** A completion that never arrives (a host that
 * stopped polling) abandons the anchor after ERA_HID_REPORT_INTERVAL_ANCHOR_LIMIT_MS
 * and the backlog drains. A transport reset drops backlog and intervals with
 * the queues they were queued behind; suspend keeps the backlog so a tap
 * queued while asleep is delivered on resume with its width. A full backlog
 * folds the newest report into its tail, so the host converges to the latest
 * state and the tail's width survives. Every hold, post and task step is
 * O(1) per report; the hot-path question is era_hid_report_interval_active().
 */

enum {
    ERA_HID_REPORT_LANE_KEYBOARD = 0, /* the keyboard interface's own endpoint: 6KRO and boot-protocol reports */
    ERA_HID_REPORT_LANE_SHARED   = 1, /* the shared endpoint: NKRO, beside mouse/EXTRA posts that are counted but never held */
    ERA_HID_REPORT_LANE_COUNT    = 2,
};

/* The widest keyboard-class report this unit copies: SHARED_EPSIZE, the NKRO report. */
#ifndef ERA_HID_REPORT_INTERVAL_REPORT_BYTES
#    define ERA_HID_REPORT_INTERVAL_REPORT_BYTES 32
#endif
/* Reports held behind one interval before the newest folds into the tail.
 * Sixteen is more keyboard events than 80 ms of typing produces; a macro burst
 * beyond it converges to its last state. */
#ifndef ERA_HID_REPORT_INTERVAL_BACKLOG
#    define ERA_HID_REPORT_INTERVAL_BACKLOG 16
#endif
/* A requested width is capped so a misconfigured delay cannot park input. */
#ifndef ERA_HID_REPORT_INTERVAL_REQUEST_MAX_MS
#    define ERA_HID_REPORT_INTERVAL_REQUEST_MAX_MS 5000
#endif
/* How long an in-flight anchor may wait for its completion before the unit
 * gives the width up rather than the keyboard. */
#ifndef ERA_HID_REPORT_INTERVAL_ANCHOR_LIMIT_MS
#    define ERA_HID_REPORT_INTERVAL_ANCHOR_LIMIT_MS 1000
#endif

typedef struct {
    uint32_t coalesced;   /* reports folded into a full backlog's tail */
    uint32_t valve_trips; /* anchors abandoned because no completion arrived within the limit */
    uint8_t  backlog;     /* reports held right now */
    bool     gate_closed; /* keyboard-class reports are being held */
} era_hid_report_interval_diagnostics_t;

void era_hid_report_interval_init(uint32_t ticks_per_ms);

/* A keyboard-class report is about to be posted to `lane`. True means the unit
 * took a copy and the caller must not post it; false means post it now and,
 * on success, call era_hid_report_interval_note_keyboard_posted(). */
bool era_hid_report_interval_hold(uint8_t lane, const void *data, uint8_t size);
/* Every successful post to a tracked lane, keyboard-class or not. */
void era_hid_report_interval_note_posted(uint8_t lane);
/* The keyboard-class report the caller just posted directly. */
void era_hid_report_interval_note_keyboard_posted(uint8_t lane);
/* One transmitted report on `lane` reached the host. ISR context: plain stores only. */
void era_hid_report_interval_note_completed(uint8_t lane, uint32_t now);
/* A synthesized tap asks for its width. */
void era_hid_report_interval_request(uint16_t ms, uint32_t now);
/* The transport lost its queues. keep_backlog: true on suspend, false on reset
 * or unconfigure. Any context; the next task applies it. */
void era_hid_report_interval_note_session_edge(bool keep_backlog);
/* Whether task() has anything to do. This is the per-pass hot-path test. */
bool era_hid_report_interval_active(void);
void era_hid_report_interval_task(uint32_t now);
void era_hid_report_interval_get_diagnostics(era_hid_report_interval_diagnostics_t *out);

/* Platform: post one report to `lane` without blocking. False means no room on
 * the endpoint or a transport that is not active; the unit keeps the report
 * and retries on a later task. */
bool era_hid_report_interval_platform_post(uint8_t lane, const uint8_t *data, uint8_t size);

#if defined(ERA_HID_REPORT_INTERVAL_ENABLE)
/* The ChibiOS platform half's entry points for the ERA class skeleton. */
void era_hid_report_interval_platform_init(void);
void era_hid_report_interval_service(void);
#endif
