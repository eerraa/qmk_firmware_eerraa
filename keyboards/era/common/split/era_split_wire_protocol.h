// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "matrix.h"

#define ERA_SPLIT_WIRE_FRAME_MARKER 0xA0
#define ERA_SPLIT_WIRE_FRAME_MARKER_MASK 0xE0
#define ERA_SPLIT_WIRE_FRAME_DIRECTION_BIT 0x10
#define ERA_SPLIT_WIRE_FRAME_LENGTH_MASK 0x0F

#define ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN 15
#define ERA_SPLIT_WIRE_COMPACT_MAX_FRAME_LEN (ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN + 2)
#define ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN
#define ERA_SPLIT_WIRE_MAX_FRAME_LEN ERA_SPLIT_WIRE_COMPACT_MAX_FRAME_LEN
#define ERA_SPLIT_WIRE_BULK_LENGTH_ESCAPE 0
#define ERA_SPLIT_WIRE_BULK_PAGE_MAX_PAYLOAD_LEN 264
#define ERA_SPLIT_WIRE_BULK_PAGE_MAX_FRAME_LEN (1 + 2 + ERA_SPLIT_WIRE_BULK_PAGE_MAX_PAYLOAD_LEN + 4)
/* Route scheduling cadences (bootstrap period, slow status period, refresh
   period, response poll period) are owned by
   scheduler/era_split_transport_scheduler_internal.h, which also carries their
   width assertions. Only wire-framing windows belong here; defining a cadence
   in both headers lets include order pick the winner silently. */
#ifndef ERA_SPLIT_PEER_RESPONSE_WINDOW_MS
#    define ERA_SPLIT_PEER_RESPONSE_WINDOW_MS 20
#endif
#ifndef ERA_SPLIT_SESSION_BOOTSTRAP_RESPONSE_WINDOW_MS
#    define ERA_SPLIT_SESSION_BOOTSTRAP_RESPONSE_WINDOW_MS 2
#endif
#ifndef ERA_SPLIT_WIRE_BULK_PAGE_BODY_TIMEOUT_MS
#    define ERA_SPLIT_WIRE_BULK_PAGE_BODY_TIMEOUT_MS 12
#endif
#define ERA_SPLIT_WIRE_HALF_MATRIX_BITS (MATRIX_ROWS_PER_HAND * MATRIX_COLS)
#define ERA_SPLIT_WIRE_HALF_MATRIX_BYTES ((ERA_SPLIT_WIRE_HALF_MATRIX_BITS + 7) / 8)

#define ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK 0x07
#define ERA_SPLIT_WIRE_CONTROL_ACK_SEQ_MASK 0x38
#define ERA_SPLIT_WIRE_CONTROL_EXT 0x40
#define ERA_SPLIT_WIRE_CONTROL_RESERVED 0x80

/* Multi-byte wire fields are little-endian, and this is the one statement of
   it. The byte order is a property of the protocol rather than of any encoder,
   which is why it is declared beside the framing constants and not in
   era_split_wire_frame.h with the encoders that were its first callers: it had
   a second verbatim copy in the core1 storage codec, 55 call sites against
   era_split_wire_frame.c's 4, and three more hand-expanded -- two in the
   payload decoder and one in the response decoder -- so "the encoders' private
   helper" had already stopped being true. The 82 today are
   `git grep -oh 'era_split_wire_\(get\|put\)\(16\|24\|32\)(' -- keyboards/era ':!*.h'`
   -- the header exclusion is what stops the definitions below from being
   counted as uses of themselves, and the pathspec is spelled that way rather
   than as a `.c` glob because a glob after a slash closes this comment
   (-Werror=comment, which is how that was found). The width alternation has to
   be widened with the accessor set: written as `16\|32` it silently stopped
   counting the 24-bit pair.

   `static inline` here, where era_split_wire_crc8/crc32 above are external, and
   the difference is the body rather than the header's taste. A CRC is a loop
   worth inlining nowhere and its note says so; a four-byte load is worth
   inlining everywhere and already was in both copies, so this move is expected
   to leave the image where it stands. */
static inline uint16_t era_split_wire_get16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static inline uint32_t era_split_wire_get32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The 24-bit pair exists for one field, the CHUNK_REQ arm's truncated image
   CRC, and it earns its place by naming the format instead of leaving both
   sides to reconstruct it from shift counts. It sits beside the 32-bit pair
   because that arm's neighbouring fields use it, and a reader comparing the
   two arms should be comparing widths rather than idioms. The top byte of the
   value is dropped on the way out and reads back zero. */
static inline uint32_t era_split_wire_get24(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16);
}

static inline void era_split_wire_put16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static inline void era_split_wire_put24(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
}

static inline void era_split_wire_put32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

typedef enum {
    ERA_SPLIT_WIRE_DIRECTION_PRIMARY_TO_SECONDARY = 0,
    ERA_SPLIT_WIRE_DIRECTION_SECONDARY_TO_PRIMARY = 1,
} era_split_wire_direction_t;

typedef enum {
    ERA_SPLIT_WIRE_FRAME_LANE_COMPACT = 0,
    ERA_SPLIT_WIRE_FRAME_LANE_BULK_PAGE,
} era_split_wire_frame_lane_t;

typedef enum {
    ERA_SPLIT_WIRE_PAYLOAD_INVALID = 0,
    ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK = 1,
    ERA_SPLIT_WIRE_PAYLOAD_SESSION_STATUS = 2,
    ERA_SPLIT_WIRE_PAYLOAD_EEPROM_SYNC = 3,
    /* 4 was ERROR_NACK, class 0x40. No encoder in this tree ever wrote that
       class -- every op byte written anywhere in the split layer is 0x10, 0x20,
       0x21 or 0xE0..0xEF -- and nothing ever switched on the kind, so the
       classifier's arm admitted a frame nothing could send into a value nothing
       could read. It falls to the `default` arm now and is refused like any
       unmatched class. Unlike the 0x6x removal this was not a no-op: that arm
       returned `true`, so the deletion narrows what the receiver admits rather
       than tidying control flow that already rejected. The value stays retired
       for the same reason 6 does. */
    ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER = 5,
    /* 6 was DUAL_HOST; the value stays retired so the diagnostic `rsp` kind
       numbering in captured logs keeps its meaning. */
    ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER_HOST_SOURCE_RSP = 7,
} era_split_wire_payload_kind_t;

#define ERA_SPLIT_WIRE_HOST_PEER_CLASS 0x20
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH 0x20
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP 0x21

