// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_wire_payload.h"

#include <string.h>

/* The EEPROM layout header left with the link level: this unit no longer names
   a level, and the restart intent's own bounds are asked of the acts. */
#include "era_split_eeprom_sync.h"
#include "era_split_restart_agreement.h"
#include "era_split_matrix_frame.h"
#include "era_split_mode_planner.h"
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "communication_core/era_split_communication_core_storage.h"
#endif

/* Per-relation, per-direction section eligibility, and the only thing that
 * opens a section. The planner ANDs its computed mask with this half's entry
 * and admission ANDs the received mask with the same entry, so a section
 * absent here can be neither sent nor accepted however the surrounding code
 * is written -- an added condition elsewhere cannot open a surface, because
 * the AND clips it.
 *
 * Keyed by relation rather than by role. Both halves of one relation agree on
 * which sections that relation carries, so the sender's send-clip and the
 * receiver's accept-clip read the same entry and cannot disagree. A HOST-PEER
 * HOST never sends a push, but it does receive one, and it must accept
 * exactly MATRIX.
 *
 * Externally visible and indexed by a runtime mode value on purpose: this is
 * what the ELF gate reads with objdump, the way the .vectors gate reads the
 * linked table's own bytes instead of a list of names. A gate built from
 * absent symbols cannot see the failure that matters here, because reusing
 * this envelope pair in a second relation makes the ops legitimately present
 * in both. */
const uint8_t g_era_split_wire_section_eligibility[ERA_SPLIT_WIRE_SECTION_ELIGIBILITY_MODES][2] = {
    [ERA_SPLIT_MODE_LOCAL_NO_LINK]   = {0, 0},
    [ERA_SPLIT_MODE_HOST_PEER_HOST]  = {ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_PUSH, ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_RSP},
    [ERA_SPLIT_MODE_HOST_PEER_PEER]  = {ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_PUSH, ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_RSP},
    [ERA_SPLIT_MODE_DUAL_HOST_LEFT]  = {ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_PUSH, ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_RSP},
    [ERA_SPLIT_MODE_DUAL_HOST_RIGHT] = {ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_PUSH, ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_RSP},
};

/* The table is sized by a wire constant so era_split_wire_protocol.h need not
   include the planner; this is what keeps the two in step. */
_Static_assert((uint8_t)ERA_SPLIT_MODE_DUAL_HOST_RIGHT + 1U == ERA_SPLIT_WIRE_SECTION_ELIGIBILITY_MODES,
               "The section eligibility table must have one entry per relation mode.");

uint8_t era_split_wire_eligible_sections(uint8_t mode, uint8_t direction) {
    if (mode >= ERA_SPLIT_WIRE_SECTION_ELIGIBILITY_MODES || direction > ERA_SPLIT_WIRE_SECTION_DIRECTION_RSP) {
        return 0;
    }
    return g_era_split_wire_section_eligibility[mode][direction];
}

/* Fixed body length per section, indexed by marker-bit position. Every section
   this direction carries has one, visual-resync included since its reason-only
   form retired, so the walk has no variable-length arm in either direction.
   A zero entry means "not a section", which is why the walk consults the
   section *mask* first and this table only for a bit the mask admits: slot 1
   held the Slice 11 ACTIVITY marker, whose genuinely zero-length body is what
   made a zero unusable as the variable-length sentinel in the first place --
   and since FA-2 it carries the activity body that reuses the bit -- seven
   bytes until the release stamp widened it to eleven. */
static const uint8_t era_split_wire_host_source_rsp_body_bytes[ERA_SPLIT_WIRE_SECTION_SLOTS] = {
    [0] = ERA_SPLIT_WIRE_INPUT_LAYER_BYTES,
    [1] = ERA_SPLIT_WIRE_ACTIVITY_BYTES,
    [2] = ERA_SPLIT_WIRE_AUTHORITY_BYTES,
    [3] = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_BYTES,
    [4] = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_FULL_BYTES,
    [5] = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES,
    [6] = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_BYTES,
    [7] = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_TIME_ANCHOR_BYTES,
};

