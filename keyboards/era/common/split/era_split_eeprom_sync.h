// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ERA_SPLIT_EEPROM_SYNC_CLASS 0xE0

/* The indicator's minimum-visible floor, anchored at each half's own lamp
   rise (2026-08-14 redesign). It pads a short process to visibility and
   never extends a long one: once the pending fact falls, the lamp survives
   only to rise + floor, so it is a rise-anchored guarantee rather than a
   trailing delay. Both halves' rises sit within one poll of each other, so
   the floors expire symmetrically. Kept from the retired local-save
   presenter (whose constant carried the STATUS_ prefix); the trailing
   1500 ms sync bridge that stood beside it is gone with nothing in its
   place — the wire-carried pending fact is what replaced the time. */
#ifndef ERA_SPLIT_EEPROM_SYNC_MIN_VISIBLE_MS
#    define ERA_SPLIT_EEPROM_SYNC_MIN_VISIBLE_MS 160
#endif


#define ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE 255U

typedef enum {
    ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ    = 0xE0,
    ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP    = 0xE1,
    ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ    = 0xE2,
    ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_RSP    = 0xE3,
    ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ    = 0xE4,
    ERA_SPLIT_EEPROM_SYNC_OP_APPLY_RSP    = 0xE5,
    ERA_SPLIT_EEPROM_SYNC_OP_COMPLETE_REQ = 0xE6,
    ERA_SPLIT_EEPROM_SYNC_OP_CLOSE_RSP    = 0xE7,
    ERA_SPLIT_EEPROM_SYNC_OP_ABORT_REQ    = 0xE8,
    ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP    = 0xE9,
    /* Slice 10 replacement re-expression: the recency/arbitration exchange
     * and the push lane. SYNC_STATUS uses the common identity prefix with
     * domain 0xFF (DOMAIN_NONE) as the whole-family summary form and a real
     * domain id as the per-conflict counter form. PUSH_CHUNK_REQ is the one
     * initiator-sent bulk operation; PUSH_CTL_REQ carries the compact push
     * phases (open/apply/complete/abort) in its body. 0xEF stays reserved. */
    ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ = 0xEA,
    ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_RSP = 0xEB,
    ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ  = 0xEC,
    ERA_SPLIT_EEPROM_SYNC_OP_PUSH_RSP        = 0xED,
    ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ    = 0xEE,
} era_split_eeprom_sync_op_t;

/* The request-to-response operation mapping, shared deliberately.
 *
 * It lived as two identical private copies, one in core0's engine and one in
 * core1's codec, until Slice 10 Stage D extended core1's for the sync-status
 * and push operations and left core0's at five cases. Core0 then set its
 * pending operation to a new id and rejected its own correct response as
 * stale, on the first request of every relation, which cost the whole stage
 * and a device session to find. A mapping both cores must agree on byte for
 * byte cannot be stored twice, so there is now one definition and no copy to
 * fall behind: adding an operation to the enum above and forgetting this
 * function is a compile-time absence, not a runtime stall.
 *
 * It stays a pure operation-to-operation function precisely so it can live
 * here. Neither core's private types cross this header, which is the
 * property that kept the two copies apart in the first place.
 *
 * Zero is returned for a request with no response operation, and zero is
 * never a valid operation id, so a caller comparing against it always
 * mismatches rather than accidentally matching some other frame. */
static inline uint8_t era_split_eeprom_sync_response_operation(uint8_t request_operation) {
    switch ((era_split_eeprom_sync_op_t)request_operation) {
        case ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ:
            return ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP;
        case ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ:
            return ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_RSP;
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ:
            return ERA_SPLIT_EEPROM_SYNC_OP_APPLY_RSP;
        case ERA_SPLIT_EEPROM_SYNC_OP_COMPLETE_REQ:
            return ERA_SPLIT_EEPROM_SYNC_OP_CLOSE_RSP;
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_REQ:
            return ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP;
        case ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ:
            return ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_RSP;
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ:
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ:
            return ERA_SPLIT_EEPROM_SYNC_OP_PUSH_RSP;
        default:
            return 0;
    }
}