/* byte2 is a plain 8-bit section mask in both directions, bodies follow it in
   ascending marker-bit order, and one validator per direction walks the mask
   summing declared body lengths and must land exactly on payload_len. The two
   id spaces are separate on purpose: the directions carry different data, and
   unifying them would spend bits to buy a symmetry no consumer reads. */
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MATRIX 0x01
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER 0x02
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY 0x04
/* The push id space's first non-matrix payload marker (Slice 12/R5), on the
   INPUT section's both-directions precedent: the RGB defect is symmetric --
   either half's VIA edit must reach the other -- and the response cell alone
   carries only the responder's changes. 0x08 has never been assigned in this
   direction, so no captured mask is ambiguous between eras. The body is the
   response direction's RGB-state body unchanged; in DUAL-HOST the sleep bit is
   zero at capture and skipped at apply (era_wire_contract.md). */
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE 0x08
/* The initiator's tap-hold activity (FA-2 S2), the push id space's next
   unassigned bit. The body is the response direction's, byte for byte, and the
   section is DUAL-HOST-only in both id spaces: HOST-PEER's PEER never resolves
   keycodes, so its HOST's tapping engine already sees every key as a local
   event and has nothing to learn from a counter image of the same facts. */
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY 0x10
/* The DUAL-HOST visual-baseline twin (Slice 14, owner record 2026-08-09):
   the push id space's next unassigned bit, so no captured mask is ambiguous.
   The body is the response direction's visual-resync body at its full fixed
   width -- reason byte plus packed pressed baseline -- because a push
   capture always has local rows. This used to add "the reason-only variant
   stays the response direction's"; that variant is retired, so the two id
   spaces carry one body of one width. Render state whose content is pressed
   positions: consumed by the receiver's hit tracker only, it writes no matrix
   state and reaches no action processing (era_invariants.md's render-state
   clause). */
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL 0x20
/* The initiator's storage-pending fact (2026-08-14 indicator redesign): one
   bit saying the storage relation still holds unfinished pair work — settled
   local divergence, decided cells, or a content-moving episode not yet closed.
   It exists because the responder structurally cannot know that fact: every
   queue, cell and completion poll is initiator-side state, and the fixed
   trailing bridge the EEPROM SYNC lamp used to stand in for it was a time
   substitute for a wire fact no value of which could be right
   (era_host_peer_storage_contract.md, Diagnostics). Open in the push cell of
   BOTH serviced relations — the initiator is the PEER in HOST-PEER and the
   Left in DUAL-HOST — and never in the response direction, whose mask is
   full and whose responder-side facts the initiator already derives from the
   summary. Latest-state and edge-armed on the INPUT-class discipline: the
   receiver holds the value applied (the lamp's mirror arm), so it is retired
   through the apply path — an invalid sent shadow forces the current value
   across once per relation, zero included. 0x40 has never been assigned in
   this direction, so no captured mask is ambiguous between eras. */
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING 0x40
/* The restart arm: the initiator's half of the two-phase agreement that resets
   both halves of a pair at one instant. One marker, never assigned in the push
   id space -- the same reading of the marker rule STORAGE_PENDING took for 0x40
   while the response direction already used that value for something else.

   **The section is the mechanism and not the feature.** It carried the link
   switch alone when it opened and now carries any agreed act; the body says
   which. That generality is what keeps the id space from having to find a
   second marker for the second thing two halves have to reset together, and
   there is no second marker to find.

   Open in the push cell of BOTH serviced relations and in neither response
   cell, and both halves of that are structural. Push is the initiator's
   direction and the initiator is what owns the deadline; the response mask has
   no free marker at all, so the responder's half of the handshake rides the
   AUTHORITY flags byte instead (below).

   It is the yielding class's first claimant, ahead of ACTIVITY: five bytes do
   not fit beside the never-deferring core in either relation, and a deferral
   costs one poll against a commit window of hundreds of milliseconds. What it
   may never do is fail to drain, and the assert family below says it always
   fits beside the one-byte facts. */
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM 0x80
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MASK (ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MATRIX | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM)
/* Which of the push id space a *core0-queued* request may carry, as opposed to
   one core1 composes for its own standing exchange (D3). The two produce the
   same envelope, so the responder can only tell them apart by content, and
   this is the content: core1's standing service composes every eligible push
   section *except* MATRIX, of which it has no arm at all
   (era_split_communication_core_standing.c), while core0's one remaining push
   enqueuer sets exactly this
   (era_split_transport_scheduler_routes.c -- the heartbeat and AUTHORITY
   enqueuers retired with their routes, so the matrix is what is left). Stated
   as the property and not as a list on purpose: the list was written at five
   arms, went stale at six when STORAGE_PENDING opened, and the discriminator
   never turned on which five they were.

   Named rather than spelled `SECTION_MATRIX` at each site because three places
   must agree for the discriminator to mean anything: the enqueuer that sets
   it, the request validator that admits it, and the responder that decides
   from it whether the answer may carry a response section. Spelled out, the
   three drift silently; named, a fourth section on core0's lane fails the
   validator rather than quietly re-opening the second carrier. */
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_CORE0_LANE_SECTIONS ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MATRIX
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_MATRIX_BYTES ERA_SPLIT_WIRE_HALF_MATRIX_BYTES

#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER 0x01
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY 0x02
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY 0x04
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE 0x08
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC 0x10
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE 0x20
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS 0x40
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_TIME_ANCHOR 0x80
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_MASK (ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_TIME_ANCHOR)
/* Bit 0x02 is **reused** (FA-2 S2, owner decision 2026-08-05): it now carries
   the responder's tap-hold activity body. It was the Slice 11 ACTIVITY marker,
   a body-less in-window "ask me" under an activity-gated cadence, retired at
   Slice 11.5 when the relation began polling unconditionally; between 11.5 and
   FA-2 it was unassigned and the mask excluded it as a reserved bit.

   The reuse's capture ambiguity is accepted and practically empty
   (era_wire_contract.md): Slice 11 closed with its device gates unrun, so the era
   in which `sec=0x02` meant the bare ask-me put almost no capture into the
   record, and the image-identity rule every reading already carries dates any
   capture that surfaces. The 16-bit-mask alternative was rejected as capacity
   rather than completeness -- it charges every section-carrying response frame
   a header byte forever to buy spare bits nothing scheduled needs. The
   capture-era boundary is recorded in era_capture_reading.md's `sec=` rules.

   Slice 11.6 took 0x04 rather than 0x02 for AUTHORITY, and the choice is the
   0x20 flags-byte precedent rather than tidiness: at that time re-taking 0x02
   would have made a `sec=` value from a Slice 11 capture ambiguous between two
   eras while a never-assigned bit was free. */