static const uint8_t era_split_wire_source_push_body_bytes[ERA_SPLIT_WIRE_SECTION_SLOTS] = {
    [0] = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_MATRIX_BYTES,
    [1] = ERA_SPLIT_WIRE_INPUT_LAYER_BYTES,
    [2] = ERA_SPLIT_WIRE_AUTHORITY_BYTES,
    [3] = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RGB_STATE_BYTES,
    [4] = ERA_SPLIT_WIRE_ACTIVITY_BYTES,
    [5] = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_VISUAL_BYTES,
    [6] = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_BYTES,
    [7] = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ARM_BYTES,
};

void era_split_wire_encode_authority_body(const era_split_wire_authority_section_t *authority, uint8_t *body) {
    if (authority == NULL || body == NULL) {
        return;
    }
    uint8_t flags = 0;
    if (authority->accepted_host_open) {
        flags |= ERA_SPLIT_WIRE_AUTHORITY_FLAG_HOST_OPEN;
    }
    if (authority->accepted_no_host) {
        flags |= ERA_SPLIT_WIRE_AUTHORITY_FLAG_NO_HOST;
    }
    if (authority->matrix_ready) {
        flags |= ERA_SPLIT_WIRE_AUTHORITY_FLAG_MATRIX_READY;
    }
    /* The restart intent shares this byte, with the act and the param carried
       once for both phases: at most one is live, because arming consumes the
       request. A half with no intent encodes act zero, which is the idle
       value. */
    if (authority->restart_armed) {
        flags |= ERA_SPLIT_WIRE_AUTHORITY_FLAG_RESTART_ARMED;
    }
    flags |= (uint8_t)((uint8_t)(authority->restart_act << ERA_SPLIT_WIRE_AUTHORITY_RESTART_ACT_SHIFT) & ERA_SPLIT_WIRE_AUTHORITY_RESTART_ACT_MASK);
    flags |= (uint8_t)((uint8_t)(authority->restart_param << ERA_SPLIT_WIRE_AUTHORITY_RESTART_PARAM_SHIFT) & ERA_SPLIT_WIRE_AUTHORITY_RESTART_PARAM_MASK);
    body[0] = flags;
    era_split_wire_put16(&body[1], authority->usb_epoch);
    era_split_wire_put16(&body[3], authority->host_open_generation);
    era_split_wire_put16(&body[5], authority->host_close_generation);
}

void era_split_wire_decode_authority_body(const uint8_t *body, era_split_wire_authority_section_t *authority) {
    if (body == NULL || authority == NULL) {
        return;
    }
    authority->accepted_host_open    = (body[0] & ERA_SPLIT_WIRE_AUTHORITY_FLAG_HOST_OPEN) != 0;
    authority->accepted_no_host      = (body[0] & ERA_SPLIT_WIRE_AUTHORITY_FLAG_NO_HOST) != 0;
    authority->matrix_ready          = (body[0] & ERA_SPLIT_WIRE_AUTHORITY_FLAG_MATRIX_READY) != 0;
    authority->restart_act           = (uint8_t)((body[0] & ERA_SPLIT_WIRE_AUTHORITY_RESTART_ACT_MASK) >> ERA_SPLIT_WIRE_AUTHORITY_RESTART_ACT_SHIFT);
    authority->restart_param         = (uint8_t)((body[0] & ERA_SPLIT_WIRE_AUTHORITY_RESTART_PARAM_MASK) >> ERA_SPLIT_WIRE_AUTHORITY_RESTART_PARAM_SHIFT);
    authority->restart_armed         = (body[0] & ERA_SPLIT_WIRE_AUTHORITY_FLAG_RESTART_ARMED) != 0;
    authority->usb_epoch             = era_split_wire_get16(&body[1]);
    authority->host_open_generation  = era_split_wire_get16(&body[3]);
    authority->host_close_generation = era_split_wire_get16(&body[5]);
}

