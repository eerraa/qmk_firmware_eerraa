// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_transport_scheduler_timing.h"

#include <stdbool.h>
#include <stdint.h>

#include "era_split_transport_scheduler_internal.h"
#include "era_split_transport_scheduler_routes.h"
#include "../../system/era_matrix_engine.h"
#include "../era_split_authority_reducer.h"
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "../era_host_peer_storage.h"
#endif
#include "../communication_core/era_split_communication_core_lifecycle.h"
#include "../communication_core/era_split_communication_core_owner.h"
#include "../communication_core/era_split_communication_core_responder.h"
#include "../era_split_responder_projection.h"
#include "../era_split_scheduler_events.h"
#include "../era_split_scheduler_session.h"
#include "../era_split_wire_payload.h"
#include "../era_split_wire_router.h"
#include "action_layer.h"
#include "hardware/structs/timer.h"
#include "timer.h"

#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "../communication_core/era_split_communication_core_storage.h"
_Static_assert(ERA_SPLIT_AUTHORITY_POLL_PERIOD_MS <= ERA_HOST_PEER_STORAGE_RETRY_MS,
               "ERA storage service cadence exceeds the shortest storage retry deadline.");
/* R7.1's scale-one constant check. The lifecycle progress word advances only
   after a selected storage service returns, so consecutive chunks cannot
   accumulate into one gap. The backend scales wire windows at runtime,
   however; this arithmetic is deliberately not presented as a Medium/Low
   proof. The scheduler admission/liveness device gate in
   era_performance_gates.md owns that observation. */
_Static_assert(ERA_SPLIT_STANDING_LIVENESS_MS + 2 * ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONSE_WINDOW_MS <= ERA_SPLIT_CORE1_UNRESPONSIVE_MS,
               "The storage phase's worst legitimate core1 progress gap must stay inside the initiator silence watch's bound, or a healthy storage transaction reads as a dead core1.");
#endif

void era_split_transport_scheduler_reset_responder_silence_watch(void) {
    g_era_split_transport_scheduler.responder_relation_rx_observed       = false;
    g_era_split_transport_scheduler.responder_relation_rx_observed_count = 0;
    g_era_split_transport_scheduler.responder_accepted_rx_observed_count = 0;
    g_era_split_transport_scheduler.responder_relation_rx_observed_ms    = 0;
}

static uint32_t era_split_transport_scheduler_relation_stale_ms(void) {
#if ERA_SPLIT_SESSION_STALE_MS == 0
    return ERA_SPLIT_RESPONDER_SILENCE_MS;
#else
    return ERA_SPLIT_SESSION_STALE_MS;
#endif
}

bool era_split_transport_scheduler_responder_silence_stale(bool peer_known) {
    if (!peer_known || !g_era_split_transport_scheduler.local_wire_available || g_era_split_transport_scheduler.local_wire_initiator) {
        era_split_transport_scheduler_reset_responder_silence_watch();
        return false;
    }

    era_split_responder_projection_t responder;
    era_split_responder_projection_get(&responder);
    // Wire-anchored liveness: the Core1 accepted-RX counter moves on every
    // frame the transaction engine accepts, so storage chunk streams that
    // suppress heartbeat traffic still refresh the silence watch.
    uint32_t accepted_rx_count = era_split_communication_core_responder_accepted_rx_count();
    uint32_t now_ms = timer_read32();
    if (!g_era_split_transport_scheduler.responder_relation_rx_observed ||
        g_era_split_transport_scheduler.responder_relation_rx_observed_count != responder.relation_request_rx_count ||
        g_era_split_transport_scheduler.responder_accepted_rx_observed_count != accepted_rx_count) {
        g_era_split_transport_scheduler.responder_relation_rx_observed       = true;
        g_era_split_transport_scheduler.responder_relation_rx_observed_count = responder.relation_request_rx_count;
        g_era_split_transport_scheduler.responder_accepted_rx_observed_count = accepted_rx_count;
        g_era_split_transport_scheduler.responder_relation_rx_observed_ms    = now_ms;
        return false;
    }

    uint32_t watch_age_ms = timer_elapsed32(g_era_split_transport_scheduler.responder_relation_rx_observed_ms);
    uint32_t stale_ms     = era_split_transport_scheduler_relation_stale_ms();
    if (watch_age_ms < stale_ms) {
        return false;
    }
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    era_host_peer_storage_cause_timeline_note_stale(watch_age_ms <= UINT16_MAX ? (uint16_t)watch_age_ms : UINT16_MAX,
                                                    stale_ms <= UINT16_MAX ? (uint16_t)stale_ms : UINT16_MAX);
#endif
    return true;
}