/* The HOST-known lock value is a one-byte body since Slice 11, not
   byte2[2:0]. It costs one byte on frames that carry lock and buys three
   section bits plus the deletion of the encoder's marker/value merge. */
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_BYTES 1
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_VALUE_MASK 0x07
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_BYTES 1
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_MAX_BASELINE_BYTES (ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN - 3 - ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_BYTES)
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES ERA_SPLIT_WIRE_HALF_MATRIX_BYTES
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_FULL_BYTES (ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_BYTES + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES)
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES 7
/* One body, two id spaces (Slice 12/R5): the push direction's RGB-state body
   is the response direction's, byte for byte. */
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RGB_STATE_BYTES ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES
/* Same rule for the visual baseline (Slice 14), at the full fixed width. */
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_VISUAL_BYTES ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_FULL_BYTES
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_BYTES 1
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_VALUE_MASK 0x7F
/* bit7 of the same body byte: the responder's storage-pending fact toward the
   initiator -- the response direction's half of the pair fact the push
   STORAGE_PENDING section carries the other way. It rides here rather than in
   a section of its own because the response mask has no free marker, and the
   bit was reserved-zero-refused from this section's birth, so a capture with
   it set unambiguously dates the image at or after the 2026-08-14
   entry-symmetry change. With it the byte has no reserved bits left. */
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_FLAG_PENDING 0x80
_Static_assert((ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_VALUE_MASK &
                ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_FLAG_PENDING) == 0,
               "the news value bits and the pending flag partition one byte and may not overlap");
_Static_assert((ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_VALUE_MASK |
                ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_FLAG_PENDING) == 0xFF,
               "every bit of the news byte is assigned; the next fact needs its own home, not a hidden bit");
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_TIME_ANCHOR_BYTES 4
/* Storage-pending body: bit0 is the fact, bits 1..7 reserved zero and
   validator-refused like every reserved bit on this envelope pair. */
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_BYTES 1
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_FLAG_PENDING 0x01
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_FLAG_MASK ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_FLAG_PENDING

/* Restart arm body: one flags byte and the commit deadline.

     body byte0   bits0..1 param, bits2..3 act, bits4..7 reserved zero
     body byte1..4 T_commit, sync-timer milliseconds, little-endian

   **The act absorbs the arm bit.** An act of zero is the idle form, so the
   width did not move when the section stopped being one feature's: there is no
   separate "armed" flag to spend a bit on, because advertising an act *is* the
   arm. The acts are era_split_restart_agreement.h's, and 3 is refused by the
   validator rather than reserved, so a captured 3 is a malformed frame and
   never an act from an era this image does not know.

   The deadline is absolute rather than a countdown, and that is the reason the
   section costs four bytes instead of one. A latest-state section can cross
   again -- a forced refresh, a reopen -- and an absolute deadline is idempotent
   under re-delivery where a countdown restarts itself on every arrival. It is
   the same encoding the time-anchor section carries, on the same clock.

   The idle form is one canonical all-zero body: with the act zero the validator
   requires the param and the deadline to be zero too, so a section whose live
   form carries a timestamp still has a reserved-zero rule to enforce. */
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ARM_BYTES 5
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_PARAM_MASK 0x03
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_SHIFT 2
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_MASK 0x0C
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_FLAG_MASK (ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_PARAM_MASK | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_MASK)
/* The widest value either two-bit field can hold. Both carriers give the act
   and the param two bits each, so this bounds both, and era_split_restart_agreement.h
   asserts its act set against it. */
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_VALUE_MAX 3

/* The INPUT layer body is one byte in both directions because this board's
   layer count selects LAYER_STATE_8BIT. The loud gate for that is a
   _Static_assert against sizeof(layer_state_t) in era_split_peer_layer.c,
   which is the unit that would truncate and the only one that needs
   action_layer.h. Do not "fix" this by including action_layer.h here: that
   header defaults DYNAMIC_KEYMAP_LAYER_COUNT itself rather than taking it
   from config, so including it would drag the whole QMK action layer into
   every core1 translation unit to reach one constant. */
#define ERA_SPLIT_WIRE_INPUT_LAYER_BYTES 1