bool era_split_wire_authority_equal(const era_split_wire_authority_section_t *lhs, const era_split_wire_authority_section_t *rhs) {
    return lhs != NULL && rhs != NULL &&
           lhs->accepted_host_open == rhs->accepted_host_open &&
           lhs->accepted_no_host == rhs->accepted_no_host &&
           lhs->matrix_ready == rhs->matrix_ready &&
           /* The restart intent is part of this comparison and not part of the
              session cache's, and the split is the point: this function is both
              the sender's shadow and the receiver's edge, so an intent change
              must cross and must be delivered, while
              era_split_scheduler_session_note_peer_authority() deliberately
              ignores the same fields because a restart intent is not a session
              edge. */
           lhs->restart_act == rhs->restart_act &&
           lhs->restart_param == rhs->restart_param &&
           lhs->restart_armed == rhs->restart_armed &&
           lhs->usb_epoch == rhs->usb_epoch &&
           lhs->host_open_generation == rhs->host_open_generation &&
           lhs->host_close_generation == rhs->host_close_generation;
}

/* The ACTIVITY body codec (FA-2 S2), on the AUTHORITY codec's shape: one pair
   for both id spaces, one `equal` for every shadow, and the decoder runs only
   on a body the layout walk already proved well-formed. */
void era_split_wire_encode_activity_body(const era_split_wire_activity_section_t *activity, uint8_t *body) {
    if (activity == NULL || body == NULL) {
        return;
    }
    body[0] = activity->window_open ? ERA_SPLIT_WIRE_ACTIVITY_FLAG_WINDOW_OPEN : 0;
    body[1] = activity->press_count;
    body[2] = activity->release_count;
    era_split_wire_put32(&body[3], activity->last_press_ms);
    era_split_wire_put32(&body[7], activity->last_release_ms);
}

void era_split_wire_decode_activity_body(const uint8_t *body, era_split_wire_activity_section_t *activity) {
    if (body == NULL || activity == NULL) {
        return;
    }
    activity->window_open     = (body[0] & ERA_SPLIT_WIRE_ACTIVITY_FLAG_WINDOW_OPEN) != 0;
    activity->press_count     = body[1];
    activity->release_count   = body[2];
    activity->last_press_ms   = era_split_wire_get32(&body[3]);
    activity->last_release_ms = era_split_wire_get32(&body[7]);
}

bool era_split_wire_activity_equal(const era_split_wire_activity_section_t *lhs, const era_split_wire_activity_section_t *rhs) {
    return lhs != NULL && rhs != NULL &&
           lhs->window_open == rhs->window_open &&
           lhs->press_count == rhs->press_count &&
           lhs->release_count == rhs->release_count &&
           lhs->last_press_ms == rhs->last_press_ms &&
           lhs->last_release_ms == rhs->last_release_ms;
}

/* Reserved flag bits zero; the counters and the instants admit every value. */
static bool era_split_wire_activity_body_valid(const uint8_t *body) {
    return (body[0] & (uint8_t)~ERA_SPLIT_WIRE_ACTIVITY_FLAG_MASK) == 0;
}

/* The RGB-state body's own validation, one rule for both id spaces (Slice 12):
   the body is byte-identical in the two directions, so the two walks may not
   disagree about what a valid one is. Reserved flag bits and the mode byte's
   top two bits are zero; every other byte admits every value. */
static bool era_split_wire_rgb_state_body_valid(const uint8_t *body) {
    return (body[0] & (uint8_t)~ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_FLAG_MASK) == 0 &&
           (body[1] & 0xC0) == 0;
}

/* The AUTHORITY body's own validation: exactly one role bit, matrix_ready only
   on a no-host half, and the restart intent's own rules. Two carriers for one
   fact set may not disagree about what a valid one is, and the two rules on
   the shared facts are the ones era_split_wire_validate_session_status_payload()
   below applies too.

   Reserved-zero used to be a third shared rule, and it was where the two
   differed first: Slice 10.5 narrowed SESSION_STATUS to `flags & 0x03` and
   left 0x04, 0x08 and 0x20 unvalidated, which bought mixed-revision tolerance
   that owner decision 2026-08-10 retired, while this byte refused its reserved
   bits from the day Slice 11.6 opened the section. On 2026-08-19 the agreed
   restart took this byte's last reserved bit, so the rule is now
   SESSION_STATUS's alone -- and what the two share is its wider form, each
   carrier refusing whatever it has no fact for. */
