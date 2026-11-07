// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_host_peer_transaction.h"

#include "atomic_util.h"
/* No era_split_scheduler_session.h since D3. The AUTHORITY section applies into
   the peer-session cache, the same destination SESSION_STATUS writes -- but the
   only apply site in this file was the retired lane-result drain, and the
   standing drain reaches that cache from the scheduler. */
#include "era_split_keyboard.h" /* the render gate's owner, for the sleep publish */
#include "era_split_wire_payload.h"
#include "era_split_tap_activity.h"

#ifdef SPLIT_KEYBOARD
/* For the anchor's held-time correction. Both cores read this counter and only
   differences of it are used, so its origin never has to be reconciled with
   core0's ChibiOS millisecond timer. */
#    include "hardware/structs/timer.h"

#    include "keyboard.h"
#    include "split_common/split_util.h"
#    include "sync_timer.h"
#endif
#if defined(RGB_MATRIX_ENABLE)
#    include "rgb_matrix.h"
#endif

#ifdef SPLIT_KEYBOARD
extern void set_split_host_keyboard_leds(uint8_t led_state);
#endif

static bool    g_era_host_peer_transaction_peer_visual_baseline_valid;
static uint8_t g_era_host_peer_transaction_peer_visual_baseline[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES];

#if defined(RGB_MATRIX_ENABLE)
static bool era_host_peer_transaction_visual_baseline_bit(const uint8_t baseline[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES], uint8_t bit) {
    return (baseline[bit >> 3] & (uint8_t)(1U << (bit & 0x07))) != 0;
}

/* The two reasons a sender can actually produce that mean "replay the pressed
   bits, do not trust the diff": a relation open, and a gap in the sender's
   tick. RENDER_RESET stays out because it arrives with a fresh baseline the
   diff already carries.

   TX_OVERFLOW (0x01) and RELATION_REOPEN (0x03) were tested here too and no
   encoder in either direction has ever assigned them -- the response planner
   picks RELATION_OPEN/RENDER_RESET/TICK_GAP and the push encoder picks
   RENDER_RESET/RELATION_OPEN -- so the two comparisons were runtime tests of a
   value the wire cannot carry. The layout validator still admits any reason up
   to REASON_MAX; narrowing that is the wire layer's own decision, and until it
   is taken such a frame applies as a plain diff rather than a replay. */
static bool era_host_peer_transaction_visual_reason_replays_pressed(uint8_t reason) {
    return reason == ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_RELATION_OPEN ||
           reason == ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_TICK_GAP;
}
#endif

bool era_host_peer_transaction_encode_rgb_state_body(const era_host_peer_rgb_state_t *state, uint8_t payload[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES]) {
    if (state == NULL || payload == NULL) {
        return false;
    }

    payload[0] = (state->enabled ? ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_FLAG_ENABLE : 0) |
                 (state->sleep ? ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_FLAG_SLEEP : 0);
    payload[1] = state->mode & 0x3F;
    payload[2] = state->hue;
    payload[3] = state->sat;
    payload[4] = state->val;
    payload[5] = state->speed;
    payload[6] = state->flags;
    return true;
}

/* Exported since Slice 12: the push direction's accept path decodes the same
   body, and one decoder for one body is what keeps the two directions from
   disagreeing about a field. */
bool era_host_peer_transaction_decode_rgb_state_body(const uint8_t *payload, era_host_peer_rgb_state_t *state) {
    if (payload == NULL ||
        (payload[0] & (uint8_t)~ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_FLAG_MASK) != 0 ||
        (payload[1] & 0xC0) != 0) {
        return false;
    }

    if (state != NULL) {
        state->enabled = (payload[0] & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_FLAG_ENABLE) != 0;
        state->sleep   = (payload[0] & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_FLAG_SLEEP) != 0;
        state->mode    = payload[1] & 0x3F;
        state->hue     = payload[2];
        state->sat     = payload[3];
        state->val     = payload[4];
        state->speed   = payload[5];
        state->flags   = payload[6];
    }
    return true;
}

/* One walk fills every section, replacing five extractors that each re-derived
 * their own offset from the mask. Beyond removing four redundant walks from
 * the core1 decode path, it removes the five places a body-size change had to
 * be repeated -- which is exactly what relocating the lock value out of byte2
 * would otherwise have had to get right five times.
 *
 * The layout is already validated: era_split_wire_layout_host_source_rsp()
 * returns false unless every present body is well-formed, so nothing here
 * re-checks a reason, a reserved bit or a mask value. */