/* The AUTHORITY section: this half's session facts, carried on the relation's
   own lane in both directions since Slice 11.6. It is the same fact set
   SESSION_STATUS carries, minus the one field that is not role revalidation --
   `bulk_page_supported`, which is a compile-time capability and cannot change
   inside a session (era_authority_contract.md).

   Since Slice 11.7 that is the whole difference. The storage-changed hint used
   to be a second exception, pinned to SESSION_STATUS; it now rides
   STORAGE_NEWS section on this same lane in DUAL-HOST, which is what
   left *that* relation with no core0-originated periodic frame at all.

   **R2 (2026-08-04) put HOST-PEER in exactly that position too, and this
   paragraph used to say the opposite emphatically.** It read "HOST-PEER is not
   in that position and this comment must not be read as saying it is: its
   matrix heartbeat is core0's and periodic, and it is that relation's whole
   liveness carrier", and while it stood it was true -- that relation's liveness
   really was a 20 ms core0 response poll beside a 50 ms core0 heartbeat. R2
   retired both routes with their due predicates and their deadlines
   (era_split_transport_scheduler_routes.c,
   era_split_transport_scheduler_timing.c: core0 no longer wakes on either
   period), so neither serviced relation's liveness is core0's any more. Each
   relation's steady state is core1's standing exchange, and what holds the wire
   through a quiet window -- including a durable apply, which in HOST-PEER is
   the hole the grant exists to close -- is ERA_SPLIT_STANDING_LIVENESS_MS
   (era_route_contract.md). The sentence that existed to stop the two relations
   being read together now exists to record that they converged.

   Reading that convergence too far is the opposite error. HOST-PEER's PEER
   keeps one core0-selected route, HOST_PEER_MATRIX_SOURCE_PUSH -- event-driven,
   with no cadence at all -- and the two relations still run different section
   sets on the same envelope. One runtime architecture is a claim about when an
   exchange runs and which core decides it, not about what crosses.

   Which heartbeat noun R2 took also matters here. The *enqueuer* went with its
   routes and the initiator HEARTBEAT *lane* enumerator went with it
   (era_split_communication_core_initiator.h). Live initiator lanes are
   INVALID, SESSION_STATUS, SOURCE_PUSH; those lanes keep per-lane
   ok/miss/bad/fail. Standing HEARTBEAT and the responder HEARTBEAT result
   kind remain load-bearing (era_split_communication_core_standing.c,
   era_split_communication_core_responder.h). Wire-diag `hb=` is printed from
   ERA_SPLIT_TRANSACTION_TIMING_BUCKET_HEARTBEAT_ACK in
   era_split_wire_diagnostics.c, not from a retired initiator lane.

   A third noun used to be listed here and no longer is: this paragraph said
   ERA_SPLIT_COMMUNICATION_CORE_HEARTBEAT_ENABLE separately keyed
   ERA_SPLIT_COMMUNICATION_CORE_STATE_TIMEOUT_US to 30000 against 10000, so
   dropping the macro would cut core1's state handshake to a third. That
   branch had already been flattened when the sentence was checked on
   2026-08-11 -- the timeout is an unconditional #ifndef/30000U default in
   era_split_communication_core_lifecycle_rp2040.c -- and the macro was retired
   with the other seven. The warning is kept in its corrected form because it
   is the shape of the mistake: a macro is safe to retire when nothing reads
   it, and the check for that is a grep of the tree, not a comment.

   Latest-state and edge-armed like every other section, and that is the single
   most reversible mistake in this lane rather than a style note: a level
   carried in every reply publishes to core0 at the poll rate and undoes Slice
   11.5's responder fix, which no build, gate or assert can see. Both sides
   therefore hold a shadow -- the sender advertises only while the live record
   differs from what the wire last confirmed, and the receiver reports to core0
   only when the decoded record differs from the last one it delivered.

   Body, identical in both directions:

     byte0      flags: bit0 accepted_host_open, bit1 accepted_no_host,
                bit2 matrix_ready, bits3..4 restart param, bits5..6 restart act,
                bit7 restart armed
     byte1..2   usb_epoch, little-endian
     byte3..4   host_open_generation, little-endian
     byte5..6   host_close_generation, little-endian

   The three generations are change detection and nothing else: no consumer
   reads their value, and what they buy is that a close-and-reopen inside one
   poll period is still visible in a latest-state section.

   Bits 3..7 are this half's restart intent, and they are here because there was
   nowhere else. The response section mask has all eight markers assigned, so
   the responder's half of the agreement has no section of its own to take --
   the situation STORAGE_NEWS bit7 answered by riding an existing body, and that
   byte has no bits left either. AUTHORITY is what remains, and it is also what
   fits: it is the one section eligible in every serviced relation and both
   directions, it never defers, no sync policy bit gates it, and both ends
   already hold a shadow so the intent gets change delivery for free.

   `armed` distinguishes the two phases and the act and param are shared between
   them, because at most one is ever live: arming consumes the request. With
   `armed` clear the fields say what this half is asking for; with it set they
   say what this half is holding a commit deadline for. The armed form echoes
   what it armed for rather than blanking the fields, and the echo is what lets
   the initiator confirm that the two halves armed for the same thing -- it
   costs nothing, because the fields exist in this byte either way.

   **This byte has no reserved bits left.** Bit 7 was the last one and the
   agreement's arm took it; the next fact on this section needs its own home,
   not a hidden bit. The three alternatives that would buy one, and what each
   costs, are recorded in era_wire_contract.md so they are not re-proposed.

   **The flag mask therefore differs from SESSION_STATUS's, and it always
   did** -- that frame carries bulk_page_supported at 0x80 and this section does
   not. What is shared literally is the *rules* the two validators apply to the
   facts they share: exactly one role bit, and matrix_ready only on a no-host
   half. The reserved-bits-zero rule used to be a third; it is now SESSION_STATUS's
   alone, because this byte has none. Bits 3..7 are a fact SESSION_STATUS does
   not carry, so they add local rules rather than fork shared ones. */
#define ERA_SPLIT_WIRE_AUTHORITY_BYTES 7
#define ERA_SPLIT_WIRE_AUTHORITY_FLAG_HOST_OPEN 0x01
#define ERA_SPLIT_WIRE_AUTHORITY_FLAG_NO_HOST 0x02
#define ERA_SPLIT_WIRE_AUTHORITY_FLAG_MATRIX_READY 0x04
#define ERA_SPLIT_WIRE_AUTHORITY_RESTART_PARAM_SHIFT 3
#define ERA_SPLIT_WIRE_AUTHORITY_RESTART_PARAM_MASK 0x18
#define ERA_SPLIT_WIRE_AUTHORITY_RESTART_ACT_SHIFT 5
#define ERA_SPLIT_WIRE_AUTHORITY_RESTART_ACT_MASK 0x60
#define ERA_SPLIT_WIRE_AUTHORITY_FLAG_RESTART_ARMED 0x80
#define ERA_SPLIT_WIRE_AUTHORITY_FLAG_MASK                                                                           (ERA_SPLIT_WIRE_AUTHORITY_FLAG_HOST_OPEN | ERA_SPLIT_WIRE_AUTHORITY_FLAG_NO_HOST |                                ERA_SPLIT_WIRE_AUTHORITY_FLAG_MATRIX_READY | ERA_SPLIT_WIRE_AUTHORITY_RESTART_PARAM_MASK |                       ERA_SPLIT_WIRE_AUTHORITY_RESTART_ACT_MASK | ERA_SPLIT_WIRE_AUTHORITY_FLAG_RESTART_ARMED)
#define ERA_SPLIT_WIRE_AUTHORITY_FLAG_ROLE_MASK (ERA_SPLIT_WIRE_AUTHORITY_FLAG_HOST_OPEN | ERA_SPLIT_WIRE_AUTHORITY_FLAG_NO_HOST)
_Static_assert((ERA_SPLIT_WIRE_AUTHORITY_RESTART_PARAM_MASK >> ERA_SPLIT_WIRE_AUTHORITY_RESTART_PARAM_SHIFT) == ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_PARAM_MASK,
               "the restart param is one field with two homes; the two body layouts must give it the same width");
_Static_assert((ERA_SPLIT_WIRE_AUTHORITY_RESTART_ACT_MASK >> ERA_SPLIT_WIRE_AUTHORITY_RESTART_ACT_SHIFT) ==
                   (ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_MASK >> ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_SHIFT),
               "the restart act is one field with two homes; the two body layouts must give it the same width");
_Static_assert(ERA_SPLIT_WIRE_AUTHORITY_FLAG_MASK == 0xFF,
               "every bit of the authority flags byte is assigned; the next fact needs its own home, not a hidden bit");

typedef struct {
    bool     accepted_host_open;
    bool     accepted_no_host;
    bool     matrix_ready;
    /* The restart intent, above. It rides this record because it rides this
       section, and it is deliberately NOT part of the session facts
       era_split_scheduler_session caches: a restart intent is not a session
       edge, and folding it into that record would raise a peer-session change
       for something the session layer has no opinion about. What does have to
       see it is era_split_wire_authority_equal(), which is both the sender's
       shadow and the receiver's edge -- so the intent crosses and is delivered
       like any other change of this section. */
    uint8_t  restart_act;
    uint8_t  restart_param;
    bool     restart_armed;
    uint16_t usb_epoch;
    uint16_t host_open_generation;
    uint16_t host_close_generation;
} era_split_wire_authority_section_t;