static bool era_split_wire_authority_body_valid(const uint8_t *body) {
    uint8_t flags = body[0];
    /* No reserved-bit test, because this byte has none left: the mask is 0xFF
       and the header asserts it. The rule is not dropped, it is discharged --
       every bit below is checked for what it may say, which is a stronger test
       than "is anything set that should not be". */
    uint8_t role = flags & ERA_SPLIT_WIRE_AUTHORITY_FLAG_ROLE_MASK;
    if (role != ERA_SPLIT_WIRE_AUTHORITY_FLAG_HOST_OPEN && role != ERA_SPLIT_WIRE_AUTHORITY_FLAG_NO_HOST) {
        return false;
    }
    if ((flags & ERA_SPLIT_WIRE_AUTHORITY_FLAG_MATRIX_READY) != 0 &&
        (flags & ERA_SPLIT_WIRE_AUTHORITY_FLAG_NO_HOST) == 0) {
        return false;
    }
    /* The restart intent's own rules, local to this carrier because
       SESSION_STATUS does not carry the fact. The act-specific validator owns
       every valid act/param/armed combination, so a malformed or impossible
       phase never reaches the state machine. */
    uint8_t restart_act   = (uint8_t)((flags & ERA_SPLIT_WIRE_AUTHORITY_RESTART_ACT_MASK) >> ERA_SPLIT_WIRE_AUTHORITY_RESTART_ACT_SHIFT);
    uint8_t restart_param = (uint8_t)((flags & ERA_SPLIT_WIRE_AUTHORITY_RESTART_PARAM_MASK) >> ERA_SPLIT_WIRE_AUTHORITY_RESTART_PARAM_SHIFT);
    if (!era_split_restart_authority_valid(restart_act, restart_param, (flags & ERA_SPLIT_WIRE_AUTHORITY_FLAG_RESTART_ARMED) != 0)) {
        return false;
    }
    return true;
}

/* One walk of byte2's mask, summing declared body lengths in ascending
   marker-bit order; the walk must land exactly on payload_len.

   There is no variable section in either direction, and the machinery for one
   was retired on 2026-08-11. The refusal it carried is unchanged and is the
   whole reason to be careful here: a mask claiming more than `payload_len`
   allows is refused as malformed rather than truncated. That refusal was never
   the variable machinery -- it is the `payload_len != 3 + fixed_total` check
   below, which the parameter selected *around* rather than provided, so it
   survives as an unconditional test and the bound two active texts lean on is
   exactly as strong as it was. Reopening a variable section means restoring
   the parameter and this comment with it. */
/* Declared body bytes of exactly the sections a mask claims, off one of the two
   tables above. The walk's own use is the receive side of it; the send side is
   era_split_wire_source_push_projected_len() below. */
static uint16_t era_split_wire_section_body_total(const uint8_t *body_bytes, uint8_t sections) {
    uint16_t total = 0;
    for (uint8_t slot = 0; slot < ERA_SPLIT_WIRE_SECTION_SLOTS; slot++) {
        if ((sections & (uint8_t)(1U << slot)) != 0) {
            total = (uint16_t)(total + body_bytes[slot]);
        }
    }
    return total;
}

/* What a source-push payload carrying exactly `sections` will occupy: the
   three-byte head plus each claimed section's declared body. It is the send
   side of the walk's `payload_len != 3 + fixed_total` test, and reading the
   table is the whole of why it exists.

   Four call sites hand-unrolled this before -- the enqueue validator in
   communication_core/era_split_communication_core_host_peer_lanes.c and the
   three yielding arms of the standing exchange in
   communication_core/era_split_communication_core_standing.c -- each summing
   whichever prefix of the section set its own arm happened to run after. A
   section opened later is invisible to an arm written that way and visible to
   the table by construction, which is the defect the fold removes rather than
   the four copies.

   The response direction's equivalent is plan-shaped rather than mask-shaped
   and stays where it is: era_host_peer_responder.c's
   era_host_peer_transaction_responder_projected_len() reads a plan of booleans,
   which has no mask to index this table with. It is one statement of its own,
   which is what this side did not have. */