typedef enum {
    ERA_SPLIT_EEPROM_SYNC_STATUS_MATCH = 0,
    ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER,
    ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY,
    ERA_SPLIT_EEPROM_SYNC_STATUS_COMPLETE,
    ERA_SPLIT_EEPROM_SYNC_STATUS_ABORTED,
    ERA_SPLIT_EEPROM_SYNC_STATUS_POLICY_CLOSED,
    ERA_SPLIT_EEPROM_SYNC_STATUS_UNSUPPORTED_DOMAIN,
    ERA_SPLIT_EEPROM_SYNC_STATUS_UNSUPPORTED_SCHEMA,
    ERA_SPLIT_EEPROM_SYNC_STATUS_SIZE_MISMATCH,
    ERA_SPLIT_EEPROM_SYNC_STATUS_STALE,
    ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY,
    ERA_SPLIT_EEPROM_SYNC_STATUS_INTEGRITY_FAIL,
    ERA_SPLIT_EEPROM_SYNC_STATUS_RESULT_FULL,
    ERA_SPLIT_EEPROM_SYNC_STATUS_TIMEOUT,
    ERA_SPLIT_EEPROM_SYNC_STATUS_ROLE_CHANGED,
    ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED,
} era_split_eeprom_sync_status_t;

/* PUSH_CTL_REQ body phases (byte 6). OPEN/APPLY/COMPLETE carry the push
 * source revision and full-image CRC32; ABORT carries the revision and an
 * abort reason status. The responder answers every phase with PUSH_RSP. */
typedef enum {
    ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_OPEN = 0,
    ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_APPLY,
    ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_COMPLETE,
    ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_ABORT,
} era_split_eeprom_sync_push_phase_t;

typedef enum {
    ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG = 0,
    ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP,
    ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_MACRO,
    ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_RGB_MATRIX,
    ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_KEYMAP_CONFIG,
    ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_DEFAULT_LAYER,
    ERA_SPLIT_EEPROM_SYNC_DOMAIN_VIA_LAYOUT_OPTIONS,
    ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT,
} era_split_eeprom_sync_domain_t;

typedef struct {
    uint32_t span_count;
    uint32_t span_rise_ms;
    uint32_t span_fall_ms;
    uint32_t red_era_count;
    uint32_t red_on_ms;
    uint32_t red_off_ms;
    uint32_t break_ms;
    uint8_t  break_count;
    uint8_t  break_flags;
    uint8_t  break_state;
    uint8_t  pending_bits;
    uint8_t  visible;
} era_split_eeprom_sync_diagnostics_t;

/* The breaker latch's panel-state bits, packed by the board flush hook at
   the instant it reports a frame: the three facts that discriminate which
   path produced a statusless frame while the lamp was commanded visible. */
#define ERA_SPLIT_EEPROM_SYNC_BREAK_STATE_RGB_ENABLED 0x01
#define ERA_SPLIT_EEPROM_SYNC_BREAK_STATE_RGB_SUSPENDED 0x02
#define ERA_SPLIT_EEPROM_SYNC_BREAK_STATE_STATUS_POLICY_ON 0x04

/* The one indicator predicate, family-wide (2026-08-14 redesign): the
   storage engine's pending fact — this half's own arm plus the peer's
   wire-carried mirror, era_host_peer_storage_indicator_pending() — held to
   the rise-anchored minimum-visible floor above. No trailing delay exists
   anywhere in this path: the lamp rises with the operation (the edited
   half's dirty note; the peer within a poll of settle), holds through
   transfer and durable write on the engine's own state, and both halves
   fall within one poll of the initiator's last close, because the
   responder's fall is the mirror's zero crossing rather than a local
   clock's guess. The fixed 1500 ms bridge this replaces padded the mirror's
   absence with time, which is why no value of it could be right. */
bool era_split_eeprom_sync_indicator_visible_advance(void);
/* Diagnostics-only LED truth, called by the board's render-policy flush hook
   on EVERY flushed frame with whether that frame was the STATUS field, the
   frame's raw flags, and the packed panel-state bits above.
   Edge-only: stamps the first STATUS flush of an era (`ron`) and the first
   non-STATUS flush after it (`roff`) — the moment the red actually left the
   panel, which no predicate stamp can see. A red era that breaks while the
   lamp is still commanded visible is the healed-trigger frame (the span's
   own end always breaks with the lamp already dark), and the note latches
   that frame's identity — flags and panel state — because four bracketed
   samples correlate with the durable-writer's flash-activity window but the
   count alone cannot say which path rendered the statusless frame: a
   zero-flag NONE/suspend fill, an enable drop, or an effect frame the
   arbitration stripped. One breaker readout names it. */
void era_split_eeprom_sync_note_status_frame_presence(bool status_frame, uint8_t frame_flags, uint8_t panel_state_bits);
void era_split_eeprom_sync_reload_domain_kb(era_split_eeprom_sync_domain_t domain);
void era_split_eeprom_sync_reload_domain_user(era_split_eeprom_sync_domain_t domain);
void era_split_eeprom_sync_get_diagnostics_snapshot(era_split_eeprom_sync_diagnostics_t *snapshot);