/* The ACTIVITY section (FA-2 S2): the sending half's tap-hold judgment window
   and its key-input activity, folded into state so the judgment a single
   keyboard makes from events can be made across two USB devices whose events
   never cross. One body, two id spaces, DUAL-HOST only.

   Body, identical in both directions:

     byte0      flags: bit0 window_open, bits1..7 reserved zero
     byte1      press counter, mod 256
     byte2      release counter, mod 256
     byte3..6   last-press sync-timer milliseconds, little-endian
     byte7..10  last-release sync-timer milliseconds, little-endian

   The window flag is the self-gate that replaces a policy bit: it rises only
   while a tap-hold key is in flight whose effective runtime options consume
   other-key input (era_tapping's bridge, all default off), so a keyboard on
   fresh defaults never opens one and the section is never due -- the property
   the silence legs read rather than a claim. The activity fields are advertised
   live only while the *peer's* window flag is up and stay frozen otherwise, so
   ordinary typing with no window open on either half moves no wire byte.

   The counters are change detection and dedup; the timestamps are the
   judgment. A counter delta alone cannot say whether the events it counts
   fell inside the window -- frozen fields un-freeze with a stale image when
   a window opens -- so every consumer orders by the event's own instant on
   the shared clock (R2/R2.1/R6), which is what makes the cross-half decision
   retroactively exact. Both edges carry one: the press instant since FA-2,
   the release instant since the L12 sitting measured the arrival-order
   approximation that stood in for it missing 51 of 53 windows -- on this
   transport a strike's press and release predominantly cross in one image,
   so an ordering keyed to "a later image" starves by construction.
   Latest-state and edge-armed like every section, with one refinement: an
   invalid sent-shadow compares against the all-zero baseline rather than
   forcing a send, so a fresh relation on fresh defaults still crosses
   nothing. */
#define ERA_SPLIT_WIRE_ACTIVITY_BYTES 11
#define ERA_SPLIT_WIRE_ACTIVITY_FLAG_WINDOW_OPEN 0x01
#define ERA_SPLIT_WIRE_ACTIVITY_FLAG_MASK ERA_SPLIT_WIRE_ACTIVITY_FLAG_WINDOW_OPEN

typedef struct {
    bool     window_open;
    uint8_t  press_count;
    uint8_t  release_count;
    uint32_t last_press_ms;
    uint32_t last_release_ms;
} era_split_wire_activity_section_t;

/* Per-relation, per-direction section eligibility. This is the only opener:
   the planner ANDs its computed mask with the entry for this half's relation,
   and admission ANDs the received mask with the same entry, so a section
   absent from the table can be neither sent nor accepted whatever the
   surrounding code does. The gates read the linked table's bytes rather than
   looking for absent symbols, because reusing this envelope pair in a second
   relation makes symbol absence unable to tell an opened section from an
   opened relation (era_performance_gates.md, the ELF gate's eligibility read). */
/* One entry per era_split_mode_t value. Stated here rather than derived from
   the enum so this header stays free of the planner; era_split_wire_payload.c
   carries the _Static_assert that keeps the two in step. */
#define ERA_SPLIT_WIRE_SECTION_ELIGIBILITY_MODES 5
#define ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_PUSH (ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MATRIX | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM)
#define ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_RSP (ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_TIME_ANCHOR)
/* Opened by Slice 11, in the commit that arms writer, reader and eligibility
   together. The INPUT layer section crosses in both directions because the
   defect is symmetric -- a layer held on either half must shift the other.
   Slice 11.5 removed the ACTIVITY marker that stood beside it: an initiator
   that polls unconditionally never needs to be asked for a slot.

   Slice 11.6 added AUTHORITY to all four cells. It is the one section that is
   eligible in every serviced relation, because it is the relation's own
   revalidation rather than a payload the relation happens to carry.

   Slice 11.7 added the storage-news section to the DUAL-HOST response cell,
   which is the last section this relation needed and the reason it needs no
   `SESSION_STATUS` at all once it is up. HOST-PEER has carried that section
   since the protocol existed; DUAL-HOST could not, because until 11.6 its
   response frame had no section envelope to put it in, so the same fact was
   pinned to a SESSION_STATUS flag bit and cost a 50 ms core0 poll to read.
   That poll was the last core0-originated periodic frame in the relation, and
   the one whose refusal by a busy responder marked a live peer stale. One
   carrier for one fact, in both relations, is what removed it.

   Slice 12 (R5) added RGB_STATE to both DUAL-HOST cells -- the existing
   response marker's DUAL-HOST cell and the new push marker -- so a VIA RGB
   change on either half reaches the other without the storage settle.
   HOST-PEER's push cell stays closed and is asserted closed below: that
   relation's PEER is dark, so the HOST's RGB crosses in the response direction
   only. Whether an eligible RGB section carries an effect is the policy
   gate's question, not this table's (era_authority_contract.md: requested &&
   slice-open support, sender's bit gating capture, receiver's bit gating
   apply).

   Slice 13 (R6) added TIME_ANCHOR to the DUAL-HOST response cell, the shared
   clock's second relation: the responder is the time authority in both
   relations, and the initiator's setter applies the one held-time-corrected
   value. The anchor keeps yielding to everything under Slice 12's order, so
   opening it adds no order decision -- only the cell.

   FA-2 (S2) added ACTIVITY to both DUAL-HOST cells -- the reused response
   marker 0x02 and the new push marker 0x10 -- so each half's tap-hold engine
   can judge the other half's key input by state instead of by events that
   cannot cross. Both cells join the yielding class under R5's decided order:
   the section yields to AUTHORITY by the existing projected-length room
   checks and, being judgment data with a live window behind it, claims
   remaining budget ahead of the RGB refresh. HOST-PEER's cells stay closed
   and are asserted closed below.

   The 2026-08-14 indicator redesign added STORAGE_PENDING to the push cell
   of both serviced relations. It is the one fact the responder cannot
   derive: the pair's remaining storage work is initiator-side state, and the
   EEPROM SYNC lamp's fixed trailing bridge existed only to stand in for it.
   One byte, one bit, never-deferring beside the other one-byte facts. The
   response direction stays untouched: its mask is full, and the fact flows
   the other way. */
#define ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_PUSH (ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM)
#define ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_RSP (ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_TIME_ANCHOR)

/* The compile-time complement to the byte read: no entry may name a section
   outside the set the landed slices opened, so an eligibility bit added
   without its contract row fails the build rather than the gate. */
_Static_assert((ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_PUSH & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MATRIX) == 0,
               "DUAL-HOST matrix execution stays closed (era_invariants.md).");
/* Slice 14 opened the visual baseline in DUAL-HOST, both directions (owner
   record 2026-08-09: RGB SYNC targets complete synchronization, behaving as
   HOST-PEER does). The stays-closed assert that stood here retired with the
   opening; what stays closed is the push cell in HOST-PEER, below. */