uint16_t era_split_wire_source_push_projected_len(uint8_t sections) {
    return (uint16_t)(3U + era_split_wire_section_body_total(era_split_wire_source_push_body_bytes, sections));
}

static bool era_split_wire_layout_walk(const uint8_t *payload, uint16_t payload_len, const uint8_t *body_bytes, uint8_t section_mask, era_split_wire_section_layout_t *layout) {
    if (payload == NULL || layout == NULL || payload_len < 3) {
        return false;
    }

    uint8_t sections = payload[2];
    if ((sections & (uint8_t)~section_mask) != 0) {
        return false;
    }

    memset(layout, 0, sizeof(*layout));
    layout->sections = sections;

    if (payload_len != (uint16_t)(3U + era_split_wire_section_body_total(body_bytes, sections))) {
        return false;
    }

    uint16_t offset = 3;
    for (uint8_t slot = 0; slot < ERA_SPLIT_WIRE_SECTION_SLOTS; slot++) {
        uint8_t marker = (uint8_t)(1U << slot);
        if ((sections & marker) == 0) {
            continue;
        }
        uint16_t length      = body_bytes[slot];
        layout->offset[slot] = (uint8_t)offset;
        offset               = (uint16_t)(offset + length);
    }
    return offset == payload_len;
}

static bool era_split_wire_validate_session_status_payload(const uint8_t *payload, uint16_t payload_len) {
    if (payload_len != 9 || payload[1] != 0x10) {
        return false;
    }

    /* Reserved bits zero, then exactly one authority bit.
     *
     * The reserved-bit rule is new and it is a deliberate narrowing. This check
     * was a low-nibble `mode` test until Slice 10.5, which also forced 0x04 and
     * 0x08 to zero; 10.5 narrowed it to the two authority bits so 0x04 could
     * carry the storage-changed hint, and left 0x04, 0x08 and 0x20 unvalidated
     * once 11.7 gave that hint back to the response section envelope. The
     * comment that stood here warned that re-tightening to the nibble would be
     * "a wire change wearing a validator cleanup's clothes", and it was right
     * about the shape: this is a wire change, taken as one.
     *
     * What it was bought with was mixed-revision tolerance -- a peer setting a
     * bit this revision does not assign was still accepted -- and by owner
     * decision 2026-08-10 both halves always run the identical image, so that
     * price is now paid for nothing. What it costs is that a future flag bit
     * needs one line here rather than none. What it buys is that the two
     * carriers of this fact set agree: the AUTHORITY section validator refused
     * every bit it had no fact for from the day Slice 11.6 opened it (and, its
     * flags byte having no reserved bit left since 2026-08-19, now refuses
     * every *value* it has no fact for), and a frame one of them admits and
     * the other refuses is the disagreement the wire contract's one-fact-set
     * rule exists to prevent.
     *
     * The bit-by-bit history is in era_split_wire_protocol.h's SESSION_STATUS
     * flags block, and a capture in which a SESSION_STATUS sets 0x04 or 0x08
     * still dates the image that sent it. */
    uint8_t flags = payload[2];
    if ((flags & (uint8_t)~ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MASK) != 0) {
        return false;
    }
    uint8_t authority = flags & ERA_SPLIT_WIRE_SESSION_STATUS_AUTHORITY_MASK;
    if (authority != ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_HOST_OPEN &&
        authority != ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_NO_HOST) {
        return false;
    }
    if ((flags & ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MATRIX_READY) != 0 &&
        (flags & ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_NO_HOST) == 0) {
        return false;
    }
    return true;
}

static bool era_split_wire_host_peer_visual_baseline_reserved_bits_clear(const uint8_t *baseline) {
    if (baseline == NULL) {
        return false;
    }

    uint8_t used_bits = ERA_SPLIT_WIRE_HALF_MATRIX_BITS & 0x07;
    if (used_bits != 0) {
        uint8_t final_mask = (uint8_t)((1U << used_bits) - 1U);
        if ((baseline[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES - 1] & (uint8_t)~final_mask) != 0) {
            return false;
        }
    }

    for (uint8_t index = ERA_SPLIT_WIRE_HALF_MATRIX_BYTES;
         index < ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES;
         index++) {
        if (baseline[index] != 0) {
            return false;
        }
    }
    return true;
}