bool era_host_peer_transaction_extract_sections(const era_split_wire_frame_t *response, era_host_peer_transaction_result_t *result) {
    if (result == NULL) {
        return false;
    }
    result->host_source_lock_state_valid         = false;
    result->host_source_lock_state               = 0;
    result->host_source_visual_snapshot_valid    = false;
    result->host_source_rgb_state_valid          = false;
    result->host_source_storage_news_valid = false;
    result->host_source_storage_news       = 0;
    result->host_source_time_anchor_valid        = false;
    result->host_source_time_anchor_ms           = 0;
    result->host_source_input_layer_valid        = false;
    result->host_source_input_layer              = 0;
    result->host_source_authority_valid          = false;
    result->host_source_activity_valid           = false;

    if (response == NULL || response->kind != ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER_HOST_SOURCE_RSP) {
        return false;
    }

    era_split_wire_section_layout_t layout;
    if (!era_split_wire_layout_host_source_rsp(response->payload, response->payload_len, &layout)) {
        return false;
    }
    const uint8_t *payload = response->payload;

    if (era_split_wire_section_present(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE)) {
        result->host_source_lock_state       = payload[era_split_wire_section_offset(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE)] &
                                         ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_VALUE_MASK;
        result->host_source_lock_state_valid = true;
    }

    if (era_split_wire_section_present(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC)) {
        uint8_t offset = era_split_wire_section_offset(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC);
        /* The walk admitted this section, so it is exactly FULL_BYTES wide: the
           response-direction length table has one entry for this slot. */
        result->host_source_visual_snapshot.reason = payload[offset];
        for (uint8_t index = 0; index < ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES; index++) {
            result->host_source_visual_snapshot.pressed_baseline[index] =
                payload[offset + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_BYTES + index];
        }
        result->host_source_visual_snapshot_valid = true;
    }

    if (era_split_wire_section_present(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE)) {
        result->host_source_rgb_state_valid =
            era_host_peer_transaction_decode_rgb_state_body(&payload[era_split_wire_section_offset(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE)],
                                                            &result->host_source_rgb_state);
    }

    if (era_split_wire_section_present(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS)) {
        result->host_source_storage_news       = payload[era_split_wire_section_offset(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS)];
        result->host_source_storage_news_valid = true;
    }

    if (era_split_wire_section_present(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER)) {
        result->host_source_input_layer       = payload[era_split_wire_section_offset(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER)];
        result->host_source_input_layer_valid = true;
    }

    if (era_split_wire_section_present(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY)) {
        era_split_wire_decode_authority_body(&payload[era_split_wire_section_offset(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY)],
                                             &result->host_source_authority);
        result->host_source_authority_valid = true;
    }

    if (era_split_wire_section_present(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY)) {
        era_split_wire_decode_activity_body(&payload[era_split_wire_section_offset(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY)],
                                            &result->host_source_activity);
        result->host_source_activity_valid = true;
    }

    if (era_split_wire_section_present(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_TIME_ANCHOR)) {
        uint8_t offset                        = era_split_wire_section_offset(&layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_TIME_ANCHOR);
        result->host_source_time_anchor_ms    = era_split_wire_get32(&payload[offset]);
        result->host_source_time_anchor_valid = true;
    }

    return true;
}

void era_host_peer_transaction_apply_lock_state(uint8_t lock_state) {
    set_split_host_keyboard_leds(lock_state);
}

void era_host_peer_transaction_invalidate_peer_visual_baseline(void) {
    /* The applied-cache heal (Slice 14): transmit-confirmed is not
       receiver-applied, so a caller that observed a possible loss -- the
       standing stopped edge, an RGB policy flip -- drops the cache, and the
       next arriving baseline diffs against nothing instead of against a
       phantom. */
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_host_peer_transaction_peer_visual_baseline_valid = false;
    }
}

void era_host_peer_transaction_apply_visual_snapshot(const era_host_peer_visual_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

#if defined(RGB_MATRIX_ENABLE)
    uint8_t previous[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES];
    bool    previous_valid = false;
    bool    replay_pressed = era_host_peer_transaction_visual_reason_replays_pressed(snapshot->reason);
    ATOMIC_BLOCK_RESTORESTATE {
        previous_valid = g_era_host_peer_transaction_peer_visual_baseline_valid;
        era_host_peer_transaction_visual_baseline_copy(previous, g_era_host_peer_transaction_peer_visual_baseline);
        era_host_peer_transaction_visual_baseline_copy(g_era_host_peer_transaction_peer_visual_baseline, snapshot->pressed_baseline);
        g_era_host_peer_transaction_peer_visual_baseline_valid = true;
    }

    uint8_t remote_row_base = 0;
#    ifdef SPLIT_KEYBOARD
    remote_row_base = is_keyboard_left() ? MATRIX_ROWS_PER_HAND : 0;
#    endif

    uint8_t bit = 0;
    for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++, bit++) {
            bool pressed     = era_host_peer_transaction_visual_baseline_bit(snapshot->pressed_baseline, bit);
            bool was_pressed = previous_valid && era_host_peer_transaction_visual_baseline_bit(previous, bit);
            if (pressed != was_pressed || (replay_pressed && pressed)) {
                rgb_matrix_handle_key_event((uint8_t)(remote_row_base + row), col, pressed);
            }
        }
    }