static void era_split_transport_scheduler_reset_initiator_silence_watch(void) {
    g_era_split_transport_scheduler.initiator_progress_observed       = false;
    g_era_split_transport_scheduler.initiator_progress_observed_count = 0;
    g_era_split_transport_scheduler.initiator_progress_observed_ms    = 0;
}

/* R7.1: the initiator-side twin of the watch above, closing the gap the R7
 * kill leg exposed. The responder watch is fed by arriving traffic, so the
 * half whose core1 *initiates* had no detector when that core1 died: nothing
 * arrived, nothing was missed, and the half idled on a re-blessed dead lease
 * until an external edge. One existing core1-published progress word is the
 * signal, and a fire feeds the same sticky peer-stale latch. A completed
 * queue, storage or standing service pass advances it, so route priority
 * cannot make healthy work look like silence. The bound is the responder watch's 100 ms figure
 * but deliberately NOT its ERA_SPLIT_SESSION_STALE_MS override: that override
 * widens how long a session may go unfed, and this watch measures core1
 * responsiveness, which no configuration is entitled to widen.
 *
 * Armed only while this half is the wire initiator holding a live CORE1
 * lease over a known peer, with no stop report and a *granted* standing
 * plan. Every legitimate quiet state disarms rather than ages: bootstrap
 * (peer unknown), the quiesce park and any torn or in-transfer lease (lease
 * not live), a core1-reported stop (core0's ordinary revalidation owns it),
 * a capped wire (local_wire_available false), and a plan the builder
 * withheld — LOCAL_NO_LINK with a known peer, or HOST-PEER before
 * matrix-ready — where core1 is not supposed to be exchanging at all
 * (standing_plan_granted, cached at the publish from core1's own acceptance
 * test). The storage-exclusivity phase stays inside the bound by the two
 * asserts beside the constants, and the whole-pair suspend holds no hazard:
 * core1 keeps exchanging through core0's USB suspend, so the count is fresh
 * at resume. */
static bool era_split_transport_scheduler_initiator_silence_stale(bool peer_known) {
    if (!peer_known || !g_era_split_transport_scheduler.local_wire_available ||
        !g_era_split_transport_scheduler.local_wire_initiator ||
        !g_era_split_transport_scheduler.standing_plan_granted ||
        g_era_split_transport_scheduler.standing_stop_observed ||
        !era_split_communication_core_owner_core1_role_is_live(ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR)) {
        era_split_transport_scheduler_reset_initiator_silence_watch();
        return false;
    }

    uint32_t progress_count = era_split_communication_core_progress_count();
    uint32_t now_ms         = timer_read32();
    if (!g_era_split_transport_scheduler.initiator_progress_observed ||
        g_era_split_transport_scheduler.initiator_progress_observed_count != progress_count) {
        g_era_split_transport_scheduler.initiator_progress_observed       = true;
        g_era_split_transport_scheduler.initiator_progress_observed_count = progress_count;
        g_era_split_transport_scheduler.initiator_progress_observed_ms    = now_ms;
        return false;
    }

    if (timer_elapsed32(g_era_split_transport_scheduler.initiator_progress_observed_ms) < ERA_SPLIT_CORE1_UNRESPONSIVE_MS) {
        return false;
    }
    return true;
}

void era_split_transport_scheduler_reset_session_probe_backoff(void) {
    g_era_split_transport_scheduler.attach_status_last_tx_valid = false;
    g_era_split_transport_scheduler.attach_status_miss_streak   = 0;
}

static uint32_t era_split_transport_scheduler_bootstrap_period_ms(void) {
    if (g_era_split_transport_scheduler.attach_status_miss_streak < ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER) {
        return ERA_SPLIT_WIRE_BOOTSTRAP_PERIOD_MS;
    }

    return ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS;
}

