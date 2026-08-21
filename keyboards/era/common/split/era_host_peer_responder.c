// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_host_peer_transaction.h"

#include "era_split_wire_payload.h"
#include "era_host_peer_storage.h"

/* The wire header states in prose that the forced-refresh period IS the
   storage dirty-quiet interval rather than a render-refresh constant, and
   then writes the number out again because the two headers do not include
   each other. This is the only site that reaches both, so the assertion
   lives here -- the same answer era_split_communication_core_storage.c
   already gives for the compact payload width. */
_Static_assert(ERA_HOST_PEER_STORAGE_NEWS_FORCED_REFRESH_MS == ERA_HOST_PEER_STORAGE_DIRTY_QUIET_MS, "The storage news forced refresh period is the dirty-quiet interval; the two spellings have drifted apart.");

#include "atomic_util.h"
/* For the anchor's send-side stamp (R6): both cores read this counter and only
   differences of it are used, so its origin never has to be reconciled with
   the ChibiOS millisecond timer. */
#include "hardware/structs/timer.h"
#include "sync_timer.h"
#include "timer.h"

static bool     g_era_host_peer_transaction_responder_lock_state_sent_valid;
static uint8_t  g_era_host_peer_transaction_responder_lock_state_bits_sent;
static bool     g_era_host_peer_transaction_responder_visual_sent_valid;
static uint8_t  g_era_host_peer_transaction_responder_visual_baseline_sent[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES];
static uint32_t g_era_host_peer_transaction_responder_visual_sent_ms;
static bool     g_era_host_peer_transaction_responder_visual_snapshot_valid;
static uint8_t  g_era_host_peer_transaction_responder_visual_snapshot[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES];
static bool                      g_era_host_peer_transaction_responder_rgb_sent_valid;
static era_host_peer_rgb_state_t g_era_host_peer_transaction_responder_rgb_sent;
static bool                      g_era_host_peer_transaction_responder_rgb_snapshot_valid;
static era_host_peer_rgb_state_t g_era_host_peer_transaction_responder_rgb_snapshot;
static bool                      g_era_host_peer_transaction_responder_time_anchor_sent_valid;
static uint32_t                  g_era_host_peer_transaction_responder_time_anchor_sent_ms;
static bool                      g_era_host_peer_transaction_responder_input_layer_sent_valid;
static uint8_t                   g_era_host_peer_transaction_responder_input_layer_sent;
static bool                              g_era_host_peer_transaction_responder_authority_sent_valid;
static era_split_wire_authority_section_t g_era_host_peer_transaction_responder_authority_sent;
static bool                              g_era_host_peer_transaction_responder_activity_sent_valid;
static era_split_wire_activity_section_t g_era_host_peer_transaction_responder_activity_sent;
/* The storage news section's sent-state (D1), the discipline its eight
   sibling sections already had. The `_ms` stamp is the lost-response cover the
   level-triggered form used to give for free; the constant and why it is the
   dirty-quiet interval are at its definition. */
static bool                              g_era_host_peer_transaction_responder_storage_news_sent_valid;
static uint8_t                           g_era_host_peer_transaction_responder_storage_news_sent;
static uint32_t                          g_era_host_peer_transaction_responder_storage_news_sent_ms;

static bool era_host_peer_transaction_visual_baseline_equal(const uint8_t lhs[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES], const uint8_t rhs[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES]) {
    for (uint8_t index = 0; index < ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES; index++) {
        if (lhs[index] != rhs[index]) {
            return false;
        }
    }
    return true;
}

static bool era_host_peer_transaction_rgb_state_equal(const era_host_peer_rgb_state_t *lhs, const era_host_peer_rgb_state_t *rhs) {
    return lhs != NULL && rhs != NULL &&
           lhs->enabled == rhs->enabled &&
           lhs->sleep == rhs->sleep &&
           lhs->mode == rhs->mode &&
           lhs->hue == rhs->hue &&
           lhs->sat == rhs->sat &&
           lhs->val == rhs->val &&
           lhs->speed == rhs->speed &&
           lhs->flags == rhs->flags;
}

static bool era_host_peer_transaction_visual_baseline_nonzero(const uint8_t baseline[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES]) {
    for (uint8_t index = 0; index < ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES; index++) {
        if (baseline[index] != 0) {
            return true;
        }
    }
    return false;
}