/* A layout that succeeds means every present body is well-formed, so the
   extractors read it without re-validating. That is the point of walking
   once: the five HOST-source extractors used to derive their own offsets from
   the mask, which is five places a body-size change had to be repeated and
   the exact shape the lock relocation would otherwise break silently. */
bool era_split_wire_layout_host_source_rsp(const uint8_t *payload, uint16_t payload_len, era_split_wire_section_layout_t *layout) {
    if (!era_split_wire_layout_walk(payload, payload_len,
                                    era_split_wire_host_source_rsp_body_bytes,
                                    ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_MASK,
                                    layout)) {
        return false;
    }

    if (era_split_wire_section_present(layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY) &&
        !era_split_wire_activity_body_valid(&payload[era_split_wire_section_offset(layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY)])) {
        return false;
    }

    if (era_split_wire_section_present(layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY) &&
        !era_split_wire_authority_body_valid(&payload[era_split_wire_section_offset(layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY)])) {
        return false;
    }

    if (era_split_wire_section_present(layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE)) {
        uint8_t lock = payload[era_split_wire_section_offset(layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE)];
        if ((lock & (uint8_t)~ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_VALUE_MASK) != 0) {
            return false;
        }
    }

    /* Fixed at the full width, and the two directions now check it the same
       way. The reason-only form -- a one-byte body carrying a reason with no
       baseline behind it -- was retired because no encoder in either direction
       could produce one: every response-direction plan comes from a capture
       that returns false unless it has a baseline, core1's encoder appends
       reason plus the whole baseline with no branch, and the push direction is
       fixed width by the length table. The snapshot's own validity flag went
       with the form on 2026-08-11 (era_host_peer_transaction.h). An accepted length no sender can emit is a
       frame the receiver admits and then has to decide what to do with, which
       is the shape this pass exists to remove. */
    if (era_split_wire_section_present(layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC)) {
        uint8_t offset = era_split_wire_section_offset(layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC);
        if (payload[offset] > ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_MAX ||
            !era_split_wire_host_peer_visual_baseline_reserved_bits_clear(&payload[offset + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_BYTES])) {
            return false;
        }
    }

    if (era_split_wire_section_present(layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE) &&
        !era_split_wire_rgb_state_body_valid(&payload[era_split_wire_section_offset(layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE)])) {
        return false;
    }

    /* **Zero is a legal mask value, and it is the all-clear** (D1). The reject
       that stood here was the decoder's half of a carrier that could not
       express a fall: the responder omitted the section when it had nothing
       settled, so "absent" had to be read as all-clear, and a receiver that
       consumes edges can never observe an absence. The section now rides a
       sent-state shadow like its eight siblings, so each transition crosses
       exactly once and the transition to zero is the one that closes the
       episode.

       The reserved-bit term was the last thing this body could be wrong
       about, and the 2026-08-14 entry-symmetry change retired it: bit7 became
       the responder's storage-pending fact (STORAGE_NEWS_FLAG_PENDING), so
       every value of the byte is now valid -- bits 0..6 the news value, bit7
       the pending flag -- and this section joined INPUT_LAYER and TIME_ANCHOR
       below in admitting its full width. Each widening moved the Opening Rule
       with it (era_closed_surface_contract.md). */

    /* INPUT_LAYER and TIME_ANCHOR admit every value of their width. A layer
       state with bits above this board's layer count is not a wire error: the
       composing half simply resolves no action on a layer its keymap does not
       define. */
    return true;
}

