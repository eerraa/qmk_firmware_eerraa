// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_transport_scheduler.h"

#include <stdint.h>
#include <string.h>

#include "action_layer.h"
#include "atomic_util.h"
#include "../system/era_matrix_engine.h"
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "communication_core/era_split_communication_core_storage.h"
#    include "era_host_peer_storage.h"
#endif
#include "era_host_peer_source_snapshot.h"
#include "era_host_peer_transaction.h"
#include "era_split_authority_reducer.h"
#include "era_split_peer_layer.h"
#include "era_split_link.h"
#include "era_split_restart_agreement.h"
#include "era_split_scheduler_events.h"
#include "era_split_scheduler_session.h"
/* The Core1 transaction-orchestration API is deliberately absent: this unit
   names nothing in it, and core0 has no wire entry point of its own
   (era_source_map.md). */
#include "diagnostics/era_split_transport_scheduler_role_diagnostics.h"
#include "era_split_sync_policy.h"
#include "era_split_wire_payload.h"
#include "era_split_wire_router.h"
#include "era_split_tap_activity.h"
#include "communication_core/era_split_communication_core_diagnostics.h"
/* For era_split_communication_core_initiator_result_ready(): the housekeeping
   due gate reads a published result rather than a pending request since Slice
   11.6. */
#include "communication_core/era_split_communication_core_initiator.h"
#include "communication_core/era_split_communication_core_lifecycle.h"
#include "communication_core/era_split_communication_core_owner.h"
#include "communication_core/era_split_communication_core_responder.h"
#include "communication_core/era_split_communication_core_standing.h"
#include "era_split_matrix_frame.h"
#include "hardware/structs/timer.h"
#include "scheduler/era_split_transport_scheduler_internal.h"
#include "scheduler/era_split_transport_scheduler_routes.h"
#include "scheduler/era_split_transport_scheduler_timing.h"
#include "sync_timer.h"
#include "timer.h"

/* Bits 5 and 6 of the dirty word whose bits 0-4 belong to
   `era_split_scheduler_dirty_flags_t` (`era_split_scheduler_events.h`). They
   live here because no producer outside this unit raises them, but the bit
   space is one: a new bit is chosen against both lists, and a retired bit
   stays unassigned rather than being recycled, the same rule the route-due
   word beside that header enum states for itself. Their numeric values are
   read off a capture -- they are the top two bits of `dirty=` on
   `wire sched` -- so they may be moved but never renumbered. */
enum {
    ERA_SPLIT_SCHEDULER_DIRTY_MODE      = 1U << 5,
    ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE = 1U << 6,
};

/* The commit deadline has to sit far enough past the arm timeout for the retire
   to reach a responder that armed on an answer nobody received. It is held
   against the slower relation's poll period, which is the one that makes the
   crossing take longest -- the levels are a compile-time set, so the slowest
   case is a constant and not a runtime check.

   It is asserted **here** and not beside the constants, because the poll period
   belongs to this unit: the link lane once reached into
   scheduler/era_split_transport_scheduler_internal.h for it, which made the one
   unit the scheduler calls depend on the scheduler's own private header. The
   arrow now points the way the call does. */
_Static_assert(ERA_SPLIT_RESTART_COMMIT_DELAY_MS > ERA_SPLIT_RESTART_ARM_TIMEOUT_MS + 2 * ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS,
               "The commit deadline must sit far enough past the arm timeout for the retire to reach a responder that armed on a lost answer.");

era_split_transport_scheduler_state_t g_era_split_transport_scheduler;

static bool                                       era_split_transport_scheduler_reset_serial_for_transport_role(bool local_wire_available, bool local_wire_initiator, bool relation_rotation_required);
static inline void __attribute__((always_inline)) era_split_transport_scheduler_flush_host_peer_relation(void);
/* The responder-side mirror of the route-due mark: a latest-state producer's
   change makes the responder snapshot due now, leaving the poll as the only
   remaining latency. Change-driven only, never per-poll -- a per-poll caller
   would be the sustained core0 wake the quiet-poll design measured and
   rejected. `mark_host_peer_rgb_state_due()` in the public header feeds it.
   Declared here rather than there because every caller is in this unit. */
static void                                              era_split_transport_scheduler_mark_responder_snapshot_due(void);

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
static uint16_t era_split_transport_scheduler_edge_u16(uint32_t value) {
    return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}

/* Milliseconds since timer_init(), which quantum/keyboard.c runs as the first
   statement of keyboard_init(). Everything this measures - split_pre_init, the
   matrix/quantum/RGB init the wire now waits behind, and the launch step
   itself - happens after that call, so the reading is an elapsed boot interval
   and not an absolute clock. */
static uint16_t era_split_transport_scheduler_boot_ms(void) {
    return era_split_transport_scheduler_edge_u16(timer_read32());
}

#endif

void era_split_transport_scheduler_mark_dirty(uint8_t flags) {
    if (flags == 0) {
        return;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        g_era_split_transport_scheduler.scheduler_dirty_flags |= flags;
    }
}

void era_split_transport_scheduler_mark_route_due(uint8_t flags) {
    if (flags == 0) {
        return;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        g_era_split_transport_scheduler.route_due_flags |= flags;
    }
}

void era_split_transport_scheduler_mark_maintenance_due(uint8_t flags) {
    if (flags == 0) {
        return;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        g_era_split_transport_scheduler.maintenance_due_flags |= flags;
    }
}

/* The grant core0 issues, rebuilt from live facts and published only when it
   differs. Every field is a core0 decision: the three identities that bound
   the grant, the one bit that is the whole of route priority as core1 sees it,
   the section eligibility this relation carries, and the latest-state body.
   Core1 decides when, and nothing else.

   `enabled` is where SESSION_STATUS and storage keep their precedence. A
   pending status revalidation or an exclusive storage transfer clears it, so
   the standing exchange suspends without a teardown and resumes when the bit
   returns -- which is the same Common Route Priority the router applies to
   every other route, expressed once instead of re-derived on core1.

   **Every serviced relation's initiator builds one, per-relation period**
   (R2). The switch below is the whole of the difference: DUAL-HOST polls at
   its own constant and HOST-PEER at the response-poll period it already ran
   from core0, so opening the grant to HOST-PEER changes no wire cadence. What
   it changes is which core owns the clock, which is the point -- the two
   core0 routes this replaces cost that relation's PEER two core0 wakes per
   exchange, on the half whose keys ride the wire. */
static void era_split_transport_scheduler_build_standing_plan(era_split_communication_core_standing_plan_t *plan) {
    memset(plan, 0, sizeof(*plan));
    if (!g_era_split_transport_scheduler.local_wire_available ||
        !g_era_split_transport_scheduler.local_wire_initiator) {
        return;
    }

    uint16_t poll_period_ms;
    switch (g_era_split_transport_scheduler.mode) {
        case ERA_SPLIT_MODE_DUAL_HOST_LEFT:
            /* The one per-relation term of the grant, and the one that follows
               the link level: the byte time doubles with each step down, so
               the period doubles with it and the worst-case exchange keeps the
               same share of its own period. HOST-PEER below does not scale --
               it has room at 10 ms, and slowing it would spend the responder's
               boarding latency for nothing. */
            poll_period_ms = (uint16_t)(ERA_SPLIT_DUAL_RUNTIME_POLL_MS * era_split_transaction_backend_wire_scale());
            break;
        case ERA_SPLIT_MODE_HOST_PEER_PEER:
            /* The one precondition the retired core0 routes carried that the
               grant's three identities do not express, and it is load-bearing
               in both directions. The HOST answers a heartbeat only once its
               session records this PEER as matrix_ready
               (era_split_scheduler_session_host_peer_host_matrix_admitted()),
               so a grant issued before that would poll a responder that
               refuses -- and the poll it replaces was gated on exactly this.
               It is also what keeps the handover ordered: reaching it raises
               a status revalidation, which clears `enabled` below until the
               frame that carries matrix_ready has round-tripped. */
            if (!era_matrix_engine_local_matrix_ready()) {
                return;
            }
            poll_period_ms = ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS;
            break;
        default:
            return;
    }

    bool enabled = !g_era_split_transport_scheduler.local_status_pending;
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    enabled = enabled && !era_host_peer_storage_route_exclusive();
#endif

    plan->owner_epoch            = era_split_communication_core_owner_epoch();
    plan->relation_generation    = g_era_split_transport_scheduler.core1_initiator_relation_generation;
    plan->poll_period_ms         = poll_period_ms;
    /* Published unconditionally, because the case it exists for is the one
       where `enabled` is clear: core1 must be able to hold the relation while
       core0 is inside the durable apply that would otherwise silence it. */
    plan->liveness_period_ms     = ERA_SPLIT_STANDING_LIVENESS_MS;
    plan->enabled                = enabled ? 1 : 0;
    plan->eligible_push_sections = era_split_wire_eligible_sections((uint8_t)g_era_split_transport_scheduler.mode,
                                                                    ERA_SPLIT_WIRE_SECTION_DIRECTION_PUSH);
    plan->eligible_rsp_sections  = era_split_wire_eligible_sections((uint8_t)g_era_split_transport_scheduler.mode,
                                                                    ERA_SPLIT_WIRE_SECTION_DIRECTION_RSP);
    /* One byte from a global, on a cold path. This is the publication the
       ownership rule turns on: the *state* stays here and only its value
       crosses, so core1 composes a transaction from it without ever reading
       live QMK state.

       **Read only where push eligibility carries it**, which is the
       responder's own recorded lesson arriving at the publisher: a field that
       moves on its own makes every publish differ and turns "on change" into
       "on pass". HOST-PEER does not carry INPUT_LAYER -- its PEER never
       resolves keycodes -- so filling it there would republish the whole plan
       on every layer edge of that half for a byte the relation discards, and
       core1 would wake for each one. */
    /* The policy gate's sender arm for the INPUT-class family (storage
       version 4), with one refinement over the RGB shape: an off half
       substitutes the neutral value instead of withholding the section.
       INPUT-class state is transient correctness state the receiver holds
       applied -- a peer layer, a live window flag -- so it must be retired
       through the apply path, not stranded by absence. The memset-zero plan
       is already the neutral value, and the sent shadows make it cross
       exactly once. */
    bool input_sync_requested = false;
    if ((plan->eligible_push_sections & (ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER |
                                         ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY)) != 0) {
        (void)era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_INPUT, &input_sync_requested);
    }
    if (input_sync_requested &&
        (plan->eligible_push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER) != 0) {
        plan->input_layer = (uint8_t)layer_state;
    }
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    /* The storage-pending byte (2026-08-14 indicator redesign), behind the
       same field-follows-eligibility rule — both serviced relations carry
       it, so a granted plan always fills it and a LOCAL_NO_LINK plan never
       does. The accessor is the engine's advertised subset, whose every
       term moves only at cold storage transitions, which is the stability
       the publish-on-change discipline requires: it flips twice per
       operator action, not per pass. */
    if ((plan->eligible_push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING) != 0) {
        bool storage_pending = era_host_peer_storage_advertised_pending();
#    ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
        era_host_peer_storage_cause_note_advertised(storage_pending);
#    endif
        plan->storage_pending = storage_pending ? ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_FLAG_PENDING : 0;
    }
