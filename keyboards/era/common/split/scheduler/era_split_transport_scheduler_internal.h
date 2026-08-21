// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../era_split_authority_reducer.h"
#include "../era_split_mode_planner.h"
/* For the maintenance-source indices: the state below sizes an array from them
   and the accessor that reads it is public, so the constants live there. */
#include "../era_split_transport_scheduler.h"
#include "../era_split_transport_scheduler_diagnostics.h"
#include "../era_split_wire_router.h"

#ifndef ERA_SPLIT_WIRE_BOOTSTRAP_PERIOD_MS
#    define ERA_SPLIT_WIRE_BOOTSTRAP_PERIOD_MS 25
#endif
/* Consecutive ATTACH_STATUS misses after which peer-unknown discovery stops
   probing at the bootstrap period and falls back to the slow status period. */
#ifndef ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER
#    define ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER 10
#endif
/* The peer-unknown discovery backoff target: after the backoff threshold
   above, a half with no confirmed peer probes at this period instead of the
   25 ms bootstrap period. Known-relation liveness always runs at
   ERA_SPLIT_SESSION_REFRESH_PERIOD_MS; the HOST-HOST slow-liveness caller
   this constant once served retired with the DUAL-HOST parent (Slice 9.5). */
#ifndef ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS
#    define ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS 500
#endif
#ifndef ERA_SPLIT_SESSION_REFRESH_PERIOD_MS
#    define ERA_SPLIT_SESSION_REFRESH_PERIOD_MS 50
#endif
/* The HOST-PEER standing exchange's period. It was the Slice 7.2 HOST-source
   response poll's period and keeps the name, because R2 handed the same period
   to core1 rather than changing it and that relation's wire cadence did not
   move across the slice.

   It moves here, to 10 ms, by owner decision 2026-08-09, which discharges the
   recorded "tuning below 20 ms" backlog entry. The arithmetic it was decided
   against is the same 187 us/poll core1 model the DUAL-HOST block below
   states: 20 ms was 0.94% of core1's saturation and 10 ms is 1.87%, and the
   ~0.80 ms worst-case exchange is 8% of the period rather than 4%. What it
   buys is the responder's boarding latency, which this period bounds exactly
   as the DUAL-HOST one does -- mean 5 ms and worst 10 ms, both halved.

   Two consequences do not read off the period, and they are why this constant
   now carries a comment at all. ERA_HOST_PEER_RGB_STATE_SNAPSHOT_PUBLISH_-
   PERIOD_MS below follows it, and since Slice 12 that publish also arms on the
   DUAL-HOST Right, so this value halves a DUAL-HOST responder's RGB snapshot
   republish too -- and that deadline is the one core0 wake still tracking a
   poll period in either relation. Separately, the responder's general result
   ring holds three undrained results, so the core0 stall a responder can cover
   before it answers a poll with nothing is three periods: 60 ms becomes 30 ms
   (era_split_communication_core_responder_service.c). Neither is a bound a
   build can check, and both are readings this change owes the device. */
#ifndef ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS
#    define ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS 10
#endif
/* The DUAL-HOST runtime cadence: one period, run unconditionally once the
   relation is a confirmed DUAL-HOST (owner decision 2026-08-01). There is no
   activity window, no quiet rate and no hint; a local section going dirty is
   still immediate and still carries no cadence.

   The value was a placeholder at 5 ms on the ground that the wire's real
   saturation had never been measured. It has been now: a poll costs 187 us of
   core1 end to end, so core1 saturates near 5.3 kHz, and the 5 ms period sat
   at 3.7% of that. 2 ms was 9.35%. 1 ms, by owner decision 2026-08-09, is
   18.7% -- about five times clear rather than the order of magnitude the
   earlier values had, which makes the margin something to measure instead of
   something to quote.

   The bound that shrank with it is not core1's mean load but the period's own
   room, and that is the part no assert can see. The due deadline is anchored
   to the START of an exchange rather than its end
   (era_split_communication_core_standing.c), so the period holds only while an
   exchange fits inside it: at 2 ms a ~0.80 ms worst-case exchange still left
   1.2 ms of guaranteed idle, and at 1 ms it leaves 0.2 ms. Nothing clamps the
   far side -- an exchange that overruns the period arms a deadline already in
   the past, and the core1 loop re-fires with no rate limit anywhere in the
   path, so the cadence stops being a cadence rather than degrading. Every
   assert on a poll period below is an upper bound, so shrinking one can never
   fail a build. This is the first thing the device legs for this value watch.

   What 1 ms buys is one direction only, and the asymmetry is the reason to
   state it here. The initiator can speak whenever it likes -- since the plan
   publish sends an event, its layer reaches the wire in about a scan whatever
   this period is. The responder cannot: the wire is half duplex and the
   initiator owns it, so a responder-side change waits for the next poll and
   nothing else. This period IS the responder's latency bound, and it bounds
   layer propagation only, because in DUAL-HOST no key crosses the wire. */