bool era_split_wire_layout_source_push(const uint8_t *payload, uint16_t payload_len, era_split_wire_section_layout_t *layout) {
    if (!era_split_wire_layout_walk(payload, payload_len,
                                    era_split_wire_source_push_body_bytes,
                                    ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MASK,
                                    layout)) {
        return false;
    }

    /* A section-less request is the one-byte compact control payload, never a
       zero-section 0x20, so an empty mask here is a malformed frame rather
       than an empty envelope. The response direction is the opposite case:
       byte2 == 0 is its no-section envelope and stays valid. */
    if (layout->sections == 0) {
        return false;
    }

    if (era_split_wire_section_present(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MATRIX) &&
        !era_split_wire_matrix_reserved_bits_clear(&payload[era_split_wire_section_offset(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MATRIX)])) {
        return false;
    }

    if (era_split_wire_section_present(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY) &&
        !era_split_wire_authority_body_valid(&payload[era_split_wire_section_offset(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY)])) {
        return false;
    }

    if (era_split_wire_section_present(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE) &&
        !era_split_wire_rgb_state_body_valid(&payload[era_split_wire_section_offset(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE)])) {
        return false;
    }

    if (era_split_wire_section_present(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY) &&
        !era_split_wire_activity_body_valid(&payload[era_split_wire_section_offset(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY)])) {
        return false;
    }

    if (era_split_wire_section_present(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL)) {
        uint16_t visual_offset = era_split_wire_section_offset(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL);
        if (payload[visual_offset] > ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_MAX ||
            !era_split_wire_host_peer_visual_baseline_reserved_bits_clear(
                &payload[visual_offset + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_BYTES])) {
            return false;
        }
    }

    /* Storage-pending: one flag bit, seven reserved-zero. Reserved bits are
       refused rather than ignored, the discipline every section on this
       envelope pair carries. */
    if (era_split_wire_section_present(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING)) {
        uint8_t pending_value = payload[era_split_wire_section_offset(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING)];
        if ((pending_value & (uint8_t)~ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_FLAG_MASK) != 0) {
            return false;
        }
    }

    /* Restart arm: reserved bits are refused like every other section here;
       the act-specific validator owns every valid act/param/deadline phase. */
    if (era_split_wire_section_present(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM)) {
        uint16_t       arm_offset = era_split_wire_section_offset(layout, ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM);
        const uint8_t *arm_body   = &payload[arm_offset];
        if ((arm_body[0] & (uint8_t)~ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_FLAG_MASK) != 0) {
            return false;
        }
        uint8_t  act       = (uint8_t)((arm_body[0] & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_MASK) >> ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_SHIFT);
        uint8_t  param     = (uint8_t)(arm_body[0] & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_PARAM_MASK);
        uint32_t commit_ms = era_split_wire_get32(&arm_body[1]);
        if (!era_split_restart_arm_valid(act, param, commit_ms)) {
            return false;
        }
    }
    return true;
}

bool era_split_wire_validate_bulk_page_payload(const uint8_t *payload, uint16_t payload_len) {
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    if (era_split_communication_core_storage_validate_wire_payload(payload, payload_len, ERA_SPLIT_WIRE_FRAME_LANE_BULK_PAGE)) {
        return true;
    }
#else
    (void)payload;
    (void)payload_len;
#endif
    return false;
}

bool era_split_wire_classify_payload(const uint8_t *payload, uint16_t payload_len, era_split_wire_frame_lane_t lane, era_split_wire_payload_kind_t *kind)
    __attribute__((aligned(16)));