static bool era_split_transport_scheduler_period_due(uint32_t period_ms) {
    if (!g_era_split_transport_scheduler.attach_status_last_tx_valid) {
        return true;
    }
    return timer_elapsed32(g_era_split_transport_scheduler.attach_status_last_tx_ms) >= period_ms;
}

static bool era_split_transport_scheduler_bootstrap_due(void) {
    return era_split_transport_scheduler_period_due(era_split_transport_scheduler_bootstrap_period_ms());
}

bool era_split_transport_scheduler_local_status_revalidation_due(void) {
    return g_era_split_transport_scheduler.local_status_pending && era_split_transport_scheduler_bootstrap_due();
}

/* True while this half's relation runs a lane of its own that carries the
   AUTHORITY section in both directions. Since Slice 11.6 that is what makes
   `SESSION_STATUS` unnecessary *inside* a relation, in both of the ways it used
   to be needed: the periodic beat, because the lane carries liveness through
   accepted relation traffic, and the per-edge revalidation, because the lane
   carries the peer's session facts. What the frame keeps is discovery,
   bootstrap and recovery -- every one of them an edge, and every one still
   reachable through the pending-revalidation arm above this call.

   One predicate serves both suppressions on purpose. They are the same claim,
   and giving each its own would let them drift into disagreeing about when a
   relation revalidates itself.

   The eligibility test is derived rather than asserted, so a relation that
   loses the section gets its frame back instead of silently losing its only
   carrier. HOST-PEER already suppressed the beat here on `matrix_ready` alone,
   and it suppressed it with no authority carrier at all -- which is why a HOST
   losing USB used to reach its PEER only when the wire began failing. */
bool era_split_transport_scheduler_relation_lane_live(void) {
    switch (g_era_split_transport_scheduler.mode) {
        case ERA_SPLIT_MODE_HOST_PEER_PEER:
            if (!era_matrix_engine_local_matrix_ready()) {
                return false;
            }
            break;
        case ERA_SPLIT_MODE_DUAL_HOST_LEFT:
            break;
        default:
            return false;
    }
    return (era_split_wire_eligible_sections((uint8_t)g_era_split_transport_scheduler.mode, ERA_SPLIT_WIRE_SECTION_DIRECTION_PUSH) &
            ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY) != 0 &&
           (era_split_wire_eligible_sections((uint8_t)g_era_split_transport_scheduler.mode, ERA_SPLIT_WIRE_SECTION_DIRECTION_RSP) &
            ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY) != 0;
}

/* The post-relation SESSION_STATUS beat, and since Slice 11.7 there is nothing
   left of it in either relation. Split out because the due predicate and the deadline must answer it
   identically -- a deadline that outlives its due predicate wakes core0 for a
   route it will then decline to select.

   Two exceptions used to live here and both are gone, for the same reason
   stated twice.

   The apply-window keepalive resumed the beat during the sliced durable write
   so the responder's accepted-RX watch stayed fed. It could not work: the
   frame it sends is core0's, and core0 inside a flash write is the thing that
   cannot send. Device-measured 2026-08-02, it emitted one frame in a 1728 ms
   window. Core1's liveness beat took that job in the same slice and holds the
   relation from the side that is still running.

   The storage hint poll ran a 50 ms `SESSION_STATUS` on the DUAL-HOST
   initiator to read one advisory bit off a frame the relation otherwise no
   longer needed, because DUAL-HOST had no response section to carry it. It has
   one now (`STORAGE_NEWS`), so the fact arrives on the lane at
   no core0 cost. This used to add "per domain", which is what the section
   carried when 11.7 moved the fact onto it; since D2 it carries a forward-only
   news value naming no domain, and the argument for the move is unchanged by
   that -- what mattered was the carrier, not what rode it.

   That poll was also the frame whose refusal by a responder busy in its own
   local write marked a live peer stale -- 11 refused frames and one forgotten
   session in a single 432 ms window, device-measured the same day. */
static bool era_split_transport_scheduler_periodic_status_suppressed(void) {
    return era_split_transport_scheduler_relation_lane_live();
}