static bool era_host_peer_transaction_capture_visual_snapshot(era_host_peer_visual_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return false;
    }

    bool    valid = false;
    uint8_t baseline[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES];
    ATOMIC_BLOCK_RESTORESTATE {
        valid = g_era_host_peer_transaction_responder_visual_snapshot_valid;
        era_host_peer_transaction_visual_baseline_copy(baseline, g_era_host_peer_transaction_responder_visual_snapshot);
    }
    if (!valid) {
        return false;
    }

    snapshot->reason                 = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_RELATION_OPEN;
    era_host_peer_transaction_visual_baseline_copy(snapshot->pressed_baseline, baseline);
    return true;
}

static bool era_host_peer_transaction_capture_rgb_state(era_host_peer_rgb_state_t *state) {
    if (state == NULL) {
        return false;
    }

    bool valid = false;
    ATOMIC_BLOCK_RESTORESTATE {
        valid  = g_era_host_peer_transaction_responder_rgb_snapshot_valid;
        *state = g_era_host_peer_transaction_responder_rgb_snapshot;
    }
    return valid;
}

void era_host_peer_transaction_force_responder_lock_state_response(void) {
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_host_peer_transaction_responder_lock_state_sent_valid  = false;
        g_era_host_peer_transaction_responder_visual_sent_valid      = false;
        g_era_host_peer_transaction_responder_rgb_sent_valid         = false;
        g_era_host_peer_transaction_responder_time_anchor_sent_valid = false;
        /* A relation flush drops the layer and authority shadows with the rest:
           the peer that reopens has cleared whatever it held, so this half must
           re-send its current values rather than treat an unchanged one as
           already known. */
        g_era_host_peer_transaction_responder_input_layer_sent_valid = false;
        g_era_host_peer_transaction_responder_authority_sent_valid   = false;
        g_era_host_peer_transaction_responder_activity_sent_valid    = false;
    }
}

void era_host_peer_transaction_forget_responder_input_layer(void) {
    /* The responder half of the relation rotation. The peer clears -- or may
       have re-baselined -- what it holds on that event, so a surviving shadow
       would let an unchanged value count as already known and leave the
       reopened peer stale until the user happened to change it.

       RGB joined at Slice 12 (R5), mirroring INPUT/AUTHORITY's rule: a
       reopened peer may be a rebooted half running its own EEPROM's RGB
       config, which the sender cannot distinguish from one that kept the
       applied state. What it adds is one seven-byte section per rotation, in
       HOST-PEER as well, since a latest-state re-send of an unchanged value is
       an idempotent apply on the receiver.

       The anchor joined at Slice 13 (R6), and for it the rotation drop is the
       contract rather than a mirror: the section is advertised until its
       sent-commit drains once per relation open/reopen
       (era_wire_contract.md), and DUAL-HOST's reopen is the rotation -- that
       relation has no flush path to force it. The apply is idempotent (the
       setter recomputes the same clock), so the cost is one four-byte section
       per rotation in either relation.

       Separate from force_responder_lock_state_response() deliberately --
       the lock stays out of the rotation clear. The visual sent-shadow
       joined at Slice 14 so a reopened peer gets one full RELATION_OPEN
       replay instead of staying visually stale while a key is held across
       the reopen; the cost is one eight-byte re-send per rotation, exactly
       when the peer may have rebooted. */
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_host_peer_transaction_responder_input_layer_sent_valid = false;
        g_era_host_peer_transaction_responder_authority_sent_valid   = false;
        g_era_host_peer_transaction_responder_rgb_sent_valid         = false;
        g_era_host_peer_transaction_responder_time_anchor_sent_valid = false;
        /* ACTIVITY joined at FA-2 on INPUT/AUTHORITY's rule. The invalid
           shadow then compares against the all-zero baseline rather than
           forcing a send, so a reopened relation on fresh defaults still
           crosses nothing -- the silence property the reuse was priced on. */
        g_era_host_peer_transaction_responder_activity_sent_valid    = false;
        g_era_host_peer_transaction_responder_visual_sent_valid      = false;
        /* The storage mask joined at D1, and the rotation is the event that
           makes it necessary rather than tidy: the initiator clears core1's
           received cache in the same rotation
           (era_split_communication_core_clear_standing()) and its storage
           engine's relation-open audit forgets the peer's last-taken claim
           (`peer_news_value = 0`, era_host_peer_storage_begin_relation_audit()),
           so a surviving shadow would let this half's current value count as
           already known against a peer that has forgotten it.

           Until D2 that clause read "declares every domain in hand", because
           the hint named domains and what the peer forgot was a per-domain
           in-hand set. D2 deleted that set with the mask it existed to track,
           and the whole of what the peer now remembers about this half's claim
           is one byte -- so the sentence is shorter and the reason it is here
           is unchanged.

           Deliberately not in force_responder_lock_state_response() beside the
           others. That call is a matrix-relation flush and a storage-recovery
           step, neither of which resets the initiator's received cache or its
           record of the peer's claim, so dropping the shadow there would
           re-advertise a value the peer still holds -- harmless but not free,
           on the path a storage episode close already runs. The rotation is
           the only event both halves agree on. */
        g_era_host_peer_transaction_responder_storage_news_sent_valid = false;
    }
}