#ifndef ERA_SPLIT_DUAL_RUNTIME_POLL_MS
#    define ERA_SPLIT_DUAL_RUNTIME_POLL_MS 1
#endif
/* The responder's silence limit: no accepted frame for this long forgets the
   session. The value is unchanged at 100 ms and what changed is where it comes
   from. It used to be twice ERA_SPLIT_SESSION_REFRESH_PERIOD_MS, because that
   beat was what fed a quiet relation; Slice 11.6 gives each relation a lane of
   its own and stops the post-relation beat, so that derivation has no premise
   left and the constant stands on its own.

   It is what the poll periods below are now held against: a responder is fed by
   the relation's own traffic, so the limit must sit comfortably above whichever
   period that relation polls at. */
#ifndef ERA_SPLIT_RESPONDER_SILENCE_MS
#    define ERA_SPLIT_RESPONDER_SILENCE_MS 100
#endif
/* Nonzero fixes the window instead of taking the constant above. */
#ifndef ERA_SPLIT_SESSION_STALE_MS
#    define ERA_SPLIT_SESSION_STALE_MS 0
#endif
/* The standing grant's liveness deadline: how long core1 lets the wire stay
   quiet before it runs one section-less exchange, whatever the enable bit says
   (era_route_contract.md). Derived from the same margin the poll periods are
   asserted against rather than chosen, because it answers the same question --
   what a half must clear so the other half's silence watch never fires on a
   living link. It is not a cadence: it fires only where nothing else is
   crossing, which since Slice 11.7 means the durable apply window and nothing
   else.

   It was called ERA_SPLIT_DUAL_RUNTIME_LIVENESS_MS while DUAL-HOST was the only
   relation with a standing grant, and the name was already narrower than the
   value: nothing in the derivation above is per relation, because the silence
   limit it halves is a constant of both. R2 gives HOST-PEER the same grant at
   its own poll period and this same deadline. The poll period keeps its
   relation in its name because that one really is per relation. */
#ifndef ERA_SPLIT_STANDING_LIVENESS_MS
#    define ERA_SPLIT_STANDING_LIVENESS_MS (ERA_SPLIT_RESPONDER_SILENCE_MS / 2)
#endif
/* R7.1: how long core0 lets its own core1 sit without observable progress
   before treating the lease as dead territory. One constant covers both
   faces of the same failure — a standing exchange whose count stops moving,
   and an in-flight initiator request that is neither consumed nor expired —
   and it is deliberately the responder's silence limit rather than a new
   number: the two watches answer the same question from the two ends of the
   wire, and every cadence already asserted against that limit (the polls,
   the standing liveness beat) is thereby inside this one too. The R7 kill
   leg is why it exists: the responder watch is fed by arriving traffic, so
   the half whose core1 *initiates* had no detector at all when that core1
   died — it idled on a re-blessed dead lease until an external edge. */
#ifndef ERA_SPLIT_CORE1_UNRESPONSIVE_MS
#    define ERA_SPLIT_CORE1_UNRESPONSIVE_MS ERA_SPLIT_RESPONDER_SILENCE_MS
#endif
/* The authority sample is the architecture's one poll, and since Slice 11.6 its
   period is derived rather than chosen: the longest interval that cannot step
   over the freshness window it serves. Detection of a stopped SOF counter costs
   the window plus up to one period -- 10 to 20 ms here -- and the number has a
   name instead of a value. Core0's empty wake rate halving is a consequence and
   not the reason. */