_Static_assert((ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_PUSH & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL) == 0,
               "HOST-PEER source-push carries no visual baseline: the PEER's hits reach the HOST as projected matrix rows, and the HOST's hits cross in the response direction only.");
_Static_assert((ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_RSP &
                ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER) == 0,
               "HOST-PEER needs no layer sync: its PEER never resolves keycodes, so the HOST's composed rows already carry a PEER-held layer key.");
_Static_assert((ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_PUSH & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER) == 0,
               "HOST-PEER source-push carries no INPUT layer: the PEER never resolves keycodes.");
/* The stays-closed half of the Slice 12 opening: RGB crosses HOST-PEER in the
   response direction only. The PEER is dark -- it renders the HOST's config --
   so a push cell there would be a second carrier for data the response
   already moves, and this assert is what keeps the marker's existence from
   opening it (era_closed_surface_contract.md). */
_Static_assert((ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_PUSH & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE) == 0,
               "HOST-PEER source-push carries no RGB: the HOST's RGB crosses in the response direction only.");
/* The stays-closed half of the FA-2 opening: that relation is one pipeline --
   its PEER never resolves keycodes, so the HOST's tapping engine already sees
   every key of both halves as a local event -- and an ACTIVITY cell there
   would carry a counter image of facts the engine holds first-hand. Asserted
   in both directions, because the marker now exists in both id spaces
   (era_closed_surface_contract.md). */
_Static_assert((ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_PUSH & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY) == 0,
               "HOST-PEER source-push carries no ACTIVITY: the HOST's engine sees every key as a local event already.");
_Static_assert((ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_RSP & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY) == 0,
               "HOST-PEER's response carries no ACTIVITY: the relation is one pipeline and needs none of this.");
#ifndef ERA_SPLIT_TIME_ANCHOR_REFRESH_MS
#    define ERA_SPLIT_TIME_ANCHOR_REFRESH_MS 60000
#endif
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_FLAG_ENABLE 0x01
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_FLAG_SLEEP 0x02
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_FLAG_MASK (ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_FLAG_ENABLE | ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_FLAG_SLEEP)
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_RELATION_OPEN 0x00
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_TX_OVERFLOW 0x01
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_TICK_GAP 0x02
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_RELATION_REOPEN 0x03
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_RENDER_RESET 0x04
#define ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_MAX ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_RENDER_RESET

#ifndef ERA_HOST_PEER_VISUAL_SNAPSHOT_FORCED_REFRESH_MS
#    define ERA_HOST_PEER_VISUAL_SNAPSHOT_FORCED_REFRESH_MS 1000
#endif

/* The storage news section's lost-response cover (D1). The section was named
   for a settled-dirty mask until D2 replaced the fact, and `63137ecca1` moved
   the identifiers to `NEWS` after it; what follows records the form that older
   name belonged to. The section used
   to repeat in every admitted response while nonzero, so a lost response could
   not lose the signal; a sent-state shadow crosses each value once and gives
   that up, and losing this one costs a config edit that then waits for the next
   relation event. The period is the storage dirty-quiet interval rather than a
   render-refresh constant, because that is the rate at which the advertised
   value can legitimately change: a refresh can never outpace a real settle, and
   a lost hint costs at most one more settle interval.

   **Its gate is `value != 0`, and D2 changed what that predicate means without
   changing the gate.** Against D1's level it meant "unconverged" -- the level
   fell to zero at convergence -- so the paragraph that stood here could say a
   converged responder has nothing to re-assert and an idle wire stays exactly
   silent. Against D2's forward-only counter it means "this relation has had at
   least one settled capture", which never becomes false again, so the section
   re-crosses once per period for the life of the relation. The idle property
   that survives is the narrower one the silence legs actually rest on: a
   relation in which nothing has settled advertises nothing, from the sent
   shadow's zero baseline.

   The repeat is bounded and invisible to the legs -- the initiator discards a
   value equal to the one it holds, so nothing is armed; this section has no
   `rt` arm, so runtime silence does not see it; the cost is one responder
   snapshot republish per period. Stopping it once the initiator has
   demonstrably acted would need a device reading this repository does not
   have: the responder cannot observe what the initiator took, so narrowing
   this from source alone trades a bounded repeat for an unbounded lost config
   edit. */
#ifndef ERA_HOST_PEER_STORAGE_NEWS_FORCED_REFRESH_MS
#    define ERA_HOST_PEER_STORAGE_NEWS_FORCED_REFRESH_MS 1000
#endif

/* Compact budget, re-derived under the Slice 12 (R5) deferral order: one rule
   in both relations, in the one shared planner. The one-byte facts (INPUT
   layer, lock, storage mask) and AUTHORITY never defer; the multi-byte
   refreshes (the visual baseline, RGB state) yield to AUTHORITY and to each
   other by remaining budget, through the projected-length checks in
   era_host_peer_responder.c; the anchor keeps yielding to everything. The
   asserts therefore state two kinds of requirement. The never-deferring-core
   asserts say the sections the plan promises never to defer fit one frame in
   each relation and direction. The drain asserts, on the deferred-anchor
   pattern below, say a deferred refresh fits beside the never-deferring
   one-byte facts -- AUTHORITY is edge-armed and retires on confirmation, so a
   deferral drains on the first poll it is absent from, or its deferral never
   drains at all.

   AUTHORITY(7) + RGB(7) is 14 body bytes against 12, so the two never share a
   frame in either direction. That stays a consequence of the order and the
   room checks, deliberately unasserted: an exclusion assert would state the
   opposite of the requirement phrasing every other assert here uses.

   What the Slice 11 lock relocation cost is exactly one combination:
   lock plus a full visual baseline plus the anchor was 15 and is now 16, so
   the anchor defers one response-poll period on a section that refreshes
   every 60 s. */
_Static_assert(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES <=
                   ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_MAX_BASELINE_BYTES,
               "HOST-PEER visual baseline does not fit compact visual snapshot.");
_Static_assert(3 + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_BYTES + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_BYTES <= ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "HOST-PEER lock plus RGB state plus the storage news value does not fit compact source response.");
_Static_assert(3 + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_BYTES + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_FULL_BYTES + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_BYTES <= ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "HOST-PEER lock plus full visual plus the storage news value does not fit compact source response.");
/* A fourth assert stood here for lock plus a *reason-only* visual plus RGB
   state plus the storage mask. That combination is unreachable: the reason-only
   visual form is retired, so a visual section is always the full width and a
   full visual plus RGB is 15 body bytes against 12 -- the two can no longer
   share a frame in either direction, which is the exclusion the comment above
   deliberately leaves unasserted. */
