// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_tap_activity.h"

#include <string.h>

#include "quantum_keycodes.h"
#include "sync_timer.h"
#include "timer.h"

#include "../features/era_tapping.h"
#include "era_split_transport_scheduler.h"
#include "era_split_wire_payload.h"

// The retro state lives in quantum/action.c as external-linkage globals; a
// peer press cancels the pending retro tap the same way a local press does --
// by making the release fail to re-prime. Declared rather than included (the
// era_split_peer_layer precedent, in the other direction), and guarded on the
// selectors that compile them, which every ERA board sets.
#if defined(RETRO_TAPPING) || defined(RETRO_TAPPING_PER_KEY)
extern bool     retro_tap_primed;
extern uint16_t retro_tap_curr_key;
#endif

#define ERA_SPLIT_TAP_ACTIVITY_OPTION_HOKP 0x01
#define ERA_SPLIT_TAP_ACTIVITY_OPTION_PH 0x02
#define ERA_SPLIT_TAP_ACTIVITY_OPTION_RETRO 0x04

typedef enum {
    ERA_SPLIT_TAP_ACTIVITY_PHASE_ENGINE = 0,
    ERA_SPLIT_TAP_ACTIVITY_PHASE_RETRO_PENDING,
} era_split_tap_activity_phase_t;

static struct {
    // Local input truth, counted always -- cheap, and what makes "didn't
    // send" readable on a console whose wire never moved.
    uint8_t  press_count;
    uint8_t  release_count;
    uint32_t last_press_ms;
    uint32_t last_release_ms;

    // The judgment window. `open_ms` and the peer-counter snapshot below are
    // what the judgment orders against; `options` is the armed set captured at
    // open, so a mid-window VIA edit changes the next window rather than this
    // one.
    bool                           window_open;
    era_split_tap_activity_phase_t phase;
    keypos_t                       window_key;
    uint32_t                       window_open_ms;
    uint8_t                        window_options;
    bool                           retro_cancelled;
    uint8_t                        snapshot_press_count;
    uint8_t                        snapshot_release_count;

    // The advertised composition (the value the wire carries) and the peer's.
    era_split_wire_activity_section_t advertised;
    era_split_wire_activity_section_t peer;
    bool                              peer_valid;

    era_split_tap_activity_diagnostics_t counters;
} g_era_split_tap_activity;

// The event's own instant on the shared clock. Reconstructed from the 16-bit
// event time rather than stamped at processing, so an event the tapping queue
// held for part of a term still carries the instant it happened.
static uint32_t era_split_tap_activity_sync_ms_of_event(uint16_t event_time) {
    uint16_t held_ms = (uint16_t)(timer_read() - event_time);
    return sync_timer_read32() - held_ms;
}

/* The advertised value's one composition rule: the window flag is always
   live; the activity fields go live only while the peer's window flag is up
   and stay frozen at the last advertised values otherwise. Recomputed at
   every mutation and compared, so the producer fires exactly when the wire
   value changed -- ordinary typing with both windows down recomposes to the
   same record and moves nothing. */
static void era_split_tap_activity_publish_if_changed(void) {
    era_split_wire_activity_section_t next = g_era_split_tap_activity.advertised;
    next.window_open                       = g_era_split_tap_activity.window_open;
    if (g_era_split_tap_activity.peer_valid && g_era_split_tap_activity.peer.window_open) {
        next.press_count     = g_era_split_tap_activity.press_count;
        next.release_count   = g_era_split_tap_activity.release_count;
        next.last_press_ms   = g_era_split_tap_activity.last_press_ms;
        next.last_release_ms = g_era_split_tap_activity.last_release_ms;
    }
    if (!era_split_wire_activity_equal(&next, &g_era_split_tap_activity.advertised)) {
        g_era_split_tap_activity.advertised = next;
        g_era_split_tap_activity.counters.advertise_change_count++;
        era_split_transport_scheduler_note_local_activity_change();
    }
}

static void era_split_tap_activity_close_window(void) {
    g_era_split_tap_activity.window_open = false;
    g_era_split_tap_activity.phase       = ERA_SPLIT_TAP_ACTIVITY_PHASE_ENGINE;
}