#endif
    /* The same rule for this half's session facts. It reads the cached local
       record rather than refreshing it, because this function runs on every
       staleness comparison and a refresh here would put a reducer snapshot on
       that path -- and because a field that moved per call would make every
       publish differ, which is the failure the anchor already paid for once. */
    plan->authority_valid        = era_split_scheduler_session_get_local_authority(&plan->authority) ? 1 : 0;
    /* This half's restart intent rides the same body, filled here and never
       through the session cache -- the two are separate facts sharing one
       carrier because the response mask has no marker left. */
    if (plan->authority_valid) {
        era_split_restart_agreement_fill_authority(&plan->authority);
    }
    /* The restart arm, under the same field-follows-eligibility rule. Both
       serviced relations carry it, so a granted plan always fills it and a
       LOCAL_NO_LINK plan never does. The deadline is absolute, so a republish
       hands core1 the same body rather than a moved one. */
    if ((plan->eligible_push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM) != 0) {
        uint8_t restart_act   = 0;
        uint8_t restart_param = 0;
        era_split_restart_agreement_arm_section(&restart_act, &restart_param, &plan->restart_commit_ms);
        plan->restart_act   = restart_act;
        plan->restart_param = restart_param;
    }
    /* The push RGB body (Slice 12), behind the same field-follows-eligibility
       rule as the layer byte -- a HOST-PEER plan never fills it -- plus the
       policy gate's sender arm: this half's own RGB requested bit gates the
       capture, the EEPROM precedent applied to this family
       (era_authority_contract.md). The capture zeroes the sleep bit, so a
       suspend flip does not make the plan differ; the config fields move only
       on a VIA edit or an RGB keycode, which is the cadence class the
       publish-on-change rule was built for. */
    if ((plan->eligible_push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE) != 0) {
        bool rgb_requested = false;
        if (era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_RGB, &rgb_requested) && rgb_requested) {
            plan->rgb_valid = era_host_peer_source_snapshot_capture_rgb_state(&plan->rgb, false) ? 1 : 0;
        }
    }
    /* The push visual baseline (Slice 14), behind the same
       field-follows-eligibility rule and the RGB policy's sender arm. The
       packed local baseline changes only on a local key edge, which is what
       keeps the publish-on-change discipline. */
    if ((plan->eligible_push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL) != 0) {
        bool visual_rgb_requested = false;
        if (era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_RGB, &visual_rgb_requested) && visual_rgb_requested) {
            matrix_row_t visual_rows[MATRIX_ROWS_PER_HAND];
            if (era_matrix_engine_copy_local_rows(visual_rows) &&
                era_split_wire_pack_matrix(visual_rows, plan->visual_baseline)) {
                plan->visual_valid = 1;
            }
        }
    }
    /* The push ACTIVITY body (FA-2 S2), behind the same
       field-follows-eligibility rule. The accessor returns the advertised
       composition -- live counters only while the peer's window is up, frozen
       otherwise -- so ordinary typing does not make every publish differ, the
       same stability contract every field of this record carries. */
    if ((plan->eligible_push_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY) != 0) {
        if (input_sync_requested) {
            era_split_tap_activity_wire_value(&plan->activity);
        }
        /* Valid in both states: an off half's zeroed neutral body must cross
           once to retire the peer's cached window flag. */
        plan->activity_valid = 1;
    }
}

/* HOST-PEER's AUTHORITY edge test and its sent-state retirement lived here
   while core0 planned, submitted and confirmed that push. R2 moved the push
   onto the standing grant, and the shadow followed the send that confirms it:
   core1's standing service already applied the identical rule to DUAL-HOST's
   AUTHORITY section, so the two relations now share one shadow rule with one
   owner instead of one rule written twice. The section itself is unchanged --
   latest-state, edge-armed, advertised while the live record differs from what
   the wire last confirmed. */

bool era_split_transport_scheduler_standing_plan_stale(void) {
    era_split_communication_core_standing_plan_t plan;
    era_split_transport_scheduler_build_standing_plan(&plan);
    return era_split_communication_core_standing_plan_differs(&plan);
}

static bool era_split_transport_scheduler_publish_standing_plan(void) {
    /* Consume the publication bit here, and before reading anything the plan
       is built from.

       This is the only place that can consume it. It is the one route-due bit
       that selects no route -- era_split_wire_router_select_owner()
       (`split/era_split_wire_router.c`) has an arm for attach-status
       revalidation and one for the HOST-PEER matrix push and no other -- so
       the scan path's refresh, which local_initiator_step() below gates on a
       route having been selected, never reaches it. Left to the housekeeping
       refresh alone the bit held scan_idle() false for up to the authority
       poll's 10 ms after every key edge, and every matrix scan in that window
       rebuilt and compared this plan for a publish the first pass had already
       done.

       Before the build rather than after it, because the mark is an OR from
       the keyboard hook: a mark arriving after this point re-arms the bit and
       is published on the next pass, where clearing after the build could
       drop one. The housekeeping refresh remains the backstop either way --
       it recomputes the bit from the actual comparison. */
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_split_transport_scheduler.route_due_flags &= (uint8_t)~ERA_SPLIT_SCHEDULER_ROUTE_DUE_DUAL_RUNTIME_PUSH;
    }

    era_split_communication_core_standing_plan_t plan;
    era_split_transport_scheduler_build_standing_plan(&plan);
    /* R7.1: cache whether this plan grants core1 anything at all. The
       initiator silence watch arms on this cached fact instead of
       re-deriving the builder's mode and matrix-ready gates, so the two can
       never drift: a nonzero relation generation is core1's own acceptance
       test for the grant (era_split_communication_core_standing.c), and a
       withheld plan — LOCAL_NO_LINK, or HOST-PEER before matrix-ready — is
       exactly the state where a frozen exchange count is legitimate. The
       review that forced this found the ungated watch firing every 100 ms
       on a healthy powered-without-USB pair. */
    g_era_split_transport_scheduler.standing_plan_granted = plan.relation_generation != 0;
    return era_split_communication_core_publish_standing_plan(&plan);
}

/* The other half of the grant: what core1 reports back. Latest-state, so a
   read that arrives late costs latency and never correctness, and a missed
   read costs nothing at all -- the next one carries the same value.

   Generation-matched like every other core1 result. A state produced under a
   relation this half has already rotated out of must not shift a keycode. */
static bool era_split_transport_scheduler_apply_standing_state(void) {
    era_split_communication_core_standing_state_t state;
    uint32_t seq = era_split_communication_core_standing_state_seq();
    if (seq == g_era_split_transport_scheduler.standing_state_seq_observed) {
        return false;
    }
    if (!era_split_communication_core_read_standing_state(&state)) {
        return false;
    }
    g_era_split_transport_scheduler.standing_state_seq_observed = seq;

    if (state.relation_generation != g_era_split_transport_scheduler.core1_initiator_relation_generation ||
        state.owner_epoch != era_split_communication_core_owner_epoch()) {
        return false;
    }

    /* The accept-clip on this side, and it repeats core1's rather than trusting
       it: the value shifts which keycode this half resolves, which is not a
       place to rely on one check. */
    uint8_t eligible_rsp = era_split_wire_eligible_sections((uint8_t)g_era_split_transport_scheduler.mode,
                                                             ERA_SPLIT_WIRE_SECTION_DIRECTION_RSP);
    /* The policy gate's apply arm for the INPUT-class family (storage
       version 4), the RGB arm's shape: this half's own INPUT requested bit
       gates the apply, read once behind the family's eligibility so
       HOST-PEER never pays it. The arrival is core1's rt rx count either
       way, so a gated-off apply reads as "sent but not applied" rather than
       as silence. */
    bool input_sync_requested = false;
    if ((eligible_rsp & (ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER |
                         ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY)) != 0) {
        (void)era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_INPUT, &input_sync_requested);
    }
    if (input_sync_requested && state.peer_input_layer_valid &&
        (eligible_rsp & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER) != 0) {
        /* The apply stays unconditional; the counter takes its answer. Every
           counter in this block does, and the reason is the same one the
           unconditional apply is written on below: reaching here means some
           field of the latest-state record moved, not necessarily this one. */
        if (era_split_peer_layer_apply((layer_state_t)state.peer_input_layer)) {
            g_era_split_transport_scheduler.host_peer_input_layer_apply_count++;
        }
    }

    /* The responder's authority, which is the news this lane exists to carry:
       a peer USB loss now reaches this half in about a poll instead of in up
       to a SESSION_STATUS period. Core1 reports it only on an edge, so
       reaching here at all means the record changed. */
    if (state.peer_authority_valid &&
        (eligible_rsp & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY) != 0) {
        if (era_split_scheduler_session_note_peer_authority(&state.peer_authority)) {
            g_era_split_transport_scheduler.host_peer_authority_rx_count++;
        }
        /* The responder's restart intent off the same body, and deliberately
           not behind the session record's own change test: that test ignores
           these fields, because a restart intent is not a session edge. Core1's
           edge is what makes reaching here mean something changed at all. */
        era_split_restart_agreement_note_peer_authority(&state.peer_authority);
    }

#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    /* The responder's settled-dirty mask section, on the same accept-clip and
       into the same consumer HOST-PEER feeds (Slice 11.7). This is what
       replaced the 50 ms `SESSION_STATUS` hint poll: one carrier and one
       consumer, in both relations.

       That sentence read "one carrier, one consumer, one in-hand set" until
       D2, and the third noun is gone. The in-hand set, the per-advertisement
       re-arm budget and the round-end re-read existed only because the hint
       named domains and armed probes directly, so every imperfection of the
       claim was a scheduling bug something had to compensate for; a hint that
       only says "ask" needs none of the three, and
       `era_host_peer_storage.c`'s `peer_news_value` comment is where that
       deletion is recorded. The first two nouns were the point of the change
       and they still hold.

       **Unconditional on purpose, and it is the consumer that makes that
       sound** (D1). This record is latest-state, so reaching here means some
       field of it moved and not necessarily this one; the value is then
       re-handed to the consumer on every unrelated standing edge, for as long
       as the cached byte stands. That was the storm: the freshness test belongs
       at the consumer, where the last-delivered value already lives, rather
       than as a fourth shadow here that would have to be rotated in step with
       the other two. Adding a test here would also have hidden the real defect
       -- a mask that could not come down -- behind a filter that made its
       symptom quiet.

       **Since D2 what crosses is a news value rather than a domain mask**, and
       the consumer arms a summary rather than probing the domains a mask named.
       This block is unchanged by that: it hands over a byte and asks nothing
       about its shape, which is why the carrier could change meaning under it.

       Ungated by this half's own EEPROM policy on purpose, and that is a
       narrowing removed rather than one forgotten. The hint is a *scheduling*
       input that bypasses no selection gate, so a half with the policy off
       selects nothing when the summary this arms comes back -- while gating
       here would make the arriving edge unobservable and lose the hint for
       good across a policy toggle, because the advertised value moves only on
       the sender's own settled captures and never because the reader's policy
       changed. HOST-PEER has always accepted it unconditionally; this makes
       the two relations agree.

       Pre-D2 that argument was written "never selects the domain the bit
       names", against a wire *level* that could in principle be re-read. Both
       phrasings are the same argument; the nouns changed under it when the
       carrier stopped naming domains and stopped being a level. */
    if (state.peer_storage_news_valid &&
        (eligible_rsp & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS) != 0) {
        era_host_peer_storage_note_host_news(state.peer_storage_news);
        /* The responder's storage-pending fact, from bit7 of the same byte --
           the initiator-side twin of the responder drain's STORAGE_PENDING
           latch (entry symmetry, 2026-08-14). Level-written on every re-hand
           exactly like the news byte above: the latch is idempotent, the
           engine clears the mirror at leave-service, and across an identity
           rotation the last level deliberately stands -- the lamp is
           continuous across the durable-apply reopen -- until the reopened
           relation's first response re-states it through the dropped sent
           shadow. The news consumer masks the flag off itself
           (NEWS_VALUE_MAX), so the raw byte is handed over unchanged. */
        era_host_peer_storage_note_peer_pending(
            (state.peer_storage_news & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_FLAG_PENDING) != 0);
#    ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
        /* Value bits only: `pnews=` is read against the peer's own `news=`
           for equality, and the pending flag's console truth is the shim's
           `mir` bit -- one fact per field. */
        g_era_split_transport_scheduler.peer_storage_news_observed =
            (uint8_t)(state.peer_storage_news & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_VALUE_MASK);
#    endif
    }