#ifndef ERA_SPLIT_AUTHORITY_POLL_PERIOD_MS
#    define ERA_SPLIT_AUTHORITY_POLL_PERIOD_MS ERA_SPLIT_AUTHORITY_SOF_FRESH_MS
#endif
#ifndef ERA_HOST_PEER_RGB_STATE_SNAPSHOT_PUBLISH_PERIOD_MS
#    define ERA_HOST_PEER_RGB_STATE_SNAPSHOT_PUBLISH_PERIOD_MS ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS
#endif

_Static_assert(ERA_SPLIT_DUAL_RUNTIME_POLL_MS > 0, "The DUAL-HOST runtime poll period must be positive; a zero period is an unbounded poll, not a disabled one.");
/* This replaces an assert that held the runtime poll against the SESSION_STATUS
   period, on the ground that a slower poll bought nothing because the status
   beat would already have carried the round trip. Slice 11.6 removed that beat,
   so the premise is gone; what a poll period actually has to clear is the
   silence limit of the half answering it, since the relation's own traffic is
   now the only thing feeding that watch. Doubling is the same margin the old
   window carried, stated where it means something. */
_Static_assert(ERA_SPLIT_DUAL_RUNTIME_POLL_MS * 2 <= ERA_SPLIT_RESPONDER_SILENCE_MS, "A DUAL-HOST runtime poll that is not comfortably inside the responder's silence limit makes one missed poll a forgotten session.");
_Static_assert(ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS * 2 <= ERA_SPLIT_RESPONDER_SILENCE_MS, "A HOST-source response poll that is not comfortably inside the responder's silence limit makes one missed poll a forgotten session.");
_Static_assert(ERA_SPLIT_STANDING_LIVENESS_MS > 0 && ERA_SPLIT_STANDING_LIVENESS_MS * 2 <= ERA_SPLIT_RESPONDER_SILENCE_MS, "The standing liveness deadline must leave the peer's silence watch a full period of margin, or the beat that exists to stop a forgotten session is what causes one.");
_Static_assert(ERA_SPLIT_STANDING_LIVENESS_MS * 2 <= ERA_SPLIT_CORE1_UNRESPONSIVE_MS, "The initiator silence watch must sit comfortably above the standing liveness beat, or the storage-exclusivity phase's slowest legitimate exchange gap reads as a dead core1.");
_Static_assert(ERA_SPLIT_AUTHORITY_POLL_PERIOD_MS <= ERA_SPLIT_AUTHORITY_SOF_FRESH_MS, "Sampling slower than the freshness window can step over it entirely.");
_Static_assert(ERA_SPLIT_SESSION_REFRESH_PERIOD_MS <= UINT16_MAX, "ERA split refresh period exceeds cached status period width.");
_Static_assert(ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS <= UINT16_MAX, "ERA split HOST-source response poll period exceeds cached route period width.");
_Static_assert(ERA_HOST_PEER_RGB_STATE_SNAPSHOT_PUBLISH_PERIOD_MS <= UINT16_MAX, "ERA split HOST-PEER RGB state snapshot period exceeds cached period width.");