bool era_split_wire_classify_payload(const uint8_t *payload, uint16_t payload_len, era_split_wire_frame_lane_t lane, era_split_wire_payload_kind_t *kind) {
    *kind = ERA_SPLIT_WIRE_PAYLOAD_INVALID;

    uint8_t control = payload[0];
    if ((control & ERA_SPLIT_WIRE_CONTROL_RESERVED) != 0 || (control & ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK) == 0) {
        return false;
    }

    if (lane == ERA_SPLIT_WIRE_FRAME_LANE_BULK_PAGE) {
        if ((control & ERA_SPLIT_WIRE_CONTROL_EXT) == 0 ||
            !era_split_wire_validate_bulk_page_payload(payload, payload_len)) {
            return false;
        }
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
        *kind = ERA_SPLIT_WIRE_PAYLOAD_EEPROM_SYNC;
        return true;
#else
        return false;
#endif
    }

    if ((control & ERA_SPLIT_WIRE_CONTROL_EXT) == 0) {
        if (payload_len != 1) {
            return false;
        }
        *kind = ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK;
        return true;
    }

    if (payload_len < 2) {
        return false;
    }

    /* The request envelope is no longer identified by an exact length and a
       one-bit flags byte, so it classifies inside the class switch like every
       other op rather than ahead of it. Both directions now validate the same
       way: walk byte2's mask, sum the declared bodies, land on payload_len. */
    uint8_t op  = payload[1];
    uint8_t cls = op & 0xF0;
    era_split_wire_section_layout_t layout;
    switch (cls) {
        case 0x10:
            if (!era_split_wire_validate_session_status_payload(payload, payload_len)) {
                return false;
            }
            *kind = ERA_SPLIT_WIRE_PAYLOAD_SESSION_STATUS;
            return true;
        case ERA_SPLIT_WIRE_HOST_PEER_CLASS:
            if (op == ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH) {
                if (!era_split_wire_layout_source_push(payload, payload_len, &layout)) {
                    return false;
                }
                *kind = ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER;
                return true;
            }
            if (op != ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP ||
                !era_split_wire_layout_host_source_rsp(payload, payload_len, &layout)) {
                return false;
            }
            *kind = ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER_HOST_SOURCE_RSP;
            return true;
        case ERA_SPLIT_EEPROM_SYNC_CLASS:
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
            if (era_split_communication_core_storage_validate_wire_payload(payload, payload_len, lane)) {
                *kind = ERA_SPLIT_WIRE_PAYLOAD_EEPROM_SYNC;
                return true;
            }
#endif
            return false;
        default:
            return false;
    }
}

bool era_split_wire_encode_session_status(uint8_t control, const era_split_wire_session_status_t *status, uint8_t *payload, uint8_t *payload_len) {
    if (status == NULL || payload == NULL || payload_len == NULL) {
        return false;
    }
    if (status->accepted_host_open == status->accepted_no_host) {
        return false;
    }
    if (status->matrix_ready && !status->accepted_no_host) {
        return false;
    }

    payload[0] = (uint8_t)(control | ERA_SPLIT_WIRE_CONTROL_EXT);
    payload[1] = 0x10;
    payload[2] = (uint8_t)((status->accepted_host_open ? ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_HOST_OPEN : 0) |
                           (status->accepted_no_host ? ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_NO_HOST : 0) |
                           (status->status_response_requested ? ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_RESPONSE_REQUESTED : 0) |
                           (status->matrix_ready ? ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MATRIX_READY : 0) |
                           (status->bulk_page_supported ? ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_BULK_PAGE : 0));
    era_split_wire_put16(&payload[3], status->usb_epoch);
    era_split_wire_put16(&payload[5], status->host_open_generation);
    era_split_wire_put16(&payload[7], status->host_close_generation);
    *payload_len = 9;
    return true;
}

bool era_split_wire_decode_session_status(const era_split_wire_frame_t *frame, era_split_wire_session_status_t *status) {
    /* The kind test is the validation. Only the two frame decoders produce an
       era_split_wire_frame_t, both reach `kind` solely through
       era_split_wire_classify_payload(), and that function sets
       SESSION_STATUS only after the body has passed
       era_split_wire_validate_session_status_payload() -- so a frame carrying
       this kind has already been walked. The second call that stood here
       re-walked it on the same bytes; what makes deleting it safe is the
       producer set, so a hand-built frame stamped with a kind it did not earn
       would be the change that reopens this. */
    if (frame == NULL || status == NULL || frame->kind != ERA_SPLIT_WIRE_PAYLOAD_SESSION_STATUS) {
        return false;
    }

    uint8_t flags = frame->payload[2];
    status->accepted_host_open        = (flags & ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_HOST_OPEN) != 0;
    status->accepted_no_host          = (flags & ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_NO_HOST) != 0;
    status->status_response_requested = (flags & ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_RESPONSE_REQUESTED) != 0;
    status->matrix_ready              = (flags & ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MATRIX_READY) != 0;
    status->bulk_page_supported       = (flags & ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_BULK_PAGE) != 0;
    status->usb_epoch                 = era_split_wire_get16(&frame->payload[3]);
    status->host_open_generation      = era_split_wire_get16(&frame->payload[5]);
    status->host_close_generation     = era_split_wire_get16(&frame->payload[7]);
    return true;
}