#endif

    /* The remaining response sections (R2), and since D3 this is the only path
       they arrive on -- the per-exchange result drain that used to share them
       retired with the second carrier.

       LOCK_STATE is unreachable in DUAL-HOST, whose response eligibility
       excludes it, so the clip is what keeps that arm free in that relation
       rather than a mode test. The other four have been eligible in both
       relations since Slices 12, 13, 14 and FA-2, which is why the RGB and
       visual arms below carry their own mode test; the header used to claim all
       of them were DUAL-HOST-unreachable and its own inner comments have
       contradicted it since Slice 12.

       Three of them are applied unconditionally because they are idempotent and
       reaching here at all means something moved: the lock apply is a setter,
       and the RGB apply compares before writing both the config and the suspend
       state.

       The two visible arrival counters move from here as well, and that is the
       point of counting them here rather than an extra. `wire hp role=peer
       vis=/rgb=` is the only instrument the Active-Cable gate has for "HSRSP
       visual-resync and RGB-state apply", and the per-exchange drain that used
       to feed it stops carrying HSRSP in this relation once the response poll
       retires. A counter that freezes because its carrier moved reads exactly
       like a section that stopped arriving. What does change is the unit: core1
       reports these on an edge, so they now count distinct applied states
       rather than every frame that repeated one, and `rt`'s rx side is what
       counts the arrivals. */
    if (state.peer_lock_state_valid &&
        (eligible_rsp & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE) != 0) {
        era_host_peer_transaction_apply_lock_state(state.peer_lock_state);
    }
    if (state.peer_rgb_state_valid &&
        (eligible_rsp & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE) != 0) {
        /* Reachable in both relations since Slice 12, and the two differ by
           exactly the two guards the slice decided. HOST-PEER consumes the
           whole body, sleep included -- unchanged. A DUAL-HOST Left applies
           configuration only, gated by its own RGB requested bit (the policy
           gate's apply arm); the arrival is core1's `rt` rx count either way,
           so a gated-off apply reads as "sent but not applied" on the console
           rather than as silence. */
        if (g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_DUAL_HOST_LEFT) {
            bool rgb_requested = false;
            if (era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_RGB, &rgb_requested) && rgb_requested) {
                if (era_host_peer_transaction_apply_rgb_state(&state.peer_rgb_state, false)) {
                    g_era_split_transport_scheduler.host_peer_rgb_state_rx_count++;
                }
            }
        } else if (era_host_peer_transaction_apply_rgb_state(&state.peer_rgb_state, true)) {
            g_era_split_transport_scheduler.host_peer_rgb_state_rx_count++;
        }
    }
    /* The visual snapshot is the exception, and the sequence is why. Its reason
       byte can ask this half to re-fire every pressed key, which a wake caused
       by some other section must not do. */
    if (state.peer_visual_valid &&
        (eligible_rsp & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC) != 0 &&
        (!g_era_split_transport_scheduler.standing_visual_seq_valid ||
         g_era_split_transport_scheduler.standing_visual_seq_applied != state.peer_visual_seq)) {
        g_era_split_transport_scheduler.standing_visual_seq_applied = state.peer_visual_seq;
        g_era_split_transport_scheduler.standing_visual_seq_valid   = true;
        /* Reachable in both relations since Slice 14. HOST-PEER applies
           unconditionally -- unchanged. A DUAL-HOST Left applies behind its
           own RGB requested bit; the seq shadow advances either way, so a
           gated-off apply reads as "sent but not applied" on the console
           rather than as a replay left pending. */
        if (g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_DUAL_HOST_LEFT) {
            bool visual_rgb_requested = false;
            if (era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_RGB, &visual_rgb_requested) && visual_rgb_requested) {
                era_host_peer_transaction_apply_visual_snapshot(&state.peer_visual_snapshot);
                g_era_split_transport_scheduler.host_peer_visual_snapshot_rx_count++;
            }
        } else {
            era_host_peer_transaction_apply_visual_snapshot(&state.peer_visual_snapshot);
            g_era_split_transport_scheduler.host_peer_visual_snapshot_rx_count++;
        }
    }
    /* The anchor, corrected for the time it spent in this record. Core1 stamped
       the instant it arrived and the setter adds the elapsed since — which is
       exactly what the fixed apply-offset used to stand in for and could not
       measure. Applying the corrected value is idempotent — recomputing it
       later yields the same clock, to the millisecond the division truncates —
       which is what lets this section share the others' latched-valid
       discipline instead of needing a sequence of its own.

       The correction was written out here until R2.1 and is now the setter's,
       because being a call-site adjustment is what let the other carrier apply
       the same anchor raw. */
    if (state.peer_time_anchor_valid &&
        (eligible_rsp & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_TIME_ANCHOR) != 0) {
        era_host_peer_transaction_apply_time_anchor(state.peer_time_anchor_ms, state.peer_time_anchor_rx_us);
    }
    /* The responder's tap-hold activity (FA-2 S2): a cache overwrite whose
       consumer is the tapping engine's own pass, idempotent like the lock and
       RGB applies above, so an unrelated edge re-delivering it costs
       nothing. Behind the INPUT-class apply arm read above. */
    if (input_sync_requested && state.peer_activity_valid &&
        (eligible_rsp & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY) != 0) {
        era_host_peer_transaction_apply_activity(&state.peer_activity);
    }

    /* Failure is core0's, and what it owes is a revalidation rather than a
       teardown. Core1 stopped and will not resume; raising the pending status
       is what gives ATTACH_STATUS the wire, and republishing an enabled plan
       for a reconfirmed relation is what restarts it.

       **Declaring the peer stale here as well is what made one failed exchange
       a relation collapse** (Slice 11.7). This exchange cannot tell "the
       responder refused" from "the wire is dead" -- nothing comes back either
       way -- so it must not decide. `SESSION_STATUS` is the frame that can
       tell them apart and the recovery target the route contract already names
       here, so raise it and let it answer: a live peer answers and the
       relation holds with no rotation, and a dead one does not, at which point
       `note_attach_status_request_attempt()` marks the peer stale exactly as
       it does for every other missed status. Recovery is one exchange later,
       not softer, and the reachable case it stops costing a teardown is a
       responder busy inside a storage transfer.

       This is the shape `era_split_transport_scheduler_force_storage_recovery()`
       already uses for every exclusive storage close, which is the same
       question asked by the other lane. */
    if (state.stopped && !g_era_split_transport_scheduler.standing_stop_observed) {
        g_era_split_transport_scheduler.standing_stop_observed      = true;
        g_era_split_transport_scheduler.local_status_pending        = true;
        g_era_split_transport_scheduler.attach_status_last_tx_valid = false;
        /* The failed exchange may have carried the release's all-zero visual
           baseline, and the sender's shadow cannot know (transmit-confirmed
           is not receiver-applied), so the settled-zero forced-refresh
           silence would leave a phantom pressed bit applied forever. Drop
           the applied cache here, where the loss is observed: the next
           arriving baseline diffs against nothing. */
        era_host_peer_transaction_invalidate_peer_visual_baseline();
        era_split_transport_scheduler_mark_route_due(ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS);
    } else if (!state.stopped) {
        g_era_split_transport_scheduler.standing_stop_observed = false;
    }
    return true;
}

void era_split_transport_scheduler_note_local_layer_change(void) {
    /* A local layer edge is the one runtime event that is due immediately and
       carries no cadence, the same treatment matrix dirty already gets. The
       route-due mark is what makes the next matrix-scan transport hook fail
       scan_idle() and select the route, instead of waiting for a housekeeping
       deadline that could be a poll period away.

       It marks the initiator's route and the DUAL-HOST Right's snapshot: the
       Right cannot select a route, but its RSP INPUT_LAYER byte otherwise
       waits out the authority-poll wake -- the responder-latency defect the
       due mark exists to close. A HOST-PEER half still pays only the cached
       mode comparison for a bit the router would then ignore, which matters
       because this runs from layer_state_set_kb() on every layer edge of
       every half.

       This is the whole producer since Slice 11.5. It used to also open an
       activity window, and a second producer on every key event kept that
       window alive; both are gone with the window, and with them the
       per-keystroke deadline-cache invalidation they performed. */
    if (!g_era_split_transport_scheduler.input_sync_requested_cached) {
        return; /* Off half: the neutral crossed at the policy edge; per-edge
                   marks would buy a wasted pass each. */
    }
    if (g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_DUAL_HOST_LEFT) {
        era_split_transport_scheduler_mark_route_due(ERA_SPLIT_SCHEDULER_ROUTE_DUE_DUAL_RUNTIME_PUSH);
    } else if (g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_DUAL_HOST_RIGHT) {
        era_split_transport_scheduler_mark_responder_snapshot_due();
    }
}

void era_split_transport_scheduler_note_local_visual_change(void) {
    /* Slice 14's push producer, the layer producer's shape: a local key edge
       is a visual-baseline change wherever the visual push is armed. The
       Left marks its route; the Right's key edges already mark the snapshot
       through the visual publish's own success arm. Bounded by the policy
       gate -- with RGB sync off this is one cached mode compare and one
       policy read per key edge, and no mark. */
    if (g_era_split_transport_scheduler.mode != ERA_SPLIT_MODE_DUAL_HOST_LEFT) {
        return;
    }
    if (g_era_split_transport_scheduler.rgb_sync_requested_cached) {
        era_split_transport_scheduler_mark_route_due(ERA_SPLIT_SCHEDULER_ROUTE_DUE_DUAL_RUNTIME_PUSH);
    }
}

void era_split_transport_scheduler_note_local_activity_change(void) {
    /* FA-2 S2's producer, on the layer producer's exact shape and for the same
       reason: an advertised-activity change -- a window edge, or key input
       while the peer's window is up -- is due immediately and carries no
       cadence. The Left marks its route; the Right marks its snapshot due --
       riding the housekeeping deadline instead cost the judgment 0-10 ms of
       arrival latency, which is what lost sequence A 9 of 14 windows on the
       device. Bounded by construction either way: the advertised value moves
       only while a judgment window is open on one half or the other, so
       fresh defaults never reach this. */
    if (!g_era_split_transport_scheduler.input_sync_requested_cached) {
        return; /* Off half: the neutral crossed at the policy edge. */
    }
    if (g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_DUAL_HOST_LEFT) {
        era_split_transport_scheduler_mark_route_due(ERA_SPLIT_SCHEDULER_ROUTE_DUE_DUAL_RUNTIME_PUSH);
    } else if (g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_DUAL_HOST_RIGHT) {
        era_split_transport_scheduler_mark_responder_snapshot_due();
    }
}

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
/* One line, two writers, and a half is only ever one of them. The responder
   counts into the scheduler's own fields from the response it sent; the
   initiator's counts live in the standing state, because core1 is what sends
   and applies there now. Summing is correct rather than lazy: a DUAL-HOST Left
   never touches the responder fields and a Right never runs the standing
   service, so exactly one side of each sum is zero. */
void era_split_transport_scheduler_get_dual_runtime_counts(uint32_t *tx_count, uint32_t *rx_count) {
    era_split_communication_core_standing_state_t state;
    if (!era_split_communication_core_read_standing_state(&state)) {
        memset(&state, 0, sizeof(state));
    }
    if (tx_count != NULL) {
        *tx_count = g_era_split_transport_scheduler.dual_runtime_tx_count + state.tx_section_count;
    }
    if (rx_count != NULL) {
        *rx_count = g_era_split_transport_scheduler.dual_runtime_rx_count + state.rx_section_count;
    }
}

void era_split_transport_scheduler_get_maintenance_source_counts(uint32_t *entry_count, uint32_t *counts) {
    if (entry_count != NULL) {
        *entry_count = g_era_split_transport_scheduler.housekeeping_entry_count;
    }
    if (counts != NULL) {
        memcpy(counts, g_era_split_transport_scheduler.maintenance_source_count, sizeof(g_era_split_transport_scheduler.maintenance_source_count));
    }
}