static bool era_split_transport_scheduler_attach_status_due(void) {
    if (era_split_transport_scheduler_core1_initiator_pending()) {
        return false;
    }

    if (!era_split_transport_scheduler_local_initiator_available()) {
        return false;
    }

    if (era_split_transport_scheduler_local_status_revalidation_due()) {
        return true;
    }

    if (era_split_transport_scheduler_periodic_status_suppressed()) {
        return false;
    }

    if (era_split_scheduler_session_peer_known()) {
        return era_split_transport_scheduler_period_due(ERA_SPLIT_SESSION_REFRESH_PERIOD_MS);
    }

    return era_split_transport_scheduler_bootstrap_due();
}

static bool era_split_transport_scheduler_host_peer_source_push_due(void) {
    if (era_split_transport_scheduler_core1_initiator_pending()) {
        return false;
    }

    if (era_split_transport_scheduler_local_status_revalidation_due()) {
        return false;
    }

    return era_split_transport_scheduler_local_initiator_available() && g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_HOST_PEER_PEER && era_matrix_engine_source_push_due();
}

/* The three HOST-PEER runtime due predicates -- the 50 ms liveness heartbeat,
   the 10 ms HOST-source response poll, and the dirty-driven AUTHORITY push --
   were retired by R2 together with the routes they fed. Each of their gates
   survives, expressed once in the standing plan core0 publishes instead of
   three times here: `local_initiator_available()` and the relation are what
   decide whether a plan is built at all, `matrix_ready` is the HOST-PEER arm's
   own precondition, `local_status_pending` is the enable bit, and the period
   became the plan's `poll_period_ms`. `core1_initiator_pending()` has no
   counterpart and needs none -- core1's pass order is what gives the request
   queue precedence over the standing service, rather than a core0 predicate
   declining to select.

   What no core0 predicate could express is the one this lane exists for: the
   liveness deadline runs while core0 is inside a durable EEPROM write, which
   is precisely when a core0-originated frame cannot be sent.

   The DUAL-HOST runtime lane has no core0 due predicate and no core0 deadline
   since Slice 11.5. Core1 owns the period under the standing grant, so what is
   left here is one publication question: does the plan core0 would publish
   differ from the one core1 is holding?

   It is mapped onto a route-due bit rather than onto a new mechanism because
   that bit is what scan_idle() reads, and a layer edge must reach the wire in
   about one scan rather than at the next housekeeping deadline. The layer hook
   ORs it as a prompt and this recomputes it from the actual comparison.

   **This recompute is the backstop and not the consumer.** It runs from the
   housekeeping body, from init and from the core1 start; the scan path's own
   call, in era_split_transport_scheduler_local_initiator_step()
   (`split/era_split_transport_scheduler.c`), is gated on a route having been
   selected, and this is the one route-due bit that selects none --
   era_split_wire_router_select_owner() (`split/era_split_wire_router.c`) has
   an arm for attach-status revalidation and one for the HOST-PEER matrix push
   and no other. So the bit is consumed where it is answered, at the top of
   era_split_transport_scheduler_publish_standing_plan(), and the comment
   there is the one that explains the ordering. Without that consumer the bit
   held scan_idle() false until the next housekeeping deadline, and every
   matrix scan in that window rebuilt and compared the plan for a publish the
   first pass had already made.

   **It stays DUAL-HOST's alone even though HOST-PEER now builds a plan too**
   (R2), and the reason is the same one that decides whether the plan carries
   an INPUT_LAYER byte at all. Every field of a HOST-PEER plan is produced at
   housekeeping cadence -- the authority sample, the mode pass, the storage
   task, the status round trip -- so there is nothing a scan could move, and
   the housekeeping backstop publishes it within the authority sample's own
   deadline. Arming this bit there would instead hold scan_idle() false and
   run the slow path per scan until the publish, which is the per-exchange
   core0 cost this slice exists to take off that half. */
static bool era_split_transport_scheduler_standing_plan_publish_due(void) {
    if (g_era_split_transport_scheduler.mode != ERA_SPLIT_MODE_DUAL_HOST_LEFT) {
        return false;
    }
    return era_split_transport_scheduler_standing_plan_stale();
}