static void era_split_tap_activity_cancel_retro(void) {
#if defined(RETRO_TAPPING) || defined(RETRO_TAPPING_PER_KEY)
    // The same state a local press leaves behind: not primed, and a current
    // key no release will match, so the retro tail in process_action fires
    // nothing.
    retro_tap_primed   = false;
    retro_tap_curr_key = 0;
#endif
    g_era_split_tap_activity.retro_cancelled = true;
    g_era_split_tap_activity.counters.retro_cancel_count++;
}

// True when the peer's cached press activity postdates the window open --
// counter advance says a new press event arrived, the stamp says it fell
// inside the window. Both are required: frozen fields un-freeze with a stale
// image when a window opens, so a counter delta alone would count presses
// from before the window ever existed.
static bool era_split_tap_activity_peer_press_in_window(void) {
    return g_era_split_tap_activity.peer_valid &&
           g_era_split_tap_activity.peer.press_count != g_era_split_tap_activity.snapshot_press_count &&
           (int32_t)(g_era_split_tap_activity.peer.last_press_ms - g_era_split_tap_activity.window_open_ms) >= 0;
}

// The release edge, same shape: counter advance plus a stamp inside the
// window. The stamp is what classifies a release that shares its image with
// the press -- on this transport a strike's two edges predominantly cross in
// one image, which is why no arrival-order rule can stand in for it.
static bool era_split_tap_activity_peer_release_in_window(void) {
    return g_era_split_tap_activity.peer_valid &&
           g_era_split_tap_activity.peer.release_count != g_era_split_tap_activity.snapshot_release_count &&
           (int32_t)(g_era_split_tap_activity.peer.last_release_ms - g_era_split_tap_activity.window_open_ms) >= 0;
}

void era_split_tap_activity_note_speculative(uint8_t event) {
    switch (event) {
        case 0:
            g_era_split_tap_activity.counters.speculative_activate_count++;
            break;
        case 1:
            g_era_split_tap_activity.counters.speculative_revert_count++;
            break;
        case 2:
            g_era_split_tap_activity.counters.speculative_abort_count++;
            break;
        default:
            break;
    }
}

void era_split_tap_activity_note_record(bool pressed, keypos_t key, uint16_t event_time, uint16_t keycode, uint8_t tap_count) {
    if (pressed) {
        g_era_split_tap_activity.press_count++;
        g_era_split_tap_activity.last_press_ms = era_split_tap_activity_sync_ms_of_event(event_time);
    } else {
        g_era_split_tap_activity.release_count++;
        g_era_split_tap_activity.last_release_ms = era_split_tap_activity_sync_ms_of_event(event_time);
    }

    // Settle notes against the open window's own key. A record reaches here
    // only when the engine settled it, so the tap count is decided: a hold
    // press either enters the retro-pending phase or closes the window, a tap
    // press closes it, and the retro-pending key's release is the retro
    // decision's own end.
    if (g_era_split_tap_activity.window_open &&
        KEYEQ(key, g_era_split_tap_activity.window_key) &&
        (IS_QK_LAYER_TAP(keycode) || IS_QK_MOD_TAP(keycode))) {
        if (pressed && tap_count == 0) {
            if (g_era_split_tap_activity.phase == ERA_SPLIT_TAP_ACTIVITY_PHASE_ENGINE) {
                if ((g_era_split_tap_activity.window_options & ERA_SPLIT_TAP_ACTIVITY_OPTION_RETRO) != 0 &&
                    !g_era_split_tap_activity.retro_cancelled) {
                    g_era_split_tap_activity.phase = ERA_SPLIT_TAP_ACTIVITY_PHASE_RETRO_PENDING;
                } else {
                    era_split_tap_activity_close_window();
                }
            }
        } else if (pressed) {
            era_split_tap_activity_close_window();
        } else if (g_era_split_tap_activity.phase == ERA_SPLIT_TAP_ACTIVITY_PHASE_RETRO_PENDING) {
            era_split_tap_activity_close_window();
        }
    }

    era_split_tap_activity_publish_if_changed();
}