uint32_t era_split_transport_scheduler_get_responder_snapshot_retry_count(void) {
    return g_era_split_transport_scheduler.responder_snapshot_publish_retry_count;
}

#endif

void era_split_transport_scheduler_mark_host_peer_rgb_state_due(void) {
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_split_transport_scheduler.host_peer_responder_rgb_state_publish_deadline_valid = false;
        g_era_split_transport_scheduler.responder_snapshot_publish_due                       = true;
        g_era_split_transport_scheduler.next_scheduler_deadline_valid                        = false;
    }
}

static void era_split_transport_scheduler_mark_responder_snapshot_due(void) {
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_split_transport_scheduler.responder_snapshot_publish_due = true;
        g_era_split_transport_scheduler.next_scheduler_deadline_valid  = false;
    }
}

/* One relation compare, on the scan-adjacent path the sleep resolver runs
   from. It is a mode test rather than a `local_wire_initiator` test because
   the fact it answers is "does this half have a USB session of its own", and
   the HOST-PEER PEER is the only relation role for which the answer is no --
   a DUAL-HOST Left is also an initiator and owns its lighting outright. */
bool era_split_transport_scheduler_lighting_sleep_owner_is_wire(void) {
    return g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_HOST_PEER_PEER;
}

/* The INPUT-class policy edge (storage version 4). On the 1->0 edge run the
   relation-rotation clear set: the peer layer and the tap-activity caches are
   peer-derived INPUT-class state this half just refused to keep consuming,
   and the activity reset also retires the stale peer window flag that would
   otherwise keep the advertised composition recomposing per keystroke. On
   the 0->1 edge re-observe the standing state: the latched record re-applies
   through the idempotent, generation-fenced apply paths, healing the
   initiator side's inbound state without waiting for the peer's next edge;
   the responder side has no retained record to replay (push results drain
   once) and heals on the peer's next edge -- the documented residual. Every
   producer of a policy change funnels through DIRTY_SYNC_POLICY -- the VIA
   set, reset-to-defaults, and the EEPROM reload -- so all of them get this
   edge handling. */
static void era_split_transport_scheduler_note_sync_policy_edge(void) {
    bool input_requested = false;
    (void)era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_INPUT, &input_requested);
    bool rgb_requested = false;
    (void)era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_RGB, &rgb_requested);
    if (g_era_split_transport_scheduler.rgb_sync_requested_cached != rgb_requested) {
        g_era_split_transport_scheduler.rgb_sync_requested_cached = rgb_requested;
        /* Either RGB edge drops the applied visual cache: a disable can
           strand a pressed bit the sender will never re-cross (its disarm
           clears the snapshot store), and an enable must not diff the first
           arriving baseline against a stale one. */
        era_host_peer_transaction_invalidate_peer_visual_baseline();
    }
    if (g_era_split_transport_scheduler.input_sync_requested_cached != input_requested) {
        g_era_split_transport_scheduler.input_sync_requested_cached = input_requested;
        if (!input_requested) {
            era_split_peer_layer_clear();
            era_split_tap_activity_relation_reset();
        } else {
            g_era_split_transport_scheduler.standing_state_seq_observed--;
        }
    }
}

static uint8_t era_split_transport_scheduler_read_pending_dirty_flags(void) {
    uint8_t flags;
    ATOMIC_BLOCK_RESTORESTATE {
        flags = g_era_split_transport_scheduler.scheduler_dirty_flags;
    }
    return flags;
}

static uint8_t era_split_transport_scheduler_consume_pending_dirty_flags(void) {
    uint8_t flags;
    ATOMIC_BLOCK_RESTORESTATE {
        flags                                                 = g_era_split_transport_scheduler.scheduler_dirty_flags;
        g_era_split_transport_scheduler.scheduler_dirty_flags = 0;
    }
    return flags;
}

/* Read without the interrupt lock its two siblings above hold, and the
   difference is what each is for. They *consume* -- read-and-clear -- so a
   writer landing between the read and the store would lose a flag outright.
   This one only asks whether either byte is non-zero, and never writes.
   Each byte is a single aligned load on this core, so the only thing a lock
   could add is consistency *between* the two loads -- and the answer it
   protects is a disjunction, where an interleaved set can cost at most one
   pass. The flag stays set, the next pass sees it, and both flags feed
   deadlines measured in milliseconds against a 25 us pass.
   What it costs to keep is not the microsecond. This gate runs on every
   scan-bound housekeeping call -- about 32,000 times a second -- so the lock
   was a 32 kHz comb of interrupt-disable windows on the core that also serves
   USB. That is the reason to drop it; the 0.24 us is the smaller half. */
static bool era_split_transport_scheduler_pending_work_flags(void) {
    return g_era_split_transport_scheduler.scheduler_dirty_flags != 0 || g_era_split_transport_scheduler.maintenance_due_flags != 0;
}

static uint8_t era_split_transport_scheduler_consume_maintenance_due_flags(void) {
    uint8_t flags;
    ATOMIC_BLOCK_RESTORESTATE {
        flags                                                 = g_era_split_transport_scheduler.maintenance_due_flags;
        g_era_split_transport_scheduler.maintenance_due_flags = 0;
    }
    return flags;
}

static bool era_split_transport_scheduler_authority_changed(const era_authority_snapshot_t *snapshot) {
    if (!g_era_split_transport_scheduler.authority_snapshot_valid || snapshot == NULL) {
        return true;
    }

    const era_authority_snapshot_t *previous = &g_era_split_transport_scheduler.authority_snapshot;
    return previous->valid != snapshot->valid || previous->is_left != snapshot->is_left || previous->usb_state != snapshot->usb_state || previous->usb_epoch != snapshot->usb_epoch;
}

static bool era_split_transport_scheduler_local_host_closed(const era_authority_snapshot_t *snapshot) {
    if (!g_era_split_transport_scheduler.authority_snapshot_valid || snapshot == NULL) {
        return false;
    }

    const era_authority_snapshot_t *previous = &g_era_split_transport_scheduler.authority_snapshot;
    return previous->valid && previous->usb_state == ERA_AUTH_USB_HOST_OPEN && (!snapshot->valid || snapshot->usb_state != ERA_AUTH_USB_HOST_OPEN);
}

static uint8_t era_split_transport_scheduler_dirty_flags(bool authority_changed, bool peer_generation_changed, bool peer_session_stale, bool mode_changed, bool wire_role_changed) {
    uint8_t flags = 0;
    if (authority_changed) {
        flags |= ERA_SPLIT_SCHEDULER_DIRTY_AUTHORITY;
    }
    if (peer_generation_changed) {
        flags |= ERA_SPLIT_SCHEDULER_DIRTY_PEER_SESSION;
    }
    if (peer_session_stale) {
        flags |= ERA_SPLIT_SCHEDULER_DIRTY_PEER_STALE;
    }
    if (mode_changed) {
        flags |= ERA_SPLIT_SCHEDULER_DIRTY_MODE;
    }
    if (wire_role_changed) {
        flags |= ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE;
    }
    return flags;
}

static bool era_split_transport_scheduler_update_mode(void) {
    era_authority_snapshot_t      auth;
    era_split_mode_peer_session_t peer_session;

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    g_era_split_transport_scheduler.plan_count++;
#endif

    era_split_authority_reducer_get_snapshot(&auth);

    bool authority_changed = era_split_transport_scheduler_authority_changed(&auth);
    bool local_host_closed = era_split_transport_scheduler_local_host_closed(&auth);
    era_split_scheduler_session_note_local_facts(&auth);
    bool peer_generation_changed = false;
    era_split_scheduler_session_consume_peer_mode_session(&peer_session, &peer_generation_changed);
    /* The forced stale recovery, scoped to the fact it is actually about. A
       local HOST close invalidates the peer-session facts only where those
       facts were read against a *hosted* peer: there the close flips this half
       from responder to initiator, and the authority contract's rule is
       written for exactly that -- do not choose an initiator from stale
       both-hosted facts, return to the peer-unknown bootstrap ordering.

       Against a peer that is unambiguously no-host, nothing here re-decides
       anything. That peer was the relation's initiator before the close and
       still is, and it cannot become the HOST by an edge that happened on this
       half. Forcing staleness there forgot the peer session and tore down a
       HOST-PEER relation whose roles were never in doubt, once per computer
       sleep. Peer-unknown and a peer whose status is neither keep the old
       answer, because neither is a relation worth holding. */
    bool peer_no_host_only  = peer_session.known && !peer_session.accepted_host_open && peer_session.accepted_no_host;
    bool peer_session_stale = (local_host_closed && !peer_no_host_only) || g_era_split_transport_scheduler.peer_session_stale || era_split_transport_scheduler_responder_silence_stale(peer_session.known);

    era_split_mode_planner_input_t input = {
        .local_authority         = auth,
        .local_side              = auth.is_left ? ERA_SPLIT_AUTHORITY_SIDE_LEFT : ERA_SPLIT_AUTHORITY_SIDE_RIGHT,
        .peer_session            = peer_session,
        .current_mode            = g_era_split_transport_scheduler.mode,
        .local_authority_changed = authority_changed,
        .peer_generation_changed = peer_generation_changed,
        .secondary_stale         = peer_session_stale,
        /* Read against the *current* mode, which is what the planner needs:
           the consequence it feeds is gated on the mode not changing, so
           current and next are the same relation wherever it is used. */
        .relation_authority_lane_live = era_split_transport_scheduler_relation_lane_live(),
    };

    era_split_mode_planner_result_t result;
    era_split_mode_planner_decide(&input, &result);

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    era_split_transport_scheduler_role_diagnostics_note_mode_change(g_era_split_transport_scheduler.mode, result.next_mode);
#endif

    if (authority_changed) {
        era_split_transport_scheduler_reset_session_probe_backoff();
    }
    /* R7: a capped launch makes the wire unavailable by policy. This is what
       turns the give-up latch into convergence — the plan stops wanting a
       wire role, the transfer to NONE succeeds trivially, the mode commits
       LOCAL_NO_LINK, and the retry loop that used to spend a 10 ms handshake
       timeout per housekeeping pass ends with it. */
    bool    next_local_wire_available = auth.valid && (auth.usb_state == ERA_AUTH_USB_HOST_OPEN || auth.usb_state == ERA_AUTH_USB_NO_HOST) &&
                                     !era_split_communication_core_launch_capped();
    bool    next_local_wire_initiator = next_local_wire_available && result.local_wire_initiator;
    bool    wire_role_changed         = !g_era_split_transport_scheduler.authority_snapshot_valid || g_era_split_transport_scheduler.local_wire_available != next_local_wire_available || g_era_split_transport_scheduler.local_wire_initiator != next_local_wire_initiator;
    uint8_t dirty_flags               = era_split_transport_scheduler_dirty_flags(authority_changed, peer_generation_changed, peer_session_stale, result.mode_changed, wire_role_changed);
    bool    maintenance_performed     = dirty_flags != 0 || result.peer_matrix_flush_required || result.peer_session_forget_required || result.local_status_required;

    /* The gate is unchanged: an authority, mode, or wire-role edge. What
       changed is that the two responsibilities behind it are now separate.
       The relation rotates on exactly the pre-existing set (authority or mode
       edge, plus every wire-role change, which always takes the full path
       below), while the backend teardown/rebuild runs only when the wire lease
       itself needs it. */
    if (authority_changed || result.mode_changed || wire_role_changed) {
        if (!era_split_transport_scheduler_reset_serial_for_transport_role(next_local_wire_available, next_local_wire_initiator, authority_changed || result.mode_changed)) {
            era_split_transport_scheduler_mark_dirty(dirty_flags);
            return true;
        }
    }

    if (result.peer_matrix_flush_required) {
        era_split_transport_scheduler_flush_host_peer_relation();
        era_host_peer_transaction_force_responder_lock_state_response();
    }
    if (result.peer_session_forget_required) {
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
        era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_SESSION_FORGET, 0);
#endif
        era_split_scheduler_session_forget_peer_from_scheduler();
        g_era_split_transport_scheduler.peer_session_stale = false;
        era_split_transport_scheduler_reset_responder_silence_watch();
    }
    if (result.local_status_required) {
        g_era_split_transport_scheduler.local_status_pending        = true;
        g_era_split_transport_scheduler.attach_status_last_tx_valid = false;
    }

    /* The relation, to the two lanes that read it. The agreement needs
       whether one is serviced at all and whether this half is the side that
       owns the commit; the link lane needs whether one is serviced and whether
       this half is the *listener* -- the peer-unknown bootstrap's responder,
       which is three facts and all three are this layer's: the wire is
       available (the lease below is built from the same term), the planner
       gave this half the responder role, and the plan is the bootstrap itself.
       The third is what makes "the talker never moves" structural rather than
       a matter of timing: a Left half is a responder beside a *known* peer in
       exactly one no-relation state -- a peer whose status is neither host-open
       nor no-host, which parks both halves as responders until the silence
       watch forgets it -- and without the bootstrap term it would be a listener
       there for the ~100 ms that state lasts. The agreement's "relation
       initiator" term is not usable for any of this: it is false on both
       halves in LOCAL_NO_LINK, which is exactly where the listener works. All
       of it is the settled mode read once per maintenance pass, which is the
       cadence both lanes run at anyway. */
    era_split_restart_agreement_note_relation(result.next_mode != ERA_SPLIT_MODE_LOCAL_NO_LINK,
                                              result.next_mode == ERA_SPLIT_MODE_DUAL_HOST_LEFT || result.next_mode == ERA_SPLIT_MODE_HOST_PEER_PEER,
                                              g_era_split_transport_scheduler.authority_snapshot.valid &&
                                                  g_era_split_transport_scheduler.authority_snapshot.is_left);
    era_split_link_note_relation(result.next_mode != ERA_SPLIT_MODE_LOCAL_NO_LINK,
                                 next_local_wire_available && !next_local_wire_initiator && result.peer_unknown,
                                 result.next_mode == ERA_SPLIT_MODE_DUAL_HOST_LEFT || result.next_mode == ERA_SPLIT_MODE_HOST_PEER_HOST);

    /* The peer layer is DUAL-HOST-only state. Any settled mode that is not a
       confirmed DUAL-HOST drops it, which is what keeps a cable pull from
       stranding this half shifted with nothing left to unshift it. The tap
       activity peer cache is the same class of state and drops with it. */
    if (result.next_mode != ERA_SPLIT_MODE_DUAL_HOST_LEFT && result.next_mode != ERA_SPLIT_MODE_DUAL_HOST_RIGHT) {
        era_split_peer_layer_clear();
        era_split_tap_activity_relation_reset();
    }

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    g_era_split_transport_scheduler.previous_mode            = g_era_split_transport_scheduler.mode;
#endif
    g_era_split_transport_scheduler.mode                     = result.next_mode;
    g_era_split_transport_scheduler.local_wire_available     = next_local_wire_available;
    g_era_split_transport_scheduler.local_wire_initiator     = next_local_wire_initiator;
    g_era_split_transport_scheduler.authority_snapshot       = auth;
    g_era_split_transport_scheduler.authority_snapshot_valid = true;
    (void)era_split_transport_scheduler_publish_communication_core_responder_snapshot();
    return maintenance_performed;
}