static bool era_split_transport_scheduler_deadline_elapsed(uint32_t now_ms, uint32_t deadline_ms) {
    return timer_expired32(now_ms, deadline_ms);
}

static void era_split_transport_scheduler_include_deadline(bool *valid, uint32_t *deadline_ms, uint32_t candidate_ms) {
    if (valid == NULL || deadline_ms == NULL) {
        return;
    }
    if (!*valid || era_split_transport_scheduler_deadline_elapsed(*deadline_ms, candidate_ms)) {
        *valid       = true;
        *deadline_ms = candidate_ms;
    }
}

static bool era_split_transport_scheduler_period_deadline(bool last_valid, uint32_t last_ms, uint32_t period_ms, uint32_t now_ms, uint32_t *deadline_ms) {
    if (deadline_ms == NULL) {
        return false;
    }
    *deadline_ms = last_valid ? last_ms + period_ms : now_ms;
    return true;
}

static bool era_split_transport_scheduler_attach_status_deadline(uint32_t now_ms, uint32_t *deadline_ms) {
    if (era_split_transport_scheduler_core1_initiator_pending()) {
        return false;
    }

    if (!era_split_transport_scheduler_local_initiator_available()) {
        return false;
    }

    if (g_era_split_transport_scheduler.local_status_pending) {
        return era_split_transport_scheduler_period_deadline(g_era_split_transport_scheduler.attach_status_last_tx_valid, g_era_split_transport_scheduler.attach_status_last_tx_ms, era_split_transport_scheduler_bootstrap_period_ms(), now_ms, deadline_ms);
    }

    if (era_split_transport_scheduler_periodic_status_suppressed()) {
        return false;
    }

    if (era_split_scheduler_session_peer_known()) {
        return era_split_transport_scheduler_period_deadline(g_era_split_transport_scheduler.attach_status_last_tx_valid, g_era_split_transport_scheduler.attach_status_last_tx_ms, ERA_SPLIT_SESSION_REFRESH_PERIOD_MS, now_ms, deadline_ms);
    }

    return era_split_transport_scheduler_period_deadline(g_era_split_transport_scheduler.attach_status_last_tx_valid, g_era_split_transport_scheduler.attach_status_last_tx_ms, era_split_transport_scheduler_bootstrap_period_ms(), now_ms, deadline_ms);
}

/* The two HOST-PEER route deadlines went with their due predicates (R2), and
   they had to go in the same commit: a deadline that outlives its due
   predicate wakes core0 for a route it will then decline to select. Their
   absence is the measurable half of the grant in this relation, exactly as it
   already was in DUAL-HOST -- core0 no longer wakes on the 10 ms poll period
   or the 50 ms liveness period at all. The one core0 deadline that still
   tracks that period is the responder RGB snapshot republish below, which is
   a publish rate and not a route. */

static bool era_split_transport_scheduler_responder_stale_deadline(uint32_t now_ms, uint32_t *deadline_ms) {
    if (!era_split_scheduler_session_peer_known() || !g_era_split_transport_scheduler.local_wire_available || g_era_split_transport_scheduler.local_wire_initiator) {
        return false;
    }

    if (!g_era_split_transport_scheduler.responder_relation_rx_observed) {
        if (deadline_ms != NULL) {
            *deadline_ms = now_ms;
        }
        return true;
    }

    if (deadline_ms != NULL) {
        *deadline_ms = g_era_split_transport_scheduler.responder_relation_rx_observed_ms + era_split_transport_scheduler_relation_stale_ms();
    }
    return true;
}

static bool era_split_transport_scheduler_authority_poll_due(uint32_t now_ms) {
    return !g_era_split_transport_scheduler.authority_poll_deadline_valid || era_split_transport_scheduler_deadline_elapsed(now_ms, g_era_split_transport_scheduler.authority_poll_deadline_ms);
}

static void era_split_transport_scheduler_note_authority_polled(uint32_t now_ms) {
    g_era_split_transport_scheduler.authority_poll_deadline_valid = true;
    g_era_split_transport_scheduler.authority_poll_deadline_ms    = now_ms + ERA_SPLIT_AUTHORITY_POLL_PERIOD_MS;
}