#else
    ATOMIC_BLOCK_RESTORESTATE {
        era_host_peer_transaction_visual_baseline_copy(g_era_host_peer_transaction_peer_visual_baseline, snapshot->pressed_baseline);
        g_era_host_peer_transaction_peer_visual_baseline_valid = true;
    }
#endif
}

/* Returns whether this apply moved anything -- the render config, or the sleep
   fact a HOST-PEER PEER renders. That answer is what `app=rgb` counts, and it
   can only be given here: the standing record is latest-state, so the caller is
   reached whenever any field of it changed and an unchanged RGB body is
   re-applied on every unrelated section's edge. Counting at the call site
   counted standing edges -- device-shown 2026-08-13 at roughly four times the
   section arrival rate on a DUAL-HOST initiator. */
bool era_host_peer_transaction_apply_rgb_state(const era_host_peer_rgb_state_t *state, bool consume_sleep) {
    if (state == NULL) {
        return false;
    }

#if defined(RGB_MATRIX_ENABLE)
    bool         changed = false;
    rgb_config_t config = rgb_matrix_config;
    config.enable       = state->enabled ? 1 : 0;
    config.mode         = state->mode & 0x3F;
    config.hsv.h        = state->hue;
    config.hsv.s        = state->sat;
    config.hsv.v        = state->val;
    config.speed        = state->speed;
    config.flags        = state->flags;

    if (rgb_matrix_config.raw != config.raw) {
        rgb_matrix_config = config;
        changed           = true;
    }
    /* The sleep fact is HOST-PEER's (Slice 12): a DUAL-HOST caller passes false
       and each half's render gate keeps consuming only its own reduced USB
       session state, so no half can be left dark or lit by its peer's host.

       It is PUBLISHED and not written. The render gate has one owner and this
       is not it -- the resolver in era_split_keyboard.c is, and it is what
       decides whether this half is currently rendering the wire's answer or its
       own session's. Writing the gate from here is what let a demoted PEER's own
       stale sleep latch survive underneath the wire's answer. */
    if (consume_sleep) {
        changed = era_split_keyboard_note_wire_lighting_sleep(state->sleep) || changed;
    }
    return changed;
#else
    (void)state;
    (void)consume_sleep;
    return false;
#endif
}

static bool g_era_host_peer_transaction_time_anchor_adopted;

bool era_host_peer_transaction_time_anchor_adopted(void) {
    return g_era_host_peer_transaction_time_anchor_adopted;
}

void era_host_peer_transaction_forget_time_anchor(void) {
    /* Relation identity, not boot lifetime, is the proof boundary. In
       particular a sequential half reflash can leave this initiator running
       after it adopted the OLD responder's uptime; when the responder reboots,
       carrying this true across the relation rotation lets LINK_SPEED/CLEAN
       arm a deadline before any anchor from the new uptime has arrived. Keep
       the corrected clock value for ordinary consumers, but withdraw exactly
       the permission to derive a new pair deadline from it. */
    g_era_host_peer_transaction_time_anchor_adopted = false;
}

void era_host_peer_transaction_apply_time_anchor(uint32_t anchor_ms, uint32_t rx_us) {
    // Transport time service: align the shared sync timer to the HOST's
    // timeline so every sync-timer consumer (RGB effect phase first) runs
    // in phase.
    //
    // The held term is what makes a wire timestamp safe to apply late: it is
    // the time this anchor spent between core1 receiving it and core0 acting
    // on it, and adding it back makes the applied value the one the anchor
    // would have carried now. The sender's publish-to-send gap arrives
    // already corrected since R6 -- core1 adds the measured hold at encode --
    // so what remains uncorrected is sub-millisecond wire transit, and no
    // fixed stand-in may be added back for it (era_wire_contract.md).
    const uint32_t held_ms    = (uint32_t)((timer_hw->timerawl - rx_us) / 1000U);
    const uint32_t applied_ms = anchor_ms + held_ms;
    g_era_host_peer_transaction_time_anchor_adopted = true;
    sync_timer_update(applied_ms);
}

void era_host_peer_transaction_apply_activity(const era_split_wire_activity_section_t *activity) {
    era_split_tap_activity_apply_peer(activity);
}

/* The lane-result apply retired with the second carrier (D3). Of the appliers
   above, LOCK_STATE and TIME_ANCHOR reach only the standing drain; VISUAL_RESYNC,
   RGB_STATE and ACTIVITY also reach the DUAL-HOST responder-result apply, which
   is the opposite direction's carrier. The INPUT_LAYER and storage-hint wrappers
   went with the drain rather than joining that list: the live callers reach
   era_split_peer_layer_apply() and era_host_peer_storage_note_host_news()
   directly, behind the policy gate and the eligibility clip a wrapper here does
   not carry. */