/* The shared clock's source is the wire responder, not the USB master: in
   DUAL-HOST both halves are masters, so mastery cannot pick the one half
   whose clock the pair follows. The committed wire role can — the initiator
   adopts the responder's published anchor in both relations. */
bool sync_timer_is_time_source(void) {
    return !g_era_split_transport_scheduler.local_wire_initiator;
}

static inline void __attribute__((always_inline)) era_split_transport_scheduler_flush_host_peer_relation(void) {
    era_matrix_engine_flush_host_peer_relation();
}

static inline void __attribute__((always_inline)) era_split_transport_scheduler_ensure_initialized(void) {
    if (!g_era_split_transport_scheduler.initialized) {
        era_split_transport_scheduler_init();
    }
}

/* The relation identity token. It fences every generation-matched consumer:
   the Core1 initiator request stamp and result match, the responder snapshot,
   and the HOST-PEER storage runtime context whose change arms the mandatory
   seven-domain relation-open audit sweep. Rotating it is a relation-level
   decision, not a wire-lease one. */
static void era_split_transport_scheduler_rotate_core1_relation(void) {
    g_era_split_transport_scheduler.core1_initiator_relation_generation++;
    if (g_era_split_transport_scheduler.core1_initiator_relation_generation == 0) {
        g_era_split_transport_scheduler.core1_initiator_relation_generation = 1;
    }
    /* A peer layer value belongs to the relation era it arrived in, exactly
       like the peer matrix cache. Rotating the identity token is what makes
       this half stop resolving keycodes on a layer the peer may no longer be
       holding, and it covers the reopen that keeps the same mode as well as
       the mode change below. */
    era_split_peer_layer_clear();
    /* The tap activity caches rotate with it: the peer cache for the same
       reason the layer does, and the advertised record back to the all-zero
       baseline the reopened peer's cleared cache assumes -- which is what
       keeps a reopened relation on fresh defaults silent. */
    era_split_tap_activity_relation_reset();
    /* The agreed restart's peer cache and an unconfirmed arm rotate with them;
       a confirmed commit and the arm that is its wire face deliberately do not:
       a relation that rotates after both halves armed is the dying link the
       deadline exists for. */
    era_split_restart_agreement_note_relation_rotation();

    /* The visual replay marker rotates with the relation for the same reason
       the shadows do, and it is a coincidence rather than a level that makes it
       necessary: `clear_standing()` zeroes core1's sequence, so the reopened
       relation counts from one again, and a marker left holding that same value
       from the previous era would let core0 skip the first genuinely new
       snapshot -- a relation-open baseline, which is the one this half most
       needs. An eight-bit counter makes that collision unlikely rather than
       impossible, which is not a property to rely on. */
    g_era_split_transport_scheduler.standing_visual_seq_valid   = false;
    g_era_split_transport_scheduler.standing_visual_seq_applied = 0;

    /* The standing grant rotates with the relation, and this is not symmetry
       for its own sake. Core1 resets its own sent-state shadow when it sees a
       new relation generation, because the peer clears what it holds on the
       same event and a surviving shadow would let an unchanged layer count as
       already known -- leaving the reopened peer at zero until the user
       happened to touch a layer key. Clearing the published plan here is what
       makes core1 see that generation change at all, and it also stops the
       standing exchange for the window in which the relation is undecided.

       The observed sequence resets with it: a state core1 published under the
       previous relation must not be read as news in the next one, and the
       generation check on apply is the second guard rather than the only. */
    era_split_communication_core_clear_standing();
    g_era_split_transport_scheduler.standing_state_seq_observed = era_split_communication_core_standing_state_seq();
    g_era_split_transport_scheduler.standing_stop_observed      = false;
    /* HOST-PEER's authority shadow used to be rotated here too. Since R2 it is
       core1's, and core1 rotates it off the relation generation this same
       clear_standing() makes it observe -- one rotation on the side that
       confirms the send, in both relations. */
    era_host_peer_transaction_forget_responder_input_layer();
}

static bool era_split_transport_scheduler_reset_serial_for_transport_role(bool local_wire_available, bool local_wire_initiator, bool relation_rotation_required) {
    /* Before the explicit launch step this function is policy-only. Mode
       planning still runs and still records the wire role it decided; it just
       does not open the wire, so no path through
       era_split_transport_scheduler_init() - and therefore no path through
       transport_master_init()/transport_slave_init() - can launch core1.
       era_split_transport_scheduler_start_communication_core() replays the
       recorded decision once, at keyboard post-init.

       Returning true rather than false is deliberate: the caller's contract is
       "is the serial role settled for this plan", and during boot it is, by
       construction - there is no wire to be wrong about yet. Reporting failure
       would mark the scheduler dirty for a condition the launch step is about
       to resolve. */
    if (!g_era_split_transport_scheduler.communication_core_started) {
        return true;
    }

    /* Idempotent wire fast-path. The caller opens this path on any relation or
       wire-lease edge, but only a lease change needs backend work. When the
       wire is available and the live owner lease is already a coherent CORE1
       lease with the target role (ready for the current epoch, no pending
       revoke), the serial role is already established: skip the owner
       teardown/rebuild, which would otherwise churn the owner epoch and
       re-park the wire for nothing.

       The relation rotation is a separate decision the caller declares, so a
       relation change with an unchanged lease still rotates. That matters
       because the lease and the relation are not the same fact: on a half that
       is the wire initiator in every mode (a LEFT PEER, since the planner
       derives the peer-unknown initiator from the side), settling into
       HOST-PEER, losing the cable, and reconnecting all keep the identical
       (CORE1, INITIATOR) lease. Rotating there keeps the storage audit sweep
       armed by its documented relation signal and keeps an in-flight
       initiator's result fenced out of the new relation, without paying the
       Core1 park that cancelling the request would cost.

       A genuine wire-role change (owner/role skew, an unpublished ready after a
       rebuild timeout, a pending revoke, or a NONE target) fails the predicate
       and falls through to the full teardown/rebuild below; the
       storage-rotation and diagnostic callers pre-tear the owner to NONE, so
       they never match this predicate and always take the full path. */
    if (local_wire_available &&
        era_split_communication_core_owner_core1_role_is_live(
            local_wire_initiator ? ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR
                                 : ERA_SPLIT_TRANSACTION_BACKEND_ROLE_RESPONDER)) {
        /* R7 fix review finding (2026-08-06): a live serviced lease observed
           on this fast path clears the give-up streak, same as the ensure
           fast path and the full transfer's ready observation — see
           era_split_communication_core_owner.c for the accumulation hazard
           this closes. One byte read when the streak is already zero. */
        era_split_communication_core_note_core1_serviced();
        if (relation_rotation_required) {
            era_split_transport_scheduler_rotate_core1_relation();
        }
        return true;
    }
    bool reset = false;
    if (!era_split_transport_scheduler_cancel_core1_initiator()) {
        goto done;
    }
    era_split_communication_core_backend_owner_t next_owner = ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE;
    era_split_transaction_backend_role_t next_role = ERA_SPLIT_TRANSACTION_BACKEND_ROLE_DISABLED;
    if (local_wire_available) {
        next_owner = ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1;
        next_role  = local_wire_initiator ? ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR : ERA_SPLIT_TRANSACTION_BACKEND_ROLE_RESPONDER;
    }
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    if (!era_split_communication_core_owner_transfer_role(ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE,
                                                           ERA_SPLIT_TRANSACTION_BACKEND_ROLE_DISABLED)) {
        goto done;
    }
    era_split_communication_core_storage_capacity_init();
    if (next_owner != ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE &&
        !era_split_communication_core_owner_transfer_role(next_owner, next_role)) {
        goto done;
    }
#else
    if (!era_split_communication_core_owner_transfer_role(next_owner, next_role)) {
        goto done;
    }
#endif
    (void)era_split_transport_scheduler_drain_communication_core_responder_results();
    if (relation_rotation_required) {
        era_split_transport_scheduler_rotate_core1_relation();
    }
    reset = true;

done:
    return reset;
}