void era_split_tap_activity_engine_window(bool in_flight, keypos_t key, uint16_t event_time) {
    if (in_flight) {
        if (g_era_split_tap_activity.window_open && KEYEQ(key, g_era_split_tap_activity.window_key) &&
            g_era_split_tap_activity.phase == ERA_SPLIT_TAP_ACTIVITY_PHASE_ENGINE) {
            return; // The ordinary in-flight tick: nothing moved.
        }
        // A new tapping key. A still-open previous window yields, including a
        // retro-pending one -- the local press that started this key already
        // cleared the retro priming, so its window has nothing left to guard.
        if (g_era_split_tap_activity.window_open) {
            era_split_tap_activity_close_window();
        }

        uint8_t options = 0;
        if (era_tapping_get_hold_on_other_key_press()) {
            options |= ERA_SPLIT_TAP_ACTIVITY_OPTION_HOKP;
        }
        if (era_tapping_get_permissive_hold()) {
            options |= ERA_SPLIT_TAP_ACTIVITY_OPTION_PH;
        }
        if (era_tapping_get_retro_tapping()) {
            options |= ERA_SPLIT_TAP_ACTIVITY_OPTION_RETRO;
        }
        if (options != 0) {
            g_era_split_tap_activity.window_open            = true;
            g_era_split_tap_activity.phase                  = ERA_SPLIT_TAP_ACTIVITY_PHASE_ENGINE;
            g_era_split_tap_activity.window_key             = key;
            g_era_split_tap_activity.window_open_ms         = era_split_tap_activity_sync_ms_of_event(event_time);
            g_era_split_tap_activity.window_options         = options;
            g_era_split_tap_activity.retro_cancelled        = false;
            g_era_split_tap_activity.snapshot_press_count   = g_era_split_tap_activity.peer.press_count;
            g_era_split_tap_activity.snapshot_release_count = g_era_split_tap_activity.peer.release_count;
            g_era_split_tap_activity.counters.window_open_count++;
        }
    } else if (g_era_split_tap_activity.window_open &&
               g_era_split_tap_activity.phase == ERA_SPLIT_TAP_ACTIVITY_PHASE_ENGINE) {
        // The engine reset without a settle reaching the record hook -- the
        // overflow clear, or a record a feature consumed. The retro-pending
        // phase deliberately survives this arm: the engine is idle for its
        // whole span.
        era_split_tap_activity_close_window();
    } else {
        return; // Idle, window closed: the per-tick fast path.
    }

    era_split_tap_activity_publish_if_changed();
}

bool era_split_tap_activity_judge_hold(bool own_release, uint16_t own_release_event_time) {
    if (!g_era_split_tap_activity.window_open ||
        g_era_split_tap_activity.phase != ERA_SPLIT_TAP_ACTIVITY_PHASE_ENGINE ||
        !era_split_tap_activity_peer_press_in_window()) {
        return false;
    }
    // When the current event is the tapping key's own release, a peer event
    // wins only if it did not postdate the release instant -- the shared
    // clock's retroactively-exact ordering. A peer press that arrived in the
    // cache but happened after the release belongs to the tap; a peer
    // release that happened after it had not completed the pair in time.
    uint32_t own_release_ms = 0;
    if (own_release) {
        own_release_ms = era_split_tap_activity_sync_ms_of_event(own_release_event_time);
        if ((int32_t)(g_era_split_tap_activity.peer.last_press_ms - own_release_ms) > 0) {
            return false;
        }
    }

    bool settle = false;
    if ((g_era_split_tap_activity.window_options & ERA_SPLIT_TAP_ACTIVITY_OPTION_HOKP) != 0) {
        g_era_split_tap_activity.counters.judged_hold_hokp_count++;
        settle = true;
    } else if ((g_era_split_tap_activity.window_options & ERA_SPLIT_TAP_ACTIVITY_OPTION_PH) != 0 &&
               era_split_tap_activity_peer_release_in_window() &&
               !(own_release &&
                 (int32_t)(g_era_split_tap_activity.peer.last_release_ms - own_release_ms) > 0)) {
        // The pair is two stamped facts: a press inside the window (the gate
        // above) and a release inside it, each ordered by its own instant on
        // the shared clock. Two accepted approximations, same class: the
        // counters carry no key identity, so this cannot prove the release
        // pairs the press, where single-keyboard permissive hold can -- and
        // latest-state carries one stamp per
        // edge, so a multi-event image is judged by its newest instants.
        g_era_split_tap_activity.counters.judged_hold_ph_count++;
        settle = true;
    }
    // The in-window peer press cancels a pending retro tap whether or not it
    // also settles the hold -- the recorded side effect. A judged settle that
    // left retro primed would re-prime at the key's own release (the current
    // key still matches, because a peer press never enters the local
    // action_exec) and emit the tap the settle just refused.
    if ((g_era_split_tap_activity.window_options & ERA_SPLIT_TAP_ACTIVITY_OPTION_RETRO) != 0 &&
        !g_era_split_tap_activity.retro_cancelled) {
        era_split_tap_activity_cancel_retro();
    }
    return settle;
}