_Static_assert(3 + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_TIME_ANCHOR_BYTES <= ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "The deferred time anchor must at least fit a bare response, or its deferral never drains.");
/* The RGB drain asserts, same pattern (Slice 12): a deferred RGB must fit
   beside the never-deferring one-byte facts in each direction. */
_Static_assert(3 + ERA_SPLIT_WIRE_INPUT_LAYER_BYTES + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_BYTES +
                   ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES <=
                   ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "A deferred RGB state must fit beside the never-deferring one-byte facts in the response direction, or its deferral never drains.");
_Static_assert(3 + ERA_SPLIT_WIRE_INPUT_LAYER_BYTES + ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_BYTES +
                   ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RGB_STATE_BYTES <=
                   ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "A deferred RGB state must fit beside the never-deferring one-byte facts in the push direction, or its deferral never drains.");
/* The ACTIVITY drain asserts, extending the same pattern (FA-2 S2; widened
   by the release stamp). At eleven bytes ACTIVITY no longer fits beside BOTH
   response-direction one-byte facts at once (3 + 1 + 1 + 11 is one over), so
   the asserts state what must still hold -- it fits beside EACH one-byte
   fact -- and the coincidence of the INPUT layer and the storage mask both
   changing on the very poll an ACTIVITY drain is due defers that drain one
   further response poll: the anchor's accepted deferral pattern, one
   millisecond at this relation's cadence, on judgment data whose consumer
   re-consults every tick. ACTIVITY and RGB still drain across consecutive
   polls when both are due at once. */
_Static_assert(3 + ERA_SPLIT_WIRE_INPUT_LAYER_BYTES + ERA_SPLIT_WIRE_ACTIVITY_BYTES <=
                   ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "A deferred ACTIVITY body must fit beside the INPUT layer in each direction, or its deferral never drains.");
_Static_assert(3 + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_BYTES +
                   ERA_SPLIT_WIRE_ACTIVITY_BYTES <=
                   ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "A deferred ACTIVITY body must fit beside the storage news value, or its deferral never drains.");
/* The push direction's twin, at 15 exactly: ACTIVITY fits beside EACH
   push-direction one-byte fact but not both at once (3 + 1 + 1 + 11 is one
   over), so the INPUT layer and the storage-pending bit both changing on the
   very poll an ACTIVITY drain is due defers that drain one further poll —
   the same accepted one-poll deferral the response direction already
   carries. */
_Static_assert(3 + ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_BYTES +
                   ERA_SPLIT_WIRE_ACTIVITY_BYTES <=
                   ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "A deferred ACTIVITY body must fit beside the storage-pending bit in the push direction, or its deferral never drains.");
/* The restart-arm drain asserts. Five bytes do not fit beside the
   never-deferring core in either relation -- DUAL-HOST is 3+1+7+1+5 = 17 and
   HOST-PEER 3+7+1+5 = 16, both against 15 -- so RESTART_ARM is the yielding
   class's first claimant rather than a member of the core, and these say the
   deferral always drains: it fits beside the one-byte facts in both relations,
   and it fits beside AUTHORITY alone at exactly 15, which is the frame a
   handshake actually wants because the arm and the confirmation ride
   together. */
_Static_assert(3 + ERA_SPLIT_WIRE_INPUT_LAYER_BYTES + ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_BYTES +
                   ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ARM_BYTES <=
                   ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "A deferred restart arm must fit beside the never-deferring one-byte facts in the push direction, or its deferral never drains.");
_Static_assert(3 + ERA_SPLIT_WIRE_AUTHORITY_BYTES + ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ARM_BYTES <=
                   ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "The restart arm must share one frame with AUTHORITY, or an arm and the confirmation it waits for cannot cross together.");
/* The visual-baseline drain asserts (Slice 14), same pattern: the full visual
   body fits beside BOTH response-direction one-byte facts and beside the
   push-direction layer byte. Visual mutually defers with the other
   multi-byte refreshes (8+7 and 8+11 both exceed the budget) through the
   existing room checks, in the decided order ACTIVITY > VISUAL > RGB >
   anchor. */
_Static_assert(3 + ERA_SPLIT_WIRE_INPUT_LAYER_BYTES + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_BYTES +
                   ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_FULL_BYTES <=
                   ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "A deferred visual baseline must fit beside the never-deferring one-byte facts in the response direction, or its deferral never drains.");
_Static_assert(3 + ERA_SPLIT_WIRE_INPUT_LAYER_BYTES + ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_BYTES +
                   ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_VISUAL_BYTES <=
                   ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "A deferred visual baseline must fit beside the never-deferring one-byte facts in the push direction, or its deferral never drains.");
/* DUAL-HOST, both directions, with AUTHORITY and the storage mask. This kept
   "the whole eligible set fits, so nothing on this lane defers" until Slice 12
   made the set 19 against 15; the expression survives and the meaning is now
   "the never-deferring core fits, in each direction" -- the accepted priority
   stated as a requirement. RGB is the section that defers when it and
   AUTHORITY fall due on the same poll, which takes an authority edge (a USB
   open or close) landing on the same poll as a due refresh. */
_Static_assert(3 + ERA_SPLIT_WIRE_INPUT_LAYER_BYTES + ERA_SPLIT_WIRE_AUTHORITY_BYTES +
                   ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_BYTES <=
                   ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "The never-deferring DUAL-HOST core must fit one compact frame in each direction.");
/* The push direction's never-deferring core after the storage-pending bit
   joined it (2026-08-14): the DUAL-HOST core is INPUT + AUTHORITY + the
   pending bit at 12 against 15, and the HOST-PEER core is AUTHORITY + the
   pending bit at 11 — the matrix rides its own core0 route and never shares
   a standing frame (the structural separation asserted below). */
_Static_assert(3 + ERA_SPLIT_WIRE_INPUT_LAYER_BYTES + ERA_SPLIT_WIRE_AUTHORITY_BYTES +
                   ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_BYTES <=
                   ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "The never-deferring DUAL-HOST push core must fit one compact frame, or the pending bit's edge can starve.");
_Static_assert(3 + ERA_SPLIT_WIRE_AUTHORITY_BYTES + ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_BYTES <=
                   ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "The never-deferring HOST-PEER push core must fit one compact frame, or the pending bit's edge can starve.");
_Static_assert(3 + ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_MATRIX_BYTES + ERA_SPLIT_WIRE_INPUT_LAYER_BYTES <= ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "The widest source-push section mask does not fit compact budget.");
/* AUTHORITY and MATRIX are both eligible in HOST-PEER and are never planned in
   one frame: on this board they are 7 bytes each and 3 + 7 + 7 is two over the
   compact budget. That is enforced structurally rather than by this comment --
   the matrix rides its own route and AUTHORITY rides RUNTIME_SECTION_PUSH, so
   no planner ever holds both masks -- and the layout walk is the backstop,
   because a mask claiming both sums past any legal payload_len and is refused
   as a malformed frame rather than truncated. */