void era_host_peer_transaction_clear_responder_visual_snapshot(void) {
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_host_peer_transaction_responder_visual_snapshot_valid = false;
    }
}

bool era_host_peer_transaction_publish_responder_visual_snapshot(const uint8_t baseline[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES]) {
    if (baseline == NULL) {
        return false;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        era_host_peer_transaction_visual_baseline_copy(g_era_host_peer_transaction_responder_visual_snapshot, baseline);
        g_era_host_peer_transaction_responder_visual_snapshot_valid = true;
    }
    return true;
}

void era_host_peer_transaction_clear_responder_rgb_state(void) {
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_host_peer_transaction_responder_rgb_snapshot_valid = false;
    }
}

bool era_host_peer_transaction_publish_responder_rgb_state(const era_host_peer_rgb_state_t *state) {
    if (state == NULL) {
        return false;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        g_era_host_peer_transaction_responder_rgb_snapshot       = *state;
        g_era_host_peer_transaction_responder_rgb_snapshot_valid = true;
    }
    return true;
}

/* What the frame this plan describes will actually be, in bytes, counting only
   the sections decided so far. The deferring sections -- the visual baseline,
   RGB state, and the time anchor since Slice 12's order -- ask it before
   adding themselves, so each is measured against a real remaining budget
   rather than a guessed one. Missing a section here is what turns a deferral
   into an encode failure, which the responder reports as no response at all:
   the lock relocation cost the anchor one byte that this used not to count. */
static uint16_t era_host_peer_transaction_responder_projected_len(const era_host_peer_transaction_responder_response_plan_t *plan) {
    uint16_t projected = 3;
    if (plan->send_input_layer) {
        projected = (uint16_t)(projected + ERA_SPLIT_WIRE_INPUT_LAYER_BYTES);
    }
    if (plan->send_authority) {
        projected = (uint16_t)(projected + ERA_SPLIT_WIRE_AUTHORITY_BYTES);
    }
    if (plan->send_activity) {
        projected = (uint16_t)(projected + ERA_SPLIT_WIRE_ACTIVITY_BYTES);
    }
    if (plan->send_lock_state) {
        projected = (uint16_t)(projected + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_BYTES);
    }
    if (plan->send_visual_snapshot) {
        projected = (uint16_t)(projected + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_FULL_BYTES);
    }
    if (plan->send_rgb_state) {
        projected = (uint16_t)(projected + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES);
    }
    if (plan->send_storage_news) {
        projected = (uint16_t)(projected + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_BYTES);
    }
    if (plan->send_time_anchor) {
        projected = (uint16_t)(projected + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_TIME_ANCHOR_BYTES);
    }
    return projected;
}