#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
void era_split_transport_scheduler_force_storage_recovery(bool revalidate_session) {
    era_split_transport_scheduler_flush_host_peer_relation();
    era_host_peer_transaction_force_responder_lock_state_response();
    if (revalidate_session) {
        g_era_split_transport_scheduler.local_status_pending        = true;
        g_era_split_transport_scheduler.attach_status_last_tx_valid = false;
        era_split_transport_scheduler_mark_route_due(ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS);
    }
    g_era_split_transport_scheduler.next_scheduler_deadline_valid = false;
}

bool era_split_transport_scheduler_rotate_storage_relation(void) {
    era_split_transport_scheduler_ensure_initialized();
    if (!era_split_transport_scheduler_stop_communication_core_for_flash_write()) {
        return false;
    }
    bool restored = era_split_transport_scheduler_reset_serial_for_transport_role(
        g_era_split_transport_scheduler.local_wire_available,
        g_era_split_transport_scheduler.local_wire_initiator,
        true);
    if (!restored) {
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE);
        return false;
    }
    era_split_transport_scheduler_force_storage_recovery(true);
    return true;
}
#endif

/* A divider change, performing half. The listener's recovery step and the
   agreed raise both land here, because changing the PIO divider needs the
   backend owner torn down and this unit is what owns that window. The
   listener has no relation to interrupt. The raise fires on both halves at
   a shared-clock deadline and keeps the relation identity
   (`rotate_relation` false) so the silence watch is the window they must
   fit.

   The dwell must hold two of the talker's backed-off probes, and the terms of
   that arithmetic are split across three headers -- the dwell is the link
   unit's, the backoff period is the scheduler's, and the response window each
   probe carries is the wire protocol's, scaled by up to the Low level's factor
   -- so the unit that includes all three is where the assert lives. */
_Static_assert(ERA_SPLIT_LINK_SCAN_DWELL_MS >= 2U * (ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS + ERA_SPLIT_PEER_RESPONSE_WINDOW_MS * (ERA_SPLIT_LINK_SPEED_HIGH / ERA_SPLIT_LINK_SPEED_LOW)),
               "The listener's dwell must hold at least two backed-off discovery probes, each with its slowest response window, or a talking peer could go unheard.");
_Static_assert(ERA_SPLIT_LINK_UPGRADE_CONFIRM_MS >= 2U * ERA_SPLIT_RESPONDER_SILENCE_MS,
               "The raise-confirm window must outlast one responder-silence watch, or a High the cable cannot hold is not observed before the session is declared live.");
static bool era_split_transport_scheduler_apply_link_step(uint8_t level, bool rotate_relation) {
    if (!era_split_transport_scheduler_stop_communication_core_for_flash_write()) {
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE);
        return false;
    }
    era_split_transaction_backend_set_speed(era_split_link_speed(level));
    era_split_link_note_step_applied(level);
    bool restored = era_split_transport_scheduler_reset_serial_for_transport_role(
        g_era_split_transport_scheduler.local_wire_available,
        g_era_split_transport_scheduler.local_wire_initiator,
        rotate_relation);
    if (!restored) {
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE);
    }
    return restored;
}

bool era_split_transport_scheduler_apply_link_level(uint8_t level) {
    if (era_split_link_active_level() == level) {
        era_split_link_note_step_applied(level);
        return true;
    }
    return era_split_transport_scheduler_apply_link_step(level, false);
}

bool era_split_transport_scheduler_flush_communication_core_for_diagnostics(void) {
    era_split_transport_scheduler_ensure_initialized();

    if (!era_split_transport_scheduler_stop_communication_core_for_flash_write()) {
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE);
        return false;
    }

    bool flushed = era_split_communication_core_queue_reset();
    bool restored = era_split_transport_scheduler_reset_serial_for_transport_role(g_era_split_transport_scheduler.local_wire_available,
                                                                                    g_era_split_transport_scheduler.local_wire_initiator,
                                                                                    true);
    if (!restored) {
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE);
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    } else {
        era_split_transport_scheduler_force_storage_recovery(true);
#endif
    }
    return flushed && restored;
}

/* The clock-free half of the housekeeping gate. Every arm below is an event, a
   latch or a published word, and not one of them needs to know what time it
   is -- which is why the deadline compare that used to close this function now
   sits at the caller. Splitting it is the whole of T9: the scan-rate entry read
   the clock before it knew whether it had anything to ask the clock about, and
   `timer_read32()` costs about 0.42 us per call even answering from its
   millisecond cache (`platforms/chibios/timer.c`). */
static bool era_split_transport_scheduler_housekeeping_event_due(void) {
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    /* A ready dedicated-storage result is an ownership fact, not a runtime
       policy/watch fact. Core1 may publish the first responder result in the
       narrow relation-transition window before core0 has entered the storage
       runtime role. If readiness is hidden behind a later-computed watch bit,
       that result can remain reserved forever: every subsequent storage frame
       then finds the responder-result slot full and is consumed without a
       response, which turns one timing race into a permanent 0xEA timeout
       loop. Wake on the published result itself so core0 always gets a chance
       to drain, validate or retire the owner it already has. */
    if (era_split_communication_core_storage_result_due()) {
        return true;
    }
#endif
    if (era_split_communication_core_responder_result_ready()) {
        return true;
    }
    /* One aligned word, no lock and no retry: the guarded read only happens
       once this has actually moved. That is what keeps the standing grant's
       report edge-driven rather than polled, and it is the whole per-scan cost
       of holding the grant on this side. */
    if (era_split_communication_core_standing_state_seq() != g_era_split_transport_scheduler.standing_state_seq_observed) {
        return true;
    }
    /* A *ready result*, not a pending request. Core1 publishes when it
       finishes, so the ring going non-empty is the edge; waking on the pending
       flag instead meant the whole in-flight window woke core0 once per matrix
       scan, and every one of those passes rebuilt state that had not changed.
       That is the second of the two core0-polls-core1 mechanisms Slice 11.6
       removes -- the first was the same flag inside poll_core1_initiator(). */
    if (era_split_communication_core_initiator_result_ready()) {
        return true;
    }
    if (era_split_transport_scheduler_pending_work_flags()) {
        return true;
    }
    if (g_era_split_transport_scheduler.responder_snapshot_publish_due) {
        return true;
    }
    /* No deadline published yet is a reason to run, and it belongs on this side
       of the split: it is a state question, not a time one, and keeping it here
       is what lets the raw stamp be read only where it is known to be current. */
    return !g_era_split_transport_scheduler.next_scheduler_deadline_valid;
}

/* The deadline half, asked of the raw hardware counter. It is a pre-filter and
   never an authority: the stamp is built to be early rather than late
   (`scheduler/era_split_transport_scheduler_timing.c`), so an open gate still
   owes the millisecond compare, and a decline there just costs the clock read
   this exists to avoid. The idiom is the wire lanes' own deadline test
   (`communication_core/era_split_communication_core_host_peer_lanes.c`). */
static inline bool era_split_transport_scheduler_deadline_raw_reached(void) {
    return (int32_t)(timer_hw->timerawl - g_era_split_transport_scheduler.next_scheduler_deadline_raw_us) >= 0;
}

static bool era_split_transport_scheduler_local_matrix_publish_needed(void) {
    if (!g_era_split_transport_scheduler.authority_snapshot_valid || g_era_split_transport_scheduler.authority_snapshot.usb_state != ERA_AUTH_USB_NO_HOST) {
        return false;
    }

    if (g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_HOST_PEER_PEER) {
        return true;
    }

    return g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_LOCAL_NO_LINK && !era_matrix_engine_local_matrix_ready();
}

static bool era_split_transport_scheduler_publish_local_matrix_if_needed(bool *first_ready) {
    if (first_ready != NULL) {
        *first_ready = false;
    }
    if (!era_split_transport_scheduler_local_matrix_publish_needed()) {
        return false;
    }
    return era_matrix_engine_publish_local_snapshot_if_needed(first_ready);
}

static void era_split_transport_scheduler_mark_local_matrix_ready_revalidation(void) {
    g_era_split_transport_scheduler.local_status_pending        = true;
    g_era_split_transport_scheduler.attach_status_last_tx_valid = false;
    era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_MATRIX_READY);
    era_split_transport_scheduler_mark_route_due(ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS);
}

static void era_split_transport_scheduler_note_local_matrix_publish(bool changed, bool first_ready) {
    if (first_ready) {
        era_split_transport_scheduler_mark_local_matrix_ready_revalidation();
    }

#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    if (era_host_peer_storage_initiator_request_pending() ||
        era_host_peer_storage_route_exclusive()) {
        return;
    }
#endif

    if (!changed || !era_split_transport_scheduler_local_initiator_available() || g_era_split_transport_scheduler.mode != ERA_SPLIT_MODE_HOST_PEER_PEER || era_split_transport_scheduler_local_status_revalidation_due() || !era_matrix_engine_source_push_due()) {
        return;
    }

    era_split_transport_scheduler_mark_route_due(ERA_SPLIT_SCHEDULER_ROUTE_DUE_HOST_PEER_SOURCE_PUSH);
}

static inline bool __attribute__((always_inline)) era_split_transport_scheduler_local_matrix_publish_scan_idle(void) {
    if (!g_era_split_transport_scheduler.authority_snapshot_valid || g_era_split_transport_scheduler.authority_snapshot.usb_state != ERA_AUTH_USB_NO_HOST) {
        return true;
    }

    if (g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_HOST_PEER_PEER) {
        return era_matrix_engine_local_matrix_ready() && !era_matrix_engine_local_changed();
    }

    return g_era_split_transport_scheduler.mode != ERA_SPLIT_MODE_LOCAL_NO_LINK || era_matrix_engine_local_matrix_ready();
}

static inline void __attribute__((always_inline)) era_split_transport_scheduler_sync_peer_matrix_projection(void) {
    (void)era_matrix_engine_sync_peer_projection(g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_HOST_PEER_HOST);
}

static inline void __attribute__((always_inline)) era_split_transport_scheduler_publish_host_peer_responder_visual_snapshot(void) {
    /* Armed for the HOST-PEER HOST unconditionally, and for the DUAL-HOST
       Right behind the RGB policy's sender arm (Slice 14) -- the visual
       baseline is the RGB family's reactive half, so it rides the same bit
       the config sections ride. */
    bool visual_armed = g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_HOST_PEER_HOST;
    if (!visual_armed && g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_DUAL_HOST_RIGHT) {
        /* The cached bit, never the policy module's atomic read: this runs
           per matrix scan ahead of the idle guard, which is exactly the
           class the cache exists for. The DIRTY_SYNC_POLICY edge refreshes
           it. */
        visual_armed = g_era_split_transport_scheduler.rgb_sync_requested_cached;
    }
    if (!visual_armed) {
        if (g_era_split_transport_scheduler.host_peer_responder_visual_snapshot_published) {
            era_host_peer_transaction_clear_responder_visual_snapshot();
        }
        g_era_split_transport_scheduler.host_peer_responder_visual_snapshot_published = false;
        return;
    }
    if (g_era_split_transport_scheduler.host_peer_responder_visual_snapshot_published && !era_matrix_engine_local_changed()) {
        return;
    }

    if (era_host_peer_source_snapshot_publish_visual()) {
        g_era_split_transport_scheduler.host_peer_responder_visual_snapshot_published = true;
        /* Change-driven by construction: this arm is guarded by local_changed
           and the first-publish flag, so the mark's rate is bounded by the
           local key-edge rate. */
        era_split_transport_scheduler_mark_responder_snapshot_due();
    }
}