typedef struct {
    bool initialized;
    bool authority_snapshot_valid;

    bool     attach_status_last_tx_valid;
    uint32_t attach_status_last_tx_ms;
    bool     local_status_pending;
    uint8_t  attach_status_miss_streak;
    bool     peer_session_stale;
    bool     responder_relation_rx_observed;
    uint32_t responder_relation_rx_observed_count;
    uint32_t responder_accepted_rx_observed_count;
    uint32_t responder_relation_rx_observed_ms;
    /* R7.1: the initiator silence watch's observation triple, the exact twin
       of the responder triple above — the wire-anchored progress counter is
       the standing exchange's own count instead of the accepted-RX count,
       because on the initiator half "progress" is exchanges completing rather
       than frames arriving. */
    bool     initiator_progress_observed;
    uint32_t initiator_progress_observed_count;
    uint32_t initiator_progress_observed_ms;
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    bool     storage_result_watch_active;
#endif
    /* `host_peer_last_tx_valid`/`_ms` retired with the two routes they paced
       (R2). They were the HOST-PEER response-poll and liveness period anchors
       and had no other reader; the matrix push that still writes the wire does
       not run on a period, and core1 now holds the only runtime clock this
       relation has. Traced rather than assumed: the four readers were the two
       due predicates and the two deadlines, all in this scheduler's timing
       unit, and all four left with them. */
    uint32_t host_peer_visual_snapshot_rx_count;
    uint32_t host_peer_rgb_state_rx_count;
    /* The third of the six response sections that now counts on the applying
       half. AUTHORITY and RGB were the two that rested on the sender's tx
       counters plus observed behaviour, which is a waiver rather than a reading;
       RGB always had this counter and could not be printed, and AUTHORITY had
       none. Both are on `wire sect` now, which is the line a promoted PEER can
       actually read. */
    uint32_t host_peer_authority_rx_count;
    /* The INPUT-class family's apply leg (storage version 4), the counter
       that makes the layer byte readable the way app=rgb made RGB readable:
       distinct applied peer-layer values, counted inside the policy gate. */
    uint32_t host_peer_input_layer_apply_count;
    /* The INPUT and RGB policy bits as last consumed, so the
       DIRTY_SYNC_POLICY edge hook can tell a toggle from an unrelated policy
       change -- and so the scan-path visual arming reads a cached bool
       instead of taking the policy module's atomic block per scan. */
    bool     input_sync_requested_cached;
    bool     rgb_sync_requested_cached;
    bool     core1_initiator_async_pending;
    /* R7.1: when the pending latch above was set. An in-flight request that
       core1 has neither consumed nor expired for the whole unresponsive
       bound is the request-lane face of a dead core1 — the request's own
       5 ms window is core1-enforced, so nobody else ages it out. */
    uint32_t core1_initiator_pending_since_ms;
    uint8_t  core1_initiator_pending_lane;
    bool     core1_initiator_peer_known_before_request;
    uint16_t core1_initiator_pending_generation;
    uint16_t core1_initiator_request_generation;
    uint16_t core1_initiator_relation_generation;
    /* What core0 keeps of the DUAL-HOST runtime lane, and it is now two words.
       `seq_observed` is the last standing-state sequence this half consumed --
       comparing it is the whole per-scan cost of holding the grant.
       `stop_observed` makes core1's stop an edge on this side, so one failure
       raises one revalidation rather than one per housekeeping pass.

       Everything else went with the machinery it served: the activity window's
       level and one-shot and the peer's remembered hint retired with the
       cadence at Slice 11.5, and the initiator's sent-state shadow moved to
       core1 with the send that confirms it. */
    uint32_t standing_state_seq_observed;
    /* R7.1: whether the last-built standing plan granted core1 a relation at
       all (nonzero relation generation — core1's own acceptance test).
       Cached at the publish so the initiator silence watch arms only where
       exchanges are supposed to be happening, without re-deriving the
       builder's mode and matrix-ready gates. */
    bool     standing_plan_granted;
    /* The visual snapshot core0 last replayed out of the standing state. The
       record's change sequence says *something* moved, not which section, and
       replaying a pressed baseline is an event rather than a level -- so
       without this an authority or storage edge would re-fire every held remote
       key. Compared against `peer_visual_seq`, which core1 bumps only on a
       genuinely new snapshot. */
    uint8_t  standing_visual_seq_applied;
    bool     standing_visual_seq_valid;
    bool     standing_stop_observed;
    /* HOST-PEER's AUTHORITY sent-state shadow, and the in-flight body beside
       it, retired by R2. It lived here only because that relation had no
       standing grant, so core0 planned, submitted and confirmed the send; the
       shadow belongs to whoever confirms, and that is now core1's standing
       service, which already held DUAL-HOST's twin under the identical rule.
       One shadow, one rule, one owner per relation -- and the in-flight copy
       goes with it, because core1 confirms against the body it serialized
       rather than against a live record it would have to re-read. */
    /* How many of core1's unpublished bare-ACK responses this half has already
       folded into its diagnostic projection. A watermark, not a counter. */
    uint32_t responder_quiet_folded;

    bool     local_wire_available;
    bool     local_wire_initiator;
    /* False until era_split_transport_scheduler_start_communication_core() runs
       at keyboard post-init. While it is false the scheduler computes policy
       and opens no wire, which is what keeps the core1 launch a named boot step
       instead of a side effect of the first mode plan. */
    bool     communication_core_started;
    bool     host_peer_responder_visual_snapshot_published;
    bool     host_peer_responder_rgb_state_published;
    bool     host_peer_responder_rgb_state_publish_deadline_valid;
    uint32_t host_peer_responder_rgb_state_publish_deadline_ms;
    bool     authority_poll_deadline_valid;
    uint32_t authority_poll_deadline_ms;
    bool     next_scheduler_deadline_valid;
    uint32_t next_scheduler_deadline_ms;
    /* The same deadline in raw hardware microseconds, stamped so it can never
       land after the millisecond one: the scan-rate task reads it to decide
       whether to read the clock at all, and `next_scheduler_deadline_ms`
       stays the sole authority on whether the pass runs. Written only where
       the millisecond deadline is written
       (`scheduler/era_split_transport_scheduler_timing.c`), and read only
       while `next_scheduler_deadline_valid` is true, so the six sites that
       invalidate the deadline need no second write. */
    uint32_t next_scheduler_deadline_raw_us;
    /* Responder-snapshot due latch: set by change-driven producers (never
       per-poll), consumed only by a successful responder-snapshot publish or
       by a half that does not own the responder role. The deadline
       invalidation beside it makes the first wake immediate; the latch is
       what carries retry across a failed publish. */
    bool     responder_snapshot_publish_due;
    /* The section mask of the last snapshot this half successfully published,
       kept because the flash-write suppression below needs to know whether the
       responder currently advertises anything. Core1's own copy of it is
       diagnostics-gated, and this decision runs in every profile. */
    uint8_t  responder_published_section_mask;
    /* Set for the width of a local flash write block, and the reason it exists
       is that core0 inside a flash program runs no scheduler pass: nothing
       drains a published result and no sent-state shadow retires, so an
       edge-armed response section behaves level-triggered for the whole
       outage. Every arriving poll then needs a result slot, the general ring
       holds three, and the fourth poll is answered with nothing -- which the
       initiator cannot tell from a dead wire. The 2026-08-09 cadence change
       halved the tolerance to three poll periods of 10 ms and 1 ms, and the
       failure itself is device-recorded at the older cadence (`full` and
       `noack` +22/+23 in one session, on a half inside a flash write).

       It suppresses the response *plan* and nothing else, which is the same
       treatment and the same argument storage exclusivity already carries
       below: the slot is still answered, with the one-byte control ACK that
       reserves no slot, and every section stays due because the sent-state
       shadows advance from the wire's own section byte and never from a plan.
       It deliberately covers the durable cross-half apply too -- R4 moved
       exclusivity's end before that write, so the apply is exposed by the same
       mechanism, and the forced housekeeping between its slices republishes
       the sections R4 reopened. */
    bool     responder_flash_write_suppress;
    /* Bracket depth, not a boolean, because `eeprom_write_block` nests: an
       erase inside a write re-enters both hooks, and the sibling bracket two
       lines away (`era_flash_slice_note_window_begin/end`) is depth-counted
       for exactly that reason. A boolean would release the park at the inner
       `end` and leave the rest of the outer write advertising sections. */
    uint8_t  responder_flash_write_depth;
    uint8_t  maintenance_due_flags;

    era_split_mode_t         mode;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    /* The relation this half was in before the current one. It is an
       instrument and not a scheduler input -- nothing decides on it, it prints
       as `prev=` on the `wire pc=` line -- so it is resident only where
       something can read it. */
    era_split_mode_t         previous_mode;
#endif
    era_authority_snapshot_t authority_snapshot;
    era_split_route_kind_t   last_owner_route;
    era_split_route_reason_t last_owner_reason;
    uint8_t                  scheduler_dirty_flags;
    uint8_t                  route_due_flags;

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    /* `initwork`/`hkwork`/`plan` on `wire sched` and `owner` on `wire route`.
       They sat outside this block and were counted in every profile, which put
       four load-add-stores on the plan pass, the owner-route selection and the
       productive housekeeping tail of a release image that has no way to read
       them -- the accretion the gated counters below were written to avoid. */
    uint32_t init_call_count;
    uint32_t housekeeping_task_count;
    uint32_t plan_count;
    uint32_t owner_step_count;
    /* `hkwork` says how often core0 did housekeeping work and not which of the
       seven contributors asked for it, which is the question the SESSION_STATUS
       lane redesign turns on: the 5 ms authority-poll deadline is what lets the
       task body run at all, so a lane change that removes SESSION_STATUS need
       not remove core0's periodic wake. `entry` counts bodies that passed the
       due gate; `src[]` counts, per contributor, the passes it was true on.
       Diagnostics-only and outside the scan path -- the task body is already
       deadline-gated. */
    uint32_t housekeeping_entry_count;
    uint32_t maintenance_source_count[ERA_SPLIT_SCHEDULER_MAINT_SOURCE_COUNT];
    uint32_t dual_runtime_tx_count;
    uint32_t dual_runtime_rx_count;
    uint32_t responder_snapshot_publish_retry_count;
    /* How many flash write blocks began while this half's responder was
       actually advertising a section -- that is, how many times the ring
       exposure above would have been live rather than how many writes ran.
       It is the whole reason the suppression is observable: a run that reads
       zero says the chain never existed on this image, and a run that reads
       nonzero measures the exposure the fix removed, in the same sitting.
       `fwg` beside it counts every write block, so the pair is the rate.

       The second figure counts parks that were *attempted and refused*, and it
       is not a curiosity: the publish can abort on a core1 claim or an
       undrained result, and core0 cannot drain from inside the write, so a
       refusal there is an outage that ran unprotected. Without the pair, an
       inert park and an effective one read identically -- which would make
       this instrument lie in exactly the case it exists to measure. */
    uint32_t responder_flash_suppress_count;
    uint32_t responder_flash_suppress_inert_count;
    /* The peer's storage news value as last delivered by the relation's lane
       (Slice 11.7 gave it that carrier; D2 changed what the carrier means). It
       prints as `pnews=` on `wire sess`, and era_capture_reading.md is the
       authority on how to read it -- decoding a console column is that
       manual's, per era_identifier_map.md's own scope note: against the peer's
       own `news`, and only for equality.

       It replaced the `phint` bit, which said only that the responder had
       something. This comment then said the replacement "names the domains,
       which is what a storage gate leg has to check against `chg`" -- true of
       the seven-bit per-domain mask Slice 11.7 shipped, and false since D2
       made the value a forward-only counter with no domain identity in it at
       all. No gate leg checks this against `chg`, and none can: a session that
       tries is reading a post-D2 field by a pre-D2 rule.

       Latest-value and never cleared inside an era, so it reads as "the last
       thing the peer claimed" rather than "what it claims now". */
    uint8_t  peer_storage_news_observed;
    uint32_t transport_step_call_count;
    uint32_t flash_write_guard_begin_count;
    /* Boot instants, in ms since timer_init() at the top of keyboard_init(), at
       which the explicit launch step was entered and at which it returned.
       Saturating: a boot that reaches post-init later than 65 s is not a case
       these are measuring. */
    uint16_t communication_core_start_entry_ms;
    uint16_t communication_core_start_exit_ms;
    era_split_transport_scheduler_edge_diagnostics_t edge_diagnostics;
#endif
} era_split_transport_scheduler_state_t;

extern era_split_transport_scheduler_state_t g_era_split_transport_scheduler;

/* The committed wire role, asked as one question. It was defined twice, byte
 * for byte, in the parent unit and in the timing unit; this header is what
 * both of them already share, so it is where the one copy belongs. */
static inline bool __attribute__((always_inline)) era_split_transport_scheduler_local_initiator_available(void) {
    return g_era_split_transport_scheduler.local_wire_available && g_era_split_transport_scheduler.local_wire_initiator;
}

/* True while the standing plan core0 would publish differs from the one core1
 * is holding. It is the publication question, not a route question, and the
 * route-due word carries it only because scan_idle() reads that word. */
bool era_split_transport_scheduler_standing_plan_stale(void);

/* True while this half's relation carries the AUTHORITY section on its own
 * lane in both directions and that lane is live. It is the one predicate
 * behind both of `SESSION_STATUS`'s retirements inside a relation -- the
 * periodic beat and the per-edge revalidation -- because they are the same
 * claim and two predicates could drift apart. Public because the mode planner
 * takes it as an input: deciding what a relation invalidation *requires* is
 * the planner's, and knowing which sections a relation carries is the
 * scheduler's. */
bool era_split_transport_scheduler_relation_lane_live(void);