void era_host_peer_transaction_prepare_responder_response(uint8_t lock_state_bits, uint8_t storage_news, uint8_t input_layer, const era_split_wire_authority_section_t *authority, const era_split_wire_activity_section_t *activity, uint8_t eligible_sections, era_host_peer_transaction_responder_response_plan_t *plan) {
    if (plan == NULL) {
        return;
    }
    lock_state_bits &= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_VALUE_MASK;
    storage_news &= (uint8_t)(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_VALUE_MASK |
                              ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_FLAG_PENDING);

    /* The eligibility clip belongs here and not only at the publish, and the
     * reason is a device measurement rather than tidiness.
     *
     * Every section here is due until the wire confirms it. Clipping only on
     * the way out leaves an ineligible section permanently due, and the time
     * anchor then re-captures a fresh sync_timer_read32() on every call --
     * which makes the responder snapshot differ from the previously published
     * one every single time and turns the publish path into a storm. Measured
     * 2026-08-01 on a DUAL-HOST Right: 338,199 publishes in 371 s, about 911
     * per second, against 366 responses per second.
     *
     * Every latest-state section has that shape, so the guard is per section
     * rather than a special case for the anchor: RGB, the visual baseline and
     * the storage mask each become ineligible in some relation as the later
     * slices land, and each would reproduce this. */
#define ERA_HOST_PEER_RESPONDER_ELIGIBLE(section) ((eligible_sections & (section)) != 0)

    *plan = (era_host_peer_transaction_responder_response_plan_t){
        .lock_state_bits    = lock_state_bits,
        .storage_news = storage_news,
        .input_layer        = input_layer,
    };

    /* The storage news section, latest-state and edge-armed since D1 -- the
     * last section that advertised a repeating level instead of a
     * transition, and the last that used *omission* as a value. It was the
     * settled-dirty mask until D2 replaced the fact and `63137ecca1` moved the
     * identifiers to `news`; that older name is what the paragraphs below are
     * about, and a patch or capture predating them needs it to resolve.
     *
     * What the level cost is the reason this changed. That mask was a level
     * whose fall could not be expressed: the send gate was `!= 0` and the decoder
     * refused a zero body, so "all clear" reached the wire as absence, and R2
     * moved the carrier onto a core1 exchange that reports edges and caches the
     * last value. A cache cannot represent an absence, so the fall never
     * arrived, both halves of a DUAL-HOST pair held a mask that could not come
     * down, and the initiator re-delivered that stale byte to a consumer that
     * queues work -- device-measured 2026-08-09, `match` rising in exact
     * multiples of seven against a contract that asked for three.
     *
     * A truthful sender is what makes the receiver's edge lossless, which is
     * why this is the fix and dropping the edge is not (the receiver's own
     * comment carries the other half). The forced refresh replaces the one
     * thing the repeating level did give -- a lost response could not lose the
     * signal.
     *
     * **Its `!= 0` term meant "unconverged" under D1's level and means "this
     * relation has captured at least once" under D2's forward-only counter**,
     * so the refresh now repeats for the life of the relation rather than
     * stopping at convergence. Since the entry-symmetry change a raised
     * pending flag (bit7) holds the term true as well, so a mid-operation
     * relation that has never settled still refreshes its claim on the same
     * bounded schedule -- which is the loss protection the flag wants. Bounded and invisible to the legs: the
     * initiator discards a value equal to the one it holds, this section has
     * no `rt` arm, and the cost is one snapshot republish per period. The
     * Stopping it once the initiator has demonstrably acted would need a
     * device reading this repository does not have: the responder cannot
     * observe what the initiator took. */
    if (ERA_HOST_PEER_RESPONDER_ELIGIBLE(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS)) {
        bool     storage_sent_valid = false;
        uint8_t  storage_sent       = 0;
        uint32_t storage_sent_ms    = 0;
        ATOMIC_BLOCK_RESTORESTATE {
            storage_sent_valid = g_era_host_peer_transaction_responder_storage_news_sent_valid;
            storage_sent       = g_era_host_peer_transaction_responder_storage_news_sent;
            storage_sent_ms    = g_era_host_peer_transaction_responder_storage_news_sent_ms;
        }
        /* **An invalid shadow forces one send, every value included** — the
           response direction's forced first cross, the exact discipline the
           push STORAGE_PENDING twin has always carried, adopted 2026-08-14
           after its absence stranded a lamp on the first two-domain L-edited
           load. The sentence that stood here argued the opposite: an invalid
           shadow compared against the all-clear, on the reasoning that both
           ends restart a rotation agreeing on zero and the forward-only news
           value never falls, so anything worth saying differed from zero and
           crossed by itself. bit7 broke that reasoning's premise — the byte
           now falls to 0x00 at every operation's close — and the initiator's
           mirror survives rotations precisely on the promise that the live
           carrier re-states itself on the fresh relation. A pure-target
           responder ends an operation with news 0 and pending 0, its
           post-rotation byte equal to the old all-clear baseline, and under
           the compare-against-zero rule nothing ever crossed: the initiator's
           dropped cache never revalidated, its drain never ran again, and
           the mirror stood lit for good (device-caught, `vis=1 pnd=0 mir=1`
           over a healthy converged wire). The cost of the force is one body
           byte once per relation reopen — the same cost the push twin's
           forced cross pays, recorded when that cell opened. */
        uint8_t last_confirmed = storage_sent_valid ? storage_sent : 0;
        /* Ordered so the timer read short-circuits away where it can: this
           runs on the scan-adjacent publish path, and the cases that reach it
           are "the shadow just dropped" (a rotation, rare), "the value
           moved" (already due), and the bounded refresh of a nonzero byte. */
        plan->send_storage_news = !storage_sent_valid ||
                                  last_confirmed != storage_news ||
                                        (storage_news != 0 &&
                                         timer_elapsed32(storage_sent_ms) >= ERA_HOST_PEER_STORAGE_NEWS_FORCED_REFRESH_MS);
    }

    /* Latest-state and edge-armed like every other section: advertised while
       the live value differs from what the wire last confirmed, retired when
       it confirms the new one. That is what makes typing with no layer
       transition produce no frame. */
    if (ERA_HOST_PEER_RESPONDER_ELIGIBLE(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER)) {
        bool    input_layer_sent_valid = false;
        uint8_t input_layer_sent       = 0;
        ATOMIC_BLOCK_RESTORESTATE {
            input_layer_sent_valid = g_era_host_peer_transaction_responder_input_layer_sent_valid;
            input_layer_sent       = g_era_host_peer_transaction_responder_input_layer_sent;
        }
        plan->send_input_layer = !input_layer_sent_valid || input_layer_sent != input_layer;
    }


    /* The shadow reads are per section, and each sits behind its own
       eligibility. This block used to read all four unconditionally -- a
       seven-byte baseline copy and an eight-field RGB copy inside a critical
       section -- so a DUAL-HOST Right paid HOST-PEER's shadows on every build,
       at the poll rate, for values its own eligibility mask discards a line
       later. Nothing here can be stale as a result: a shadow is only ever read
       to answer whether its section is due, and a section that is not eligible
       is never due. */
    bool    lock_state_sent_valid = false;
    uint8_t lock_state_bits_sent  = 0;
    if (ERA_HOST_PEER_RESPONDER_ELIGIBLE(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE)) {
        ATOMIC_BLOCK_RESTORESTATE {
            lock_state_sent_valid = g_era_host_peer_transaction_responder_lock_state_sent_valid;
            lock_state_bits_sent  = g_era_host_peer_transaction_responder_lock_state_bits_sent;
        }
        plan->send_lock_state = !lock_state_sent_valid || lock_state_bits_sent != lock_state_bits;
    }

    /* The responder's authority, on exactly the same edge-armed rule as every
       other section here. This is the direction that has no other carrier: a
       responder cannot initiate, so before Slice 11.6 its USB loss reached the
       peer only when the initiator's next SESSION_STATUS beat happened to ask
       -- or, in HOST-PEER, where that beat was already suppressed, only when
       the wire started failing.

       **It never defers since Slice 12 (R5), and that is the decided order
       rather than a promotion this file made up**: the one-byte facts and
       AUTHORITY form the never-deferring core -- asserted to fit one frame in
       era_split_wire_protocol.h -- and the multi-byte refreshes below yield to
       it and to each other by remaining budget. The order it replaces, where
       authority yielded to the payload sections, was a landing-order artifact:
       Slice 11.6 made the newcomer yield so the accepted sections would not
       re-gate, not because a lighting refresh outranks the relation's only
       carrier of the responder's session facts -- and that inversion binds ten
       times harder in HOST-PEER, whose poll is 10 ms against DUAL-HOST's 1. */
    if (authority != NULL && ERA_HOST_PEER_RESPONDER_ELIGIBLE(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY)) {
        bool                               authority_sent_valid = false;
        era_split_wire_authority_section_t authority_sent       = {0};
        ATOMIC_BLOCK_RESTORESTATE {
            authority_sent_valid = g_era_host_peer_transaction_responder_authority_sent_valid;
            authority_sent       = g_era_host_peer_transaction_responder_authority_sent;
        }
        if (!authority_sent_valid || !era_split_wire_authority_equal(&authority_sent, authority)) {
            plan->authority      = *authority;
            plan->send_authority = true;
        }
    }

    /* The tap-hold activity (FA-2 S2), first of the yielding class: judgment
       data with a live window behind it claims remaining budget ahead of the
       render refreshes below, and yields to AUTHORITY through the same room
       check they use. The edge rule has one refinement, and it is the
       fresh-defaults silence property rather than an optimisation: an invalid
       sent-shadow compares against the all-zero baseline instead of forcing a
       send, so a relation that has never opened a window advertises nothing.
       The zero-baseline is safe to assume on both ends because the rotation
       clears the receiving cache and this shadow together. */
    if (activity != NULL && ERA_HOST_PEER_RESPONDER_ELIGIBLE(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY)) {
        bool                              activity_sent_valid = false;
        era_split_wire_activity_section_t activity_sent       = {0};
        ATOMIC_BLOCK_RESTORESTATE {
            activity_sent_valid = g_era_host_peer_transaction_responder_activity_sent_valid;
            activity_sent       = g_era_host_peer_transaction_responder_activity_sent;
        }
        const era_split_wire_activity_section_t zero_baseline = {0};
        const era_split_wire_activity_section_t *last_confirmed = activity_sent_valid ? &activity_sent : &zero_baseline;
        if (!era_split_wire_activity_equal(last_confirmed, activity) &&
            era_host_peer_transaction_responder_projected_len(plan) + ERA_SPLIT_WIRE_ACTIVITY_BYTES <= ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN) {
            plan->activity      = *activity;
            plan->send_activity = true;
        }
    }

    /* The multi-byte refreshes, each measured against the real remaining
       budget (Slice 12). A refresh that does not fit this poll stays due --
       its sent shadow only advances on a confirmed send -- and drains on the
       next poll AUTHORITY is absent from, which its edge-armed retirement
       guarantees. This is what retired the visual_full_payload special case:
       a full visual leaves no room for RGB by arithmetic, so the generic room
       check subsumes the rule that was written out by hand. Visual is checked
       before RGB, so when both are due and both fit alone, visual wins the
       frame and RGB defers one poll -- the same outcome the special case
       produced. */
    era_host_peer_visual_snapshot_t visual_snapshot;
    if (ERA_HOST_PEER_RESPONDER_ELIGIBLE(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC) &&
        era_host_peer_transaction_capture_visual_snapshot(&visual_snapshot)) {
        bool     visual_sent_valid = false;
        uint8_t  visual_baseline_sent[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES];
        uint32_t visual_sent_ms = 0;
        ATOMIC_BLOCK_RESTORESTATE {
            visual_sent_valid = g_era_host_peer_transaction_responder_visual_sent_valid;
            era_host_peer_transaction_visual_baseline_copy(visual_baseline_sent, g_era_host_peer_transaction_responder_visual_baseline_sent);
            visual_sent_ms = g_era_host_peer_transaction_responder_visual_sent_ms;
        }
        bool baseline_changed = !visual_sent_valid || !era_host_peer_transaction_visual_baseline_equal(visual_snapshot.pressed_baseline, visual_baseline_sent);
        /* The forced refresh runs while a key is held or until the release's
           all-zero baseline has confirmed once (Slice 14): the current-or-sent
           nonzero term keeps the stale-pressed-bit heal -- the transition to
           zero still gets its replay a period later -- and makes a settled
           idle wire exactly silent in both relations, which is the DUAL-HOST
           idle-zero property and, in HOST-PEER, retires only the 1 Hz
           re-send of a baseline the receiver had already zeroed. */
        bool forced_refresh   = visual_sent_valid && timer_elapsed32(visual_sent_ms) >= ERA_HOST_PEER_VISUAL_SNAPSHOT_FORCED_REFRESH_MS &&
                              (era_host_peer_transaction_visual_baseline_nonzero(visual_snapshot.pressed_baseline) ||
                               era_host_peer_transaction_visual_baseline_nonzero(visual_baseline_sent));
        uint16_t visual_bytes = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_FULL_BYTES;
        if ((baseline_changed || forced_refresh) &&
            era_host_peer_transaction_responder_projected_len(plan) + visual_bytes <= ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN) {
            if (!visual_sent_valid) {
                visual_snapshot.reason = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_RELATION_OPEN;
            } else if (baseline_changed) {
                visual_snapshot.reason = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_RENDER_RESET;
            } else {
                visual_snapshot.reason = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_TICK_GAP;
            }
            plan->visual_snapshot      = visual_snapshot;
            plan->send_visual_snapshot = true;
        }
    }

    era_host_peer_rgb_state_t rgb_state;
    if (ERA_HOST_PEER_RESPONDER_ELIGIBLE(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE) &&
        era_host_peer_transaction_capture_rgb_state(&rgb_state)) {
        bool                      rgb_sent_valid = false;
        era_host_peer_rgb_state_t rgb_sent       = {0};
        ATOMIC_BLOCK_RESTORESTATE {
            rgb_sent_valid = g_era_host_peer_transaction_responder_rgb_sent_valid;
            rgb_sent       = g_era_host_peer_transaction_responder_rgb_sent;
        }
        if ((!rgb_sent_valid || !era_host_peer_transaction_rgb_state_equal(&rgb_state, &rgb_sent)) &&
            era_host_peer_transaction_responder_projected_len(plan) + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES <= ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN) {
            plan->rgb_state      = rgb_state;
            plan->send_rgb_state = true;
        }
    }

    if (ERA_HOST_PEER_RESPONDER_ELIGIBLE(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_TIME_ANCHOR)) {
        bool     time_anchor_sent_valid = false;
        uint32_t time_anchor_sent_ms    = 0;
        ATOMIC_BLOCK_RESTORESTATE {
            time_anchor_sent_valid = g_era_host_peer_transaction_responder_time_anchor_sent_valid;
            time_anchor_sent_ms    = g_era_host_peer_transaction_responder_time_anchor_sent_ms;
        }
        if (!time_anchor_sent_valid || timer_elapsed32(time_anchor_sent_ms) >= ERA_SPLIT_TIME_ANCHOR_REFRESH_MS) {
            // The anchor value is captured fresh on every cold snapshot publish
            // while due. It rides any slot whose remaining compact budget fits
            // its four bytes — on seven-byte half-matrix boards that includes
            // the full visual baseline exactly — and defers otherwise.
            if (era_host_peer_transaction_responder_projected_len(plan) + ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_TIME_ANCHOR_BYTES <= ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN) {
                plan->send_time_anchor = true;
                /* The reading and its stamp are one capture (R6): core1 adds
                   the elapsed between this instant and the encode, so the
                   snapshot serving a poll a period late no longer ages the
                   anchor by that period. */
                plan->time_anchor_ms       = sync_timer_read32();
                plan->time_anchor_stamp_us = timer_hw->timerawl;
            }
        }
    }

#undef ERA_HOST_PEER_RESPONDER_ELIGIBLE
}