static void era_split_transport_scheduler_publish_host_peer_responder_rgb_state(uint32_t now_ms) {
    /* The responder-role term (Slice 12). This was a HOST_PEER_HOST equality,
       which was that gate's whole meaning while HOST-PEER was the only
       relation whose responder carried RGB; the DUAL-HOST Right is the other
       responder now, behind the policy gate's sender arm -- its own RGB
       requested bit gates the arming -- and with the sleep bit zeroed at
       capture. Eligibility alone deliberately does not reach this arming:
       the snapshot is published only where the role and the policy both say
       so, and a cleared snapshot is what the plan-side eligibility clip
       already expects. */
    bool responder_rgb_armed = false;
    bool include_sleep       = false;
    if (g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_HOST_PEER_HOST) {
        responder_rgb_armed = true;
        include_sleep       = true;
    } else if (g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_DUAL_HOST_RIGHT) {
        bool rgb_requested = false;
        responder_rgb_armed = era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_RGB, &rgb_requested) && rgb_requested;
    }
    if (!responder_rgb_armed) {
        if (g_era_split_transport_scheduler.host_peer_responder_rgb_state_published) {
            era_host_peer_transaction_clear_responder_rgb_state();
        }
        g_era_split_transport_scheduler.host_peer_responder_rgb_state_published              = false;
        g_era_split_transport_scheduler.host_peer_responder_rgb_state_publish_deadline_valid = false;
        return;
    }

    if (g_era_split_transport_scheduler.host_peer_responder_rgb_state_publish_deadline_valid &&
        !timer_expired32(now_ms, g_era_split_transport_scheduler.host_peer_responder_rgb_state_publish_deadline_ms)) {
        return;
    }

    if (!era_host_peer_source_snapshot_publish_rgb_state(include_sleep)) {
        if (g_era_split_transport_scheduler.host_peer_responder_rgb_state_published) {
            era_host_peer_transaction_clear_responder_rgb_state();
        }
        g_era_split_transport_scheduler.host_peer_responder_rgb_state_published              = false;
        g_era_split_transport_scheduler.host_peer_responder_rgb_state_publish_deadline_valid = false;
        return;
    }

    g_era_split_transport_scheduler.host_peer_responder_rgb_state_published              = true;
    g_era_split_transport_scheduler.host_peer_responder_rgb_state_publish_deadline_valid = true;
    g_era_split_transport_scheduler.host_peer_responder_rgb_state_publish_deadline_ms    = now_ms + ERA_HOST_PEER_RGB_STATE_SNAPSHOT_PUBLISH_PERIOD_MS;
}

static inline bool __attribute__((always_inline)) era_split_transport_scheduler_peer_matrix_projection_scan_idle(void) {
    return era_matrix_engine_peer_projection_scan_idle(g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_HOST_PEER_HOST);
}

static inline bool __attribute__((always_inline)) era_split_transport_scheduler_scan_idle(void) {
    return g_era_split_transport_scheduler.route_due_flags == 0 && era_split_transport_scheduler_local_matrix_publish_scan_idle() && era_split_transport_scheduler_peer_matrix_projection_scan_idle();
}

void era_split_transport_scheduler_init(void) {
    if (g_era_split_transport_scheduler.initialized) {
        return;
    }

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    g_era_split_transport_scheduler.init_call_count++;
#endif
    era_split_scheduler_session_init();
    g_era_split_transport_scheduler.mode          = ERA_SPLIT_MODE_LOCAL_NO_LINK;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    g_era_split_transport_scheduler.previous_mode = ERA_SPLIT_MODE_LOCAL_NO_LINK;
#endif
    (void)era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_INPUT,
                                              &g_era_split_transport_scheduler.input_sync_requested_cached);
    (void)era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_RGB,
                                              &g_era_split_transport_scheduler.rgb_sync_requested_cached);
    g_era_split_transport_scheduler.initialized   = true;
    era_split_transport_scheduler_sample_authority(timer_read32(), false);
    era_split_transport_scheduler_update_mode();
    era_split_transport_scheduler_refresh_route_due_flags();
    era_split_transport_scheduler_update_next_deadline();
}

/* The one boot entry point that opens the wire. Called from
 * era_split_keyboard_post_init(), which is keyboard_post_init_kb - after
 * matrix_init(), after quantum_init()/eeconfig_init(), after rgb_matrix_init(),
 * and after era_host_peer_storage_init() has read the seven domains. It is
 * strictly later than keyboard_setup(), so the boot-ordering invariant in
 * era_sram_residency_contract.md (no core1 launch before the wear-leveling init
 * consolidation and the hardware-id read) holds with a margin instead of by
 * accident.
 *
 * The ensure_core1() calls that remain on the scan and lane paths are re-arms
 * of an already-launched core, not second boot launches: no matrix scan and no
 * housekeeping pass runs inside keyboard_init(), so this is the first of them
 * to execute on any boot.
 *
 * Returns whether the wire came up. Nothing at the call site acts on it, and
 * that is now an answer rather than a gap. The snapshot invalidation below is
 * the whole of the failure handling; a give-up state at the caller would need
 * a rule for when to resume, and no observed failure has asked for one. What
 * the launch-robustness work bounded is inside the handshake itself, in
 * era_split_communication_core_launch_sequence(). */
bool era_split_transport_scheduler_start_communication_core(void) {
    if (g_era_split_transport_scheduler.communication_core_started) {
        return true;
    }
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    g_era_split_transport_scheduler.communication_core_start_entry_ms = era_split_transport_scheduler_boot_ms();
#endif

    if (!g_era_split_transport_scheduler.initialized) {
        era_split_transport_scheduler_init();
    } else {
        /* One authority re-sample and re-plan before the wire opens. The init
           steps between the scheduler's first policy pass and this point are
           exactly the window in which a cabled half finishes USB enumeration,
           and planning here is free: it costs one reduction and one plan, and
           it keeps the first lease from being one the next housekeeping pass
           would tear straight down. Both still run with the wire closed. */
        (void)era_split_transport_scheduler_sample_authority(timer_read32(), false);
        (void)era_split_transport_scheduler_update_mode();
    }

    g_era_split_transport_scheduler.communication_core_started = true;

    /* Boot Low reaches the backend here, once, before the wire has ever been
       opened. The stored level stays in the link unit; the raise after the
       relation opens is a later apply_link_level. Core0 is what may read
       EEPROM and the backend is what owns the PIO divider. The level is
       translated to a baud on this side: the backend takes a number and never
       learns that levels exist. */
    era_split_transaction_backend_set_speed(era_split_link_speed(era_split_link_active_level()));

    bool started = era_split_transport_scheduler_reset_serial_for_transport_role(
        g_era_split_transport_scheduler.local_wire_available,
        g_era_split_transport_scheduler.local_wire_initiator,
        true);
    if (!started) {
        /* Invalidating the snapshot is what actually arms the retry, and the
           dirty flag alone would not. update_mode() only reaches the serial
           reset on an authority, mode or wire-role edge, and after a failed
           launch none of the three has moved - the plan is still correct, it
           just did not take. era_split_transport_scheduler_authority_changed()
           reports true on an invalid snapshot, so the next housekeeping pass
           takes the full path and retries through the ordinary runtime route
           rather than through a second launch site. This replaces the retry the
           ensure_core1() call in transport_master_init()/transport_slave_init()
           used to provide. */
        g_era_split_transport_scheduler.authority_snapshot_valid = false;
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE);
    } else if (g_era_split_transport_scheduler.local_wire_available && !g_era_split_transport_scheduler.local_wire_initiator) {
        (void)era_split_transport_scheduler_publish_communication_core_responder_snapshot();
    }

    (void)era_split_transport_scheduler_refresh_route_due_flags();
    era_split_transport_scheduler_update_next_deadline();
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    g_era_split_transport_scheduler.communication_core_start_exit_ms = era_split_transport_scheduler_boot_ms();
#endif
    return started;
}

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
static inline bool era_split_transport_scheduler_note_maintenance_source(uint8_t source, bool performed) {
    if (performed) {
        g_era_split_transport_scheduler.maintenance_source_count[source]++;
    }
    return performed;
}
#    define ERA_SPLIT_SCHEDULER_MAINT(source, expr) era_split_transport_scheduler_note_maintenance_source((source), (expr))
#else
#    define ERA_SPLIT_SCHEDULER_MAINT(source, expr) (expr)
#endif

/* The two halves of this entry point are two jobs, and they run at rates three
   orders of magnitude apart: the gate below decides on every matrix scan pass,
   the body it guards runs about a hundred times a second. Keeping them one
   function made the scan-rate half carry the maintenance half's frame -- 140
   bytes of stack claimed on every quiet pass for a `storage_context` only the
   body builds -- and made the file's largest function two unrelated things.
   The split is the same one T9 made to the gate's own predicate, finished. */
static bool era_split_transport_scheduler_housekeeping_body(uint32_t now_ms) __attribute__((noinline));

bool era_split_transport_scheduler_task(void) {
    /* The inline guard the other three entry points already use. This one
       called `..._init()` outright, so every pass paid a call, a prologue and
       an epilogue to read one already-set flag -- and this entry is the one
       that runs at the scan rate, which the other three do not. Measured
       2026-08-16: the whole of this function's unconditional path is 2.9 us on
       a 24 us pass, and this was 0.18 of it. */
    era_split_transport_scheduler_ensure_initialized();
    /* T9: the clock is read only once this pass has a reason to want it. The
       event arms answer without it; the deadline arm answers from the raw
       counter, at a load and a compare against `timer_read32()`'s ~0.42 us.
       The millisecond compare is still the authority on whether the pass runs,
       so an early raw stamp costs one clock read and moves no cadence. */
    uint32_t now_ms;
    if (era_split_transport_scheduler_housekeeping_event_due()) {
        now_ms = timer_read32();
    } else if (era_split_transport_scheduler_deadline_raw_reached()) {
        now_ms = timer_read32();
        if (!timer_expired32(now_ms, g_era_split_transport_scheduler.next_scheduler_deadline_ms)) {
            return false;
        }
    } else {
        return false;
    }
    return era_split_transport_scheduler_housekeeping_body(now_ms);
}