_Static_assert(3 + ERA_SPLIT_WIRE_AUTHORITY_BYTES <= ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "An authority-only frame must fit the compact budget in both directions, or the relation has no carrier for its own revalidation.");
_Static_assert(3 + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_BYTES + ERA_SPLIT_WIRE_AUTHORITY_BYTES + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_BYTES <= ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "HOST-PEER authority plus lock plus the storage news value does not fit compact source response.");

/* Class 0x6x (DUAL_RUNTIME_BUNDLE) carries no compiled id and the classifier
   rejects it through its `default` arm. Its shape, section masks and body
   sizes are recorded in no active document -- this comment used to say they
   were canonical in era_wire_contract.md, and they never were. Nothing needs
   them: the class is unallocated and nothing reintroduces it. */

/* SESSION_STATUS flags byte.
 *
 *   0x01 accepted_host_open      0x10 status_response_requested
 *   0x02 accepted_no_host        0x20 retired (was dual_host_ready)
 *   0x04 unassigned              0x40 matrix_ready
 *   0x08 unassigned              0x80 bulk_page_supported
 *
 * 0x20 was retired un-reused with the DUAL-HOST parent (Slice 9.5) so old
 * captures showing it set keep their meaning, and Slice 10.5 kept that
 * ban rather than making one captured value ambiguous between two eras.
 *
 * That left 0x04 and 0x08, and they were not the free-and-unvalidated bits
 * the wire contract described: the validator's `flags & 0x0F` mode check
 * forced both to zero. Slice 10.5 took 0x04 for the hint and narrowed that
 * check to the two authority bits it was always about, which makes 0x08
 * genuinely unassigned-and-unvalidated like 0x20. The cost is paid once and
 * is recorded in era_wire_contract.md: a predecessor rejects the whole
 * frame, so this revision's discovery is not backward compatible and both
 * halves must be flashed together - which the storage family already
 * required. A future flag bit costs nothing further.
 *
 * Slice 11 took 0x08 as the runtime activity hint and Slice 11.5 gave it back.
 * The hint's whole job was the cold start: SESSION_STATUS was the only frame
 * running in a quiet DUAL-HOST window, so it was the only way a settled
 * responder could ask the initiator to open a slot. A relation that polls
 * unconditionally has no quiet window and no cold start, so the bit has no
 * question left to carry.
 *
 * Slice 11.7 gave 0x04 back on the same reasoning one step further out. The
 * storage-changed hint was here because DUAL-HOST had no response section
 * envelope, and it kept a 50 ms core0 poll alive purely to read one bit off a
 * frame the relation otherwise no longer needed. It now rides the
 * STORAGE_NEWS section, on the same lane HOST-PEER has always
 * carried it on.
 *
 * **What that section carries is no longer a domain mask, and this paragraph
 * used to say it was.** It read "per domain rather than as one bit", which was
 * exactly right at 11.7: the section then held a settled-dirty mask and the
 * gain over the flag bit really was domain identity. D2 (2026-08-10) replaced
 * the fact with a forward-only news value, 1..127 with 0 meaning nothing to
 * claim, stepped once per settled capture whichever domain it was -- because a
 * counter has no fall to express and the domain identity was never load-bearing
 * anyway, the `SYNC_STATUS` summary the news arms re-deriving every domain from
 * both halves' current facts (era_host_peer_storage.c). So what the section
 * buys over the bit is *not* domain identity but a carrier the relation already
 * runs: one lane instead of a core0 poll. A capture whose storage section is
 * read as naming domains is being read against a pre-D2 image.
 *
 * Both returned to unassigned where Slice 10.5's narrowed `flags & 0x03` check
 * left them, and until 2026-08-10 they were unassigned *and unvalidated*: the
 * check never looked at either, so neither taking one nor releasing it moved
 * the wire. They are validated now. What paid for leaving them open was
 * mixed-revision tolerance, and by owner decision 2026-08-10 the two halves
 * always run the identical image, so the only thing the openness still bought
 * was a disagreement with the AUTHORITY section -- the same fact set, on the
 * relation's own lane, whose validator refused every bit it had no fact for
 * from the day Slice 11.6 opened it. A frame one carrier admits and the other
 * refuses is what that rule exists to prevent, so this frame's decoder now
 * refuses any bit outside ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MASK. A future
 * flag bit costs one line in that mask; that is the whole price, and it
 * replaces a tolerance nothing needs. (Since 2026-08-19 the AUTHORITY flags
 * byte has no reserved bit at all, so reserved-zero is this frame's rule
 * alone; what the two carriers still share is the wider form of it, each
 * refusing whatever it has no fact for.)
 *
 * A sentence stood here describing what a predecessor peer that still read 0x04
 * would experience -- it "sees no hint, and asks for the summary its own
 * round-end re-read was always going to ask for". Both halves of it are dead:
 * D2 deleted the round-end re-read outright (the state it detected is
 * unreachable once the carrier only moves forward), and the same owner decision
 * makes what a mismatched peer would do a question this header does not owe an
 * answer to. */
#define ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_HOST_OPEN 0x01
#define ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_NO_HOST 0x02
#define ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_RESPONSE_REQUESTED 0x10
#define ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MATRIX_READY 0x40
#define ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_BULK_PAGE 0x80
#define ERA_SPLIT_WIRE_SESSION_STATUS_AUTHORITY_MASK \
    (ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_HOST_OPEN | ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_NO_HOST)
/* Every bit this revision's encoder can set, and therefore the whole of what
   its decoder admits. The unassigned bits are validated rather than ignored
   for the reason the AUTHORITY section validated its own while it had any: the
   two carry one fact set, so they may not disagree about what a valid one is. */
#define ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MASK                                                  \
    (ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_HOST_OPEN | ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_NO_HOST | \
     ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_RESPONSE_REQUESTED |                                     \
     ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MATRIX_READY | ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_BULK_PAGE)

typedef struct {
    bool     accepted_host_open;
    bool     accepted_no_host;
    bool     status_response_requested;
    bool     matrix_ready;
    bool     bulk_page_supported;
    uint16_t usb_epoch;
    uint16_t host_open_generation;
    uint16_t host_close_generation;
} era_split_wire_session_status_t;

typedef struct {
    era_split_wire_direction_t    direction;
    era_split_wire_frame_lane_t   lane;
    era_split_wire_payload_kind_t kind;
    uint16_t                   payload_len;
    uint8_t                    payload[ERA_SPLIT_WIRE_BULK_PAGE_MAX_PAYLOAD_LEN];
    uint8_t                    tx_seq;
    uint8_t                    ack_seq;
} era_split_wire_frame_t;

typedef era_split_wire_frame_t era_split_wire_bulk_page_frame_t;