void era_host_peer_transaction_commit_responder_response(const era_host_peer_transaction_responder_response_plan_t *plan, const era_host_peer_transaction_responder_response_t *response) {
    if (plan == NULL || response == NULL || response->result != ERA_SPLIT_TRANSACTION_RESULT_OK) {
        return;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        if (response->lock_state_sent) {
            g_era_host_peer_transaction_responder_lock_state_bits_sent  = plan->lock_state_bits;
            g_era_host_peer_transaction_responder_lock_state_sent_valid = true;
        }
        if (response->visual_snapshot_sent) {
            era_host_peer_transaction_visual_baseline_copy(g_era_host_peer_transaction_responder_visual_baseline_sent, plan->visual_snapshot.pressed_baseline);
            g_era_host_peer_transaction_responder_visual_sent_valid = true;
            g_era_host_peer_transaction_responder_visual_sent_ms    = timer_read32();
        }
        if (response->rgb_state_sent) {
            g_era_host_peer_transaction_responder_rgb_sent       = plan->rgb_state;
            g_era_host_peer_transaction_responder_rgb_sent_valid = true;
        }
        if (response->storage_news_sent) {
            g_era_host_peer_transaction_responder_storage_news_sent       = plan->storage_news;
            g_era_host_peer_transaction_responder_storage_news_sent_valid = true;
            g_era_host_peer_transaction_responder_storage_news_sent_ms    = timer_read32();
        }
        if (response->time_anchor_sent) {
            g_era_host_peer_transaction_responder_time_anchor_sent_valid = true;
            g_era_host_peer_transaction_responder_time_anchor_sent_ms    = timer_read32();
        }
        if (response->input_layer_sent) {
            g_era_host_peer_transaction_responder_input_layer_sent       = plan->input_layer;
            g_era_host_peer_transaction_responder_input_layer_sent_valid = true;
        }
        if (response->authority_sent) {
            g_era_host_peer_transaction_responder_authority_sent       = plan->authority;
            g_era_host_peer_transaction_responder_authority_sent_valid = true;
        }
        if (response->activity_sent) {
            g_era_host_peer_transaction_responder_activity_sent       = plan->activity;
            g_era_host_peer_transaction_responder_activity_sent_valid = true;
        }
    }
}