bool era_split_transport_scheduler_sample_authority(uint32_t now_ms, bool mark_dirty_on_change) {
    bool changed = era_split_authority_reducer_task();
    if (changed && mark_dirty_on_change) {
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_AUTHORITY);
    }
    era_split_transport_scheduler_note_authority_polled(now_ms);
    return changed;
}

bool era_split_transport_scheduler_refresh_time_due_tokens(uint32_t now_ms) {
    bool maintenance_performed = false;

    if (era_split_transport_scheduler_authority_poll_due(now_ms)) {
        era_split_transport_scheduler_mark_maintenance_due(ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE);
    }

    uint32_t stale_deadline_ms = 0;
    if (era_split_transport_scheduler_responder_stale_deadline(now_ms, &stale_deadline_ms) && era_split_transport_scheduler_deadline_elapsed(now_ms, stale_deadline_ms)) {
        if (era_split_transport_scheduler_responder_silence_stale(era_split_scheduler_session_peer_known())) {
            g_era_split_transport_scheduler.peer_session_stale = true;
            era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_PEER_STALE);
            maintenance_performed = true;
        }
    }

    /* R7.1: the initiator twin runs every pass rather than behind a deadline
       gate — its whole cost is a handful of flag reads and one aligned-word
       load, against the responder arm's two snapshot calls. Same latch, same
       dirty flag, same downstream: the planner's stale arm replans and the
       transfer meets R7's judgment. */
    if (era_split_transport_scheduler_initiator_silence_stale(era_split_scheduler_session_peer_known())) {
        g_era_split_transport_scheduler.peer_session_stale = true;
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_PEER_STALE);
        maintenance_performed = true;
    }

    return maintenance_performed;
}

/* The raw-microsecond stamp the scan-rate task pre-filters on: the earliest
   raw instant at which `timer_read32()` could already be reading
   `deadline_ms`. `now_ms` covers a whole millisecond of raw counter and
   `raw_now_us` sits at an unknown phase inside it, so the millisecond in
   which the deadline arrives begins somewhere in
   `(raw_now_us + (remaining - 1) * 1000, raw_now_us + remaining * 1000]` --
   and taking the low end makes the stamp early by up to one millisecond and
   late by none. That direction is the whole safety argument: an early stamp
   costs one clock read that the millisecond compare then declines, while a
   late one would move a cadence.

   Two cases fold into "open now": a deadline already past, which wraps
   `remaining_ms` to something enormous, and one further out than the horizon,
   where `remaining * 1000` would leave the raw counter's signed comparison
   window. Both degrade to reading the clock every pass, which is what this
   function's callers did before the stamp existed. */
#define ERA_SPLIT_SCHEDULER_DEADLINE_RAW_HORIZON_MS 60000U

static inline uint32_t era_split_transport_scheduler_deadline_raw_stamp(uint32_t raw_now_us, uint32_t now_ms, uint32_t deadline_ms) {
    uint32_t remaining_ms = deadline_ms - now_ms;
    if (remaining_ms == 0U || remaining_ms > ERA_SPLIT_SCHEDULER_DEADLINE_RAW_HORIZON_MS) {
        return raw_now_us;
    }
    return raw_now_us + (remaining_ms - 1U) * 1000U;
}