static bool era_split_transport_scheduler_housekeeping_body(uint32_t now_ms) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    g_era_split_transport_scheduler.housekeeping_entry_count++;
#endif

    bool maintenance_performed = false;
    bool authority_changed      = false;
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    maintenance_performed = ERA_SPLIT_SCHEDULER_MAINT(ERA_SPLIT_SCHEDULER_MAINT_SOURCE_STORAGE, era_host_peer_storage_task(now_ms));
#endif
    maintenance_performed = ERA_SPLIT_SCHEDULER_MAINT(ERA_SPLIT_SCHEDULER_MAINT_SOURCE_RESPONDER_DRAIN, era_split_transport_scheduler_drain_communication_core_responder_results()) || maintenance_performed;
    maintenance_performed = ERA_SPLIT_SCHEDULER_MAINT(ERA_SPLIT_SCHEDULER_MAINT_SOURCE_STANDING_STATE, era_split_transport_scheduler_apply_standing_state()) || maintenance_performed;
    era_split_transport_scheduler_publish_host_peer_responder_rgb_state(now_ms);
    /* R7.1: an in-flight initiator request that core1 has neither consumed
       nor expired for the whole unresponsive bound is the request-lane face
       of a dead core1 — the request's publish-relative, wire-scaled freshness
       window is core1-enforced, so without this nothing ages it out and the rotting latch also holds the
       ATTACH_STATUS probe closed. A ready result is excluded because it is
       the opposite evidence: core1 answered and only core0's own drain is
       late (the sliced durable apply stalls this task for measured seconds),
       and the poll below drains it this same pass. Marking wire-role dirty
       is the launch-failure retry's own mechanism — the dirty-and-pending
       gate below cancels through the quiesce path this same pass, and that
       transfer meets R7's judgment (declare-dead, relaunch or cap). The
       peer-stale latch beside it is what makes the replan unconditional: a
       request sent that got no answer is this codebase's named peer-stale
       evidence class, and without the latch the cancel's own early returns
       can retire the lease before the silence watch ever latches, stranding
       the half in its old mode with a capped wire. */
    if (g_era_split_transport_scheduler.core1_initiator_async_pending &&
        !era_split_communication_core_initiator_result_ready() &&
        timer_elapsed32(g_era_split_transport_scheduler.core1_initiator_pending_since_ms) >= ERA_SPLIT_CORE1_UNRESPONSIVE_MS) {
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE);
        g_era_split_transport_scheduler.peer_session_stale = true;
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_PEER_STALE);
    }
    if (era_split_transport_scheduler_read_pending_dirty_flags() != 0 && era_split_transport_scheduler_core1_initiator_pending()) {
        if (!era_split_transport_scheduler_cancel_core1_initiator()) {
            return false;
        }
        maintenance_performed = ERA_SPLIT_SCHEDULER_MAINT(ERA_SPLIT_SCHEDULER_MAINT_SOURCE_CORE1_INITIATOR, true);
    } else {
        maintenance_performed = ERA_SPLIT_SCHEDULER_MAINT(ERA_SPLIT_SCHEDULER_MAINT_SOURCE_CORE1_INITIATOR, era_split_transport_scheduler_poll_core1_initiator()) || maintenance_performed;
    }
    maintenance_performed      = ERA_SPLIT_SCHEDULER_MAINT(ERA_SPLIT_SCHEDULER_MAINT_SOURCE_TIME_TOKENS, era_split_transport_scheduler_refresh_time_due_tokens(now_ms)) || maintenance_performed;

    uint8_t maintenance_due_flags = era_split_transport_scheduler_consume_maintenance_due_flags();
    if ((maintenance_due_flags & ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE) != 0) {
        authority_changed = era_split_transport_scheduler_sample_authority(now_ms, true);
    }
    uint8_t pending_dirty_flags = era_split_transport_scheduler_consume_pending_dirty_flags();
    if ((pending_dirty_flags & ERA_SPLIT_SCHEDULER_DIRTY_SYNC_POLICY) != 0) {
        era_split_transport_scheduler_note_sync_policy_edge();
    }
    if (pending_dirty_flags != 0) {
        maintenance_performed = ERA_SPLIT_SCHEDULER_MAINT(ERA_SPLIT_SCHEDULER_MAINT_SOURCE_MODE, era_split_transport_scheduler_update_mode()) || maintenance_performed;
    }

    /* The agreed restart runs here rather than on the scan path, and the
       cadence is not a compromise: its own bounds are the arm timeout and the
       commit delay, both tens to hundreds of milliseconds wide against a
       housekeeping pass the authority sample already brings round every 10 ms.
       Putting it on the scan path would spend per-pass work on a state machine
       that is idle for the life of most boots. */
    era_split_restart_agreement_task();
    /* Beside it and not inside it: the agreement owns the mechanism and the
       link lane owns what a level means. Reconciliation's open half runs here
       -- the winner may raise, the loser waits, and a live raise adopts. */
    era_split_link_task();

    /* The listener's step, evaluated after the mode has settled this pass so
       the relation and wire-role terms it reads are this pass's answers and
       not the previous one's. */
    uint8_t link_step_level;
    if (era_split_link_step_due(&link_step_level)) {
        maintenance_performed = ERA_SPLIT_SCHEDULER_MAINT(ERA_SPLIT_SCHEDULER_MAINT_SOURCE_MODE, era_split_transport_scheduler_apply_link_step(link_step_level, true)) || maintenance_performed;
    }

#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    era_split_sync_policy_snapshot_t policy;
    era_split_scheduler_session_diagnostics_t session;
    era_split_sync_policy_get_snapshot(&policy);
    era_split_scheduler_session_get_diagnostics_snapshot(&session);
    era_split_transaction_backend_role_t owner_role = era_split_communication_core_owner_current_role();
    bool owner_ready = era_split_communication_core_owner_current() == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1 &&
                       owner_role == (g_era_split_transport_scheduler.local_wire_initiator ?
                                          ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR :
                                          ERA_SPLIT_TRANSACTION_BACKEND_ROLE_RESPONDER);
    era_host_peer_storage_runtime_context_t storage_context = {
        .now_ms                    = now_ms,
        .owner_epoch               = era_split_communication_core_owner_epoch(),
        .relation_generation       = g_era_split_transport_scheduler.core1_initiator_relation_generation,
        .policy_generation         = policy.eeprom_policy_generation,
        .peer_usb_epoch            = session.peer_usb_epoch,
        .peer_host_open_generation = session.peer_host_open_generation,
        .peer_host_close_generation = session.peer_host_close_generation,
        .mode                      = (uint8_t)g_era_split_transport_scheduler.mode,
        .owner_ready               = owner_ready ? 1 : 0,
        .local_initiator           = g_era_split_transport_scheduler.local_wire_initiator ? 1 : 0,
        .general_initiator_pending = era_split_transport_scheduler_core1_initiator_pending() ? 1 : 0,
        .status_revalidation_due   = era_split_transport_scheduler_local_status_revalidation_due() ? 1 : 0,
        .local_policy_requested    = policy.requested[ERA_SPLIT_SYNC_POLICY_FIELD_EEPROM],
        .local_bulk_page_supported = session.local_bulk_page_supported,
        .peer_known                = session.peer_known,
        .peer_host_open            = session.peer_accepted_host_open,
        .peer_no_host              = session.peer_accepted_no_host,
        .peer_bulk_page_supported  = session.peer_bulk_page_supported,
        .local_left                = g_era_split_transport_scheduler.authority_snapshot.valid &&
                                             g_era_split_transport_scheduler.authority_snapshot.is_left ?
                                         1 :
                                         0,
    };
    maintenance_performed = era_host_peer_storage_runtime_task(&storage_context) ||
                            maintenance_performed;
#endif
    /* One recompute, after everything above that can move a due fact. It sat
       twice: once before the storage block and once at its end, with the first
       write unconditionally overwritten by the second — the storage engine
       names `route_due_flags` nowhere and its two scheduler callbacks write
       that word without reading it. Deleting the earlier call outright would
       have been wrong, because it was outside the `#ifdef` and was the only
       one on a build with storage disabled; moving the survivor out is what
       leaves exactly one in both configurations. */
    maintenance_performed = ERA_SPLIT_SCHEDULER_MAINT(ERA_SPLIT_SCHEDULER_MAINT_SOURCE_ROUTE_DUE, era_split_transport_scheduler_refresh_route_due_flags()) || maintenance_performed;
    if (g_era_split_transport_scheduler.route_due_flags == 0) {
        era_split_transport_scheduler_clear_owner_route();
    }
    /* Advisory return: a failed differing publish re-arms the due latch
       inside the callee and retries next pass, so this site need not care. */
    (void)era_split_transport_scheduler_publish_communication_core_responder_snapshot();
    /* The backstop publish. The scan-path one below catches a layer edge in
       about one scan; this one catches everything else -- a mode change, a
       relation rotation, an exclusivity edge, a status revalidation opening or
       closing -- without any of them needing to know the plan exists. It is
       free when nothing moved, because the publish compares first. */
    (void)era_split_transport_scheduler_publish_standing_plan();
    era_split_transport_scheduler_update_next_deadline();
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    if (maintenance_performed) {
        g_era_split_transport_scheduler.housekeeping_task_count++;
    }
#endif
    return authority_changed;
}

static void era_split_transport_scheduler_local_initiator_step(void) {
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    /* A Core1 service is non-preemptive once begun. While the dedicated
     * storage request/result reservation is live, keep both generic requests
     * outside their publish-relative freshness window. Their due facts remain
     * in their owners, so the next transaction boundary reselects mandatory
     * status first and matrix later. Between storage requests, the ordinary
     * exclusive mask still admits status and suppresses only normal traffic. */
    if (era_host_peer_storage_initiator_request_pending()) {
        g_era_split_transport_scheduler.route_due_flags = 0;
    } else if (era_host_peer_storage_route_exclusive()) {
        g_era_split_transport_scheduler.route_due_flags &= ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS;
    }
#endif
    if (g_era_split_transport_scheduler.route_due_flags == 0) {
        era_split_transport_scheduler_clear_owner_route();
        return;
    }
    if (!era_split_transport_scheduler_initiator_route_available()) {
        return;
    }

    if (!era_split_communication_core_owner_ensure_core1()) {
        return;
    }
    era_split_transport_scheduler_select_owner_route();
    era_split_transport_scheduler_execute_owner_route();
    if (g_era_split_transport_scheduler.last_owner_route != ERA_SPLIT_ROUTE_NONE) {
        era_split_transport_scheduler_refresh_route_due_flags();
        if (g_era_split_transport_scheduler.route_due_flags == 0) {
            era_split_transport_scheduler_clear_owner_route();
        }
        era_split_transport_scheduler_update_next_deadline();
    }
}

void        era_split_transport_scheduler_transport_step(void) __attribute__((aligned(16)));
static bool era_split_transport_scheduler_transport_slow(void) __attribute__((noinline));
static bool era_split_transport_scheduler_transport_slow(void) {
    bool local_matrix_first_ready = false;
    bool local_matrix_changed     = era_split_transport_scheduler_publish_local_matrix_if_needed(&local_matrix_first_ready);
    if (local_matrix_changed || local_matrix_first_ready) {
        era_split_transport_scheduler_note_local_matrix_publish(local_matrix_changed, local_matrix_first_ready);
    }

    (void)era_split_transport_scheduler_publish_communication_core_responder_snapshot();
    era_split_transport_scheduler_sync_peer_matrix_projection();

    if (!era_split_transport_scheduler_local_initiator_available()) {
        return true;
    }

    /* A layer edge reaches the wire from here, in about one scan. The layer
       hook marks the route due, scan_idle() fails on the next pass, and this
       publishes the new byte; core1 sends it on its next loop pass without
       waiting for its period, because a differing body outranks the cadence.
       That is the same "dirty is immediate and carries no cadence" treatment
       matrix dirty gets, moved to the publication rather than to a route.

       Gated on the sections it exists for rather than on the relation, because
       that is the fact it actually depends on. A relation whose plan carries
       none of INPUT_LAYER, ACTIVITY, or the visual baseline (Slice 14 -- the
       third plan field a scan moves) has no field a scan can move --
       HOST-PEER's every plan field is produced at housekeeping cadence -- so
       publishing here would cost that relation a plan build and compare per
       key event and buy nothing, on the half whose keys ride the wire. The
       housekeeping backstop still catches everything, within the authority
       sample's own deadline. */
    if ((era_split_wire_eligible_sections((uint8_t)g_era_split_transport_scheduler.mode,
                                          ERA_SPLIT_WIRE_SECTION_DIRECTION_PUSH) &
         (ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER | ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY |
          ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL)) != 0) {
        (void)era_split_transport_scheduler_publish_standing_plan();
    }

    era_split_transport_scheduler_local_initiator_step();
    return true;
}

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
static inline void __attribute__((always_inline)) era_split_transport_scheduler_note_transport_step_call(void) {
    g_era_split_transport_scheduler.transport_step_call_count++;
}
#else
static inline void __attribute__((always_inline)) era_split_transport_scheduler_note_transport_step_call(void) {}
#endif

/* The scan path's whole transport step, one body for every half. The
   master/slave pair this replaces differed only in which diagnostics counter
   it stepped and whether it returned transport_slow()'s bool - a return every
   caller discarded - and the is_keyboard_master() branch that chose between
   them is gone with it (era_rp2040_matrix_core.c). Role belongs to the paths
   inside: the responder-visual publish and the projection read the mode, the
   initiator step reads the wire role, and each acts only where its relation
   arms it. */
void era_split_transport_scheduler_transport_step(void) {
    era_split_transport_scheduler_ensure_initialized();
    era_split_transport_scheduler_note_transport_step_call();
    era_split_transport_scheduler_publish_host_peer_responder_visual_snapshot();
    if (era_split_transport_scheduler_scan_idle()) {
        return;
    }

    (void)era_split_transport_scheduler_transport_slow();
}