void era_split_tap_activity_apply_peer(const era_split_wire_activity_section_t *activity) {
    if (activity == NULL) {
        return;
    }
    if (g_era_split_tap_activity.peer_valid &&
        era_split_wire_activity_equal(&g_era_split_tap_activity.peer, activity)) {
        return; // Idempotent re-delivery on an unrelated wake.
    }
    g_era_split_tap_activity.peer       = *activity;
    g_era_split_tap_activity.peer_valid = true;
    g_era_split_tap_activity.counters.peer_update_count++;

    // A pre-window stamp proves every event this image counts on that edge is
    // freeze-gap backlog -- the stamp is the newest of them -- so re-anchor
    // that edge's baseline over the image. Keyed by the stamp, never by
    // arrival order: an image whose stamp is in-window never re-anchors, so a
    // backlog jump cannot park the uint8 counter exactly on the window-open
    // snapshot and void the in-window delta (the 1-in-256 wrap).
    if (g_era_split_tap_activity.window_open) {
        if ((int32_t)(g_era_split_tap_activity.peer.last_press_ms - g_era_split_tap_activity.window_open_ms) < 0) {
            g_era_split_tap_activity.snapshot_press_count = g_era_split_tap_activity.peer.press_count;
        }
        if ((int32_t)(g_era_split_tap_activity.peer.last_release_ms - g_era_split_tap_activity.window_open_ms) < 0) {
            g_era_split_tap_activity.snapshot_release_count = g_era_split_tap_activity.peer.release_count;
        }
    }

    // The retro-pending span has no engine presence, so the peer press that
    // cancels its tap is consumed here rather than by the in-flight judge.
    if (g_era_split_tap_activity.window_open &&
        g_era_split_tap_activity.phase == ERA_SPLIT_TAP_ACTIVITY_PHASE_RETRO_PENDING &&
        (g_era_split_tap_activity.window_options & ERA_SPLIT_TAP_ACTIVITY_OPTION_RETRO) != 0 &&
        !g_era_split_tap_activity.retro_cancelled &&
        era_split_tap_activity_peer_press_in_window()) {
        era_split_tap_activity_cancel_retro();
    }

    era_split_tap_activity_publish_if_changed();
}

void era_split_tap_activity_wire_value(era_split_wire_activity_section_t *out) {
    if (out != NULL) {
        *out = g_era_split_tap_activity.advertised;
    }
}

void era_split_tap_activity_relation_reset(void) {
    g_era_split_tap_activity.peer_valid = false;
    memset(&g_era_split_tap_activity.peer, 0, sizeof(g_era_split_tap_activity.peer));
    // Back to the all-zero baseline the reopened peer's cleared cache assumes.
    // A window that is open across the rotation recomposes to a nonzero value
    // below and crosses after the reopen; fresh defaults recompose to zero and
    // stay silent.
    memset(&g_era_split_tap_activity.advertised, 0, sizeof(g_era_split_tap_activity.advertised));
    era_split_tap_activity_publish_if_changed();
}

void era_split_tap_activity_note_sent(void) {
    g_era_split_tap_activity.counters.sent_count++;
}

void era_split_tap_activity_get_diagnostics(era_split_tap_activity_diagnostics_t *out) {
    if (out != NULL) {
        *out = g_era_split_tap_activity.counters;
    }
}