void era_split_transport_scheduler_update_next_deadline(void) {
    bool     valid       = false;
    uint32_t deadline_ms = 0;
    /* Read before the millisecond, so the pair can only understate how far the
       deadline is -- the same direction the stamp above rounds in. */
    uint32_t raw_now_us  = timer_hw->timerawl;
    uint32_t now_ms      = timer_read32();

    uint32_t authority_deadline_ms = g_era_split_transport_scheduler.authority_poll_deadline_valid ? g_era_split_transport_scheduler.authority_poll_deadline_ms : now_ms;
    era_split_transport_scheduler_include_deadline(&valid, &deadline_ms, authority_deadline_ms);

    uint32_t route_deadline_ms = 0;
    if (era_split_transport_scheduler_attach_status_deadline(now_ms, &route_deadline_ms)) {
        era_split_transport_scheduler_include_deadline(&valid, &deadline_ms, route_deadline_ms);
    }
    if (era_split_transport_scheduler_responder_stale_deadline(now_ms, &route_deadline_ms)) {
        era_split_transport_scheduler_include_deadline(&valid, &deadline_ms, route_deadline_ms);
    }
    /* No runtime deadline sits here any more in either serviced relation, and
       its absence is the measurable half of the standing grant: core0 no
       longer wakes on a poll period at all, so a shorter period costs it
       nothing. DUAL-HOST reached that at Slice 11.5 and HOST-PEER at R2. */
    if (g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_HOST_PEER_HOST) {
        era_split_transport_scheduler_include_deadline(&valid,
                                                       &deadline_ms,
                                                       g_era_split_transport_scheduler.host_peer_responder_rgb_state_publish_deadline_valid ? g_era_split_transport_scheduler.host_peer_responder_rgb_state_publish_deadline_ms : now_ms);
    }
    g_era_split_transport_scheduler.next_scheduler_deadline_valid  = valid;
    g_era_split_transport_scheduler.next_scheduler_deadline_ms     = deadline_ms;
    g_era_split_transport_scheduler.next_scheduler_deadline_raw_us = era_split_transport_scheduler_deadline_raw_stamp(raw_now_us, now_ms, deadline_ms);
}

bool era_split_transport_scheduler_refresh_route_due_flags(void) {
    uint8_t flags = 0;
    if (era_split_transport_scheduler_attach_status_due()) {
        flags |= ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS;
    }
    if (era_split_transport_scheduler_host_peer_source_push_due()) {
        flags |= ERA_SPLIT_SCHEDULER_ROUTE_DUE_HOST_PEER_SOURCE_PUSH;
    }
    if (era_split_transport_scheduler_standing_plan_publish_due()) {
        flags |= ERA_SPLIT_SCHEDULER_ROUTE_DUE_DUAL_RUNTIME_PUSH;
    }
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    /* A published storage request owns the next non-preemptive transaction
       boundary. Keep both generic lanes outside the queue until its result is
       drained; their source latches remain due. Between storage requests the
       episode's exclusivity clamp is still a mask down to the one bit that
       survives it, so mandatory revalidation wins before the next request. */
    if (era_host_peer_storage_initiator_request_pending()) {
        flags = 0;
    } else if (era_host_peer_storage_route_exclusive()) {
        flags &= ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS;
    }
#endif

    uint8_t previous                                = g_era_split_transport_scheduler.route_due_flags;
    g_era_split_transport_scheduler.route_due_flags = flags;
    return previous != flags;
}

static bool era_split_transport_scheduler_route_due(uint8_t flag) {
    return (g_era_split_transport_scheduler.route_due_flags & flag) != 0;
}

void era_split_transport_scheduler_clear_owner_route(void) {
    if (g_era_split_transport_scheduler.last_owner_route == ERA_SPLIT_ROUTE_NONE && g_era_split_transport_scheduler.last_owner_reason == ERA_SPLIT_ROUTE_REASON_NONE) {
        return;
    }

    g_era_split_transport_scheduler.last_owner_route  = ERA_SPLIT_ROUTE_NONE;
    g_era_split_transport_scheduler.last_owner_reason = ERA_SPLIT_ROUTE_REASON_NONE;
}

void era_split_transport_scheduler_select_owner_route(void) {
    if (g_era_split_transport_scheduler.route_due_flags == 0) {
        era_split_transport_scheduler_clear_owner_route();
        return;
    }

    era_split_router_owner_input_t input = {
        .mode                      = g_era_split_transport_scheduler.mode,
        .attach_status_due         = era_split_transport_scheduler_route_due(ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS),
        .host_peer_source_push_due = era_split_transport_scheduler_route_due(ERA_SPLIT_SCHEDULER_ROUTE_DUE_HOST_PEER_SOURCE_PUSH),
    };

    era_split_route_selection_t selection;
    era_split_wire_router_select_owner(&input, &selection);
    /* The two lines above are live route state -- ..._routes.c dispatches on
       them -- and only the counter below is the instrument. */
    g_era_split_transport_scheduler.last_owner_route  = selection.kind;
    g_era_split_transport_scheduler.last_owner_reason = selection.reason;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    g_era_split_transport_scheduler.owner_step_count++;
#endif
}
