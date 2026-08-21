// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_transport_scheduler_routes.h"

#include <stdint.h>

#include "atomic_util.h"
#include "era_split_transport_scheduler_internal.h"
#include "../era_host_peer_matrix_link.h"
#include "../era_host_peer_transaction.h"
#include "../communication_core/era_split_communication_core_diagnostics.h"
#include "../communication_core/era_split_communication_core_initiator.h"
#include "../communication_core/era_split_communication_core_lifecycle.h"
#include "../communication_core/era_split_communication_core_owner.h"
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "../communication_core/era_split_communication_core_storage.h"
#    include "../era_host_peer_storage.h"
#endif
#include "../era_split_transport_scheduler.h"
#include "../era_split_scheduler_events.h"
#include "../era_split_scheduler_session.h"
#include "../era_split_transaction_engine.h"
#include "../era_split_wire_payload.h"
#include "hardware/structs/timer.h"
#include "timer.h"

#ifndef ERA_SPLIT_COMMUNICATION_CORE_REQUEST_QUEUE_WINDOW_US
#    define ERA_SPLIT_COMMUNICATION_CORE_REQUEST_QUEUE_WINDOW_US 5000U
#endif

/* The `clear_host_peer_tx` parameter went with the period anchor it cleared
   (R2): the two HOST-PEER routes it paced are gone and nothing else read it,
   so the two callers were choosing between clearing a field with no reader and
   not clearing it. */
static void era_split_transport_scheduler_mark_peer_stale_revalidation(void) {
    g_era_split_transport_scheduler.peer_session_stale          = true;
    g_era_split_transport_scheduler.local_status_pending        = true;
    g_era_split_transport_scheduler.attach_status_last_tx_valid = false;
    era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_PEER_STALE);
    era_split_transport_scheduler_mark_route_due(ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS);
}

static void era_split_transport_scheduler_note_attach_status_request_attempt(era_split_transaction_engine_result_t result, bool request_sent, bool stale_on_no_response) {
    if (request_sent) {
        era_split_scheduler_session_note_local_status_sent();
    }
    g_era_split_transport_scheduler.attach_status_last_tx_ms    = timer_read32();
    g_era_split_transport_scheduler.attach_status_last_tx_valid = true;
    if (result == ERA_SPLIT_TRANSACTION_RESULT_OK) {
        g_era_split_transport_scheduler.attach_status_miss_streak = 0;
        g_era_split_transport_scheduler.local_status_pending      = false;
    } else if (g_era_split_transport_scheduler.attach_status_miss_streak < UINT8_MAX) {
        g_era_split_transport_scheduler.attach_status_miss_streak++;
    }
    /* No softening here since Slice 11.7, and its removal is the point rather
     * than a simplification. There used to be one for the apply-window
     * keepalive -- a periodic frame core0 sent from inside its own flash write,
     * where a miss meant nothing -- and that frame is gone: core1's liveness
     * beat holds the relation through the write instead.
     *
     * What is left on this path is discovery and revalidation only, and a miss
     * on either is decisive by construction. Revalidation is sent because this
     * half already doubts the relation, and the relation's own lane is what
     * carries it the rest of the time; a frame this half sends *because* it
     * needs an answer, that gets none, is exactly the peer-stale evidence the
     * route contract names here. */
    if (stale_on_no_response && request_sent && result != ERA_SPLIT_TRANSACTION_RESULT_OK) {
        era_split_transport_scheduler_mark_peer_stale_revalidation();
    }
}

/* The one HOST-PEER owner route left is the matrix push, so this now records
   the outcome of exactly that. The period stamp it used to take went with the
   two paced routes (R2); what survives is the pair that was never about a
   period -- a successful relation frame clears the discovery miss streak, and
   a request that went out and got nothing back is peer-stale evidence. */
static void era_split_transport_scheduler_note_host_peer_request_attempt(era_split_transaction_engine_result_t result, bool request_sent) {
    if (result == ERA_SPLIT_TRANSACTION_RESULT_OK) {
        g_era_split_transport_scheduler.attach_status_miss_streak = 0;
        return;
    }

    if (request_sent) {
        era_split_transport_scheduler_mark_peer_stale_revalidation();
    }
}

/* The apply-summary counters retired with the path they counted (D3). They
   were the second half of a deliberate double-count -- the same three
   `*_rx_count` fields the standing apply increments -- kept so that a section
   arriving on either path could not read as silence on the other. There is one
   path now, so `wire sect app=` reads the same totals from one producer. */

bool era_split_transport_scheduler_core1_initiator_pending(void) {
    return g_era_split_transport_scheduler.core1_initiator_async_pending ||
           era_split_communication_core_initiator_pending() ||
           era_split_communication_core_initiator_result_ready();
}

bool era_split_transport_scheduler_cancel_core1_initiator(void) {
    if (!era_split_transport_scheduler_core1_initiator_pending()) {
        return true;
    }
    if (!era_split_communication_core_queue_reset()) {
        return false;
    }
    g_era_split_transport_scheduler.core1_initiator_async_pending             = false;
    g_era_split_transport_scheduler.core1_initiator_pending_lane              = ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_INVALID;
    g_era_split_transport_scheduler.core1_initiator_peer_known_before_request = false;
    return true;
}

bool era_split_transport_scheduler_stop_communication_core_for_flash_write(void) {
    if (!era_split_transport_scheduler_cancel_core1_initiator()) {
        return false;
    }
    if (!era_split_communication_core_owner_transfer_role(ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE,
                                                           ERA_SPLIT_TRANSACTION_BACKEND_ROLE_DISABLED)) {
        return false;
    }
    (void)era_split_transport_scheduler_drain_communication_core_responder_results();
    bool stopped = era_split_communication_core_request_quiesce();
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    if (stopped) {
        era_split_communication_core_storage_capacity_init();
    }
#endif
    return stopped;
}

bool era_split_transport_scheduler_initiator_route_available(void) {
    return !era_split_transport_scheduler_core1_initiator_pending();
}

static uint16_t era_split_transport_scheduler_next_core1_request_generation(void) {
    g_era_split_transport_scheduler.core1_initiator_request_generation++;
    if (g_era_split_transport_scheduler.core1_initiator_request_generation == 0) {
        g_era_split_transport_scheduler.core1_initiator_request_generation = 1;
    }
    return g_era_split_transport_scheduler.core1_initiator_request_generation;
}

static void era_split_transport_scheduler_init_core1_request(era_split_communication_core_initiator_request_t *request,
                                                             era_split_communication_core_initiator_lane_t lane,
                                                             era_split_wire_payload_kind_t expected_kind,
                                                             era_split_wire_payload_kind_t alternate_expected_kind,
                                                             uint16_t response_window_ms) {
    *request = (era_split_communication_core_initiator_request_t){
        .owner_epoch            = era_split_communication_core_owner_epoch(),
        .relation_generation    = g_era_split_transport_scheduler.core1_initiator_relation_generation,
        .request_generation     = era_split_transport_scheduler_next_core1_request_generation(),
        .lane                   = (uint8_t)lane,
        .route_kind             = (uint8_t)g_era_split_transport_scheduler.last_owner_route,
        .route_reason           = (uint8_t)g_era_split_transport_scheduler.last_owner_reason,
        .request_direction      = ERA_SPLIT_WIRE_DIRECTION_PRIMARY_TO_SECONDARY,
        .expected_kind          = (uint8_t)expected_kind,
        .alternate_expected_kind = (uint8_t)alternate_expected_kind,
        .response_window_ms     = response_window_ms,
        .not_after_us           = timer_hw->timerawl + ERA_SPLIT_COMMUNICATION_CORE_REQUEST_QUEUE_WINDOW_US,
    };
}

static bool era_split_transport_scheduler_submit_core1_request(const era_split_communication_core_initiator_request_t *request, bool peer_known_before_request) {
    if (!era_split_communication_core_enqueue_initiator(request)) {
        return false;
    }
    g_era_split_transport_scheduler.core1_initiator_async_pending             = true;
    g_era_split_transport_scheduler.core1_initiator_pending_since_ms          = timer_read32();
    g_era_split_transport_scheduler.core1_initiator_pending_lane              = request->lane;
    g_era_split_transport_scheduler.core1_initiator_pending_generation        = request->request_generation;
    g_era_split_transport_scheduler.core1_initiator_peer_known_before_request = peer_known_before_request;
    return true;
}

static bool era_split_transport_scheduler_try_enqueue_attach_status(void) {
    era_split_wire_session_status_t local_status;
    if (!era_split_scheduler_session_build_local_status(true, &local_status)) {
        return false;
    }

    bool     peer_known_before_request = era_split_scheduler_session_peer_known();
    uint16_t response_window_ms        = peer_known_before_request ? ERA_SPLIT_PEER_RESPONSE_WINDOW_MS : ERA_SPLIT_SESSION_BOOTSTRAP_RESPONSE_WINDOW_MS;
    era_split_communication_core_initiator_request_t request;
    era_split_transport_scheduler_init_core1_request(&request,
                                                     ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS,
                                                     ERA_SPLIT_WIRE_PAYLOAD_SESSION_STATUS,
                                                     ERA_SPLIT_WIRE_PAYLOAD_INVALID,
                                                     response_window_ms);
    request.semantic.session_status = local_status;
    bool submitted = era_split_transport_scheduler_submit_core1_request(&request, peer_known_before_request);
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    if (submitted) {
        era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_SESSION_SUBMIT,
                                                  peer_known_before_request ? 1 : 0);
    }
#endif
    return submitted;
}

/* The heartbeat enqueuer is gone with the two routes that selected it (R2).
   Core1's standing service builds the identical frame -- one compact control
   byte, GRANT_ACK expected with HOST_PEER_HOST_SOURCE_RSP as the alternate --
   from its own period rather than from a queued request, which is the whole of
   what moved. The HEARTBEAT *lane* is deliberately left standing on the core1
   side: its id, its four ok/miss/bad/fail counters and the matrix-link
   heartbeat count are capture surface, and retiring them is a diagnostics
   decision this slice did not take. It is unreachable in the meantime, and a
   HOST-PEER PEER's `hb=` now reads 0/0 as a consequence -- one of the
   re-baselined diagnostic shapes R2 owes, not a silent relation. */

static bool era_split_transport_scheduler_try_enqueue_host_peer_source_push(void) {
    era_split_communication_core_initiator_request_t request;
    era_split_transport_scheduler_init_core1_request(&request,
                                                     ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH,
                                                     ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK,
                                                     ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER_HOST_SOURCE_RSP,
                                                     ERA_SPLIT_PEER_RESPONSE_WINDOW_MS);
    request.semantic.source_push.sections = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_CORE0_LANE_SECTIONS;
    if (!era_host_peer_matrix_link_capture_source_push(request.semantic.source_push.packed_rows, &request.semantic.source_push.matrix_seq)) {
        return false;
    }
    return era_split_transport_scheduler_submit_core1_request(&request, false);
}

/* HOST-PEER's AUTHORITY push enqueuer is gone with its route (R2). The section
   is unchanged and still cannot share the matrix frame -- 3 + 7 + 7 is two
   over the compact budget on this board -- but it no longer needs a route of
   its own to avoid deferring behind the matrix: core1 plans it onto whichever
   standing exchange is next, on a wire the matrix push already preempts by
   the pass order. */

static bool era_split_transport_scheduler_core1_result_matches(const era_split_communication_core_initiator_result_t *result) {
    return result != NULL &&
           result->owner_epoch == era_split_communication_core_owner_epoch() &&
           result->relation_generation == g_era_split_transport_scheduler.core1_initiator_relation_generation &&
           result->request_generation == g_era_split_transport_scheduler.core1_initiator_pending_generation &&
           result->lane == g_era_split_transport_scheduler.core1_initiator_pending_lane;
}

static void era_split_transport_scheduler_apply_core1_session_result(const era_split_communication_core_initiator_result_t *result) {
    bool request_sent = result->request_sent != 0;
    era_split_transaction_engine_result_t transaction_result = (era_split_transaction_engine_result_t)result->result;
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    uint8_t cause_detail = (uint8_t)transaction_result;
    if (transaction_result == ERA_SPLIT_TRANSACTION_RESULT_OK && !result->decoded.session.valid) {
        cause_detail = 5;
    }
    era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_SESSION_RESULT, cause_detail);
#endif
    era_split_transport_scheduler_note_attach_status_request_attempt(transaction_result,
                                                                    request_sent,
                                                                    g_era_split_transport_scheduler.core1_initiator_peer_known_before_request);
    if (transaction_result == ERA_SPLIT_TRANSACTION_RESULT_OK && result->decoded.session.valid) {
        era_split_scheduler_session_note_peer_status(&result->decoded.session.status);
    }
}

/* The accept-clip for this path retired with the sections it clipped (D3). Its
   rule did not: a receiving half re-asks the eligibility question against its
   own relation rather than trusting the sender's send-clip, and that clip now
   happens once on core1's standing decode and once at core0's standing apply,
   which is where it always was for this relation's other carrier. */

static void era_split_transport_scheduler_apply_core1_host_peer_result(const era_split_communication_core_initiator_result_t *result) {
    /* Lane facts only. Since D3 a response on this lane carries no section, so
       everything read here comes from the *request* -- what was sent, and
       whether the exchange completed -- and nothing from the answer's body. The
       section set arrives on the standing exchange and is applied from the
       standing state. */
    era_split_transaction_engine_result_t lane_result_code = (era_split_transaction_engine_result_t)result->result;
    bool                                  request_sent    = result->request_sent != 0;

    /* There is no HEARTBEAT arm here. That lane has had no enqueuer since R2 and
       the arm was reachable by nothing; its counters are kept as capture
       surface -- a counter that must read zero is an instrument -- but the code
       that would have moved them is not capture surface, it is code. */
    if (result->lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH) {
        /* One lane, one section set again since R2, and since D3 the section
           set is the request's alone. The matrix-link bookkeeping belongs to
           the matrix section rather than to the lane, and nothing else can
           arrive on it -- kept because what it asserts is still the rule, and
           now spelled with the constant that says whose lane this is.

           No sent-state shadow retires here in either relation any more.
           DUAL-HOST's INPUT_LAYER shadow moved to core1 at Slice 11.5 and
           HOST-PEER's AUTHORITY shadow followed at R2, both with the send that
           confirms them. */
        if ((result->request_sections & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_CORE0_LANE_SECTIONS) != 0) {
            if (request_sent) {
                era_host_peer_matrix_link_note_source_push_sent();
            }
            if (lane_result_code == ERA_SPLIT_TRANSACTION_RESULT_OK) {
                era_host_peer_matrix_link_note_source_push_accepted(result->matrix_seq);
            }
        }
    }

    /* The time anchor was applied from here rather than from inside the retired
       apply_result(), because this frame held core1's receive instant for the
       exchange and that one did not (R2.1) -- applying the decoded value without
       it was how this path once carried the pre-R2 raw form while its twin was
       corrected. The standing path had solved the same problem earlier and
       better, by storing its own receive instant beside the value in the record,
       so the anchor needs no frame to be applied from at all. With one carrier
       that is the only shape left, and the instant this frame carried went with
       the arm that read it. */
    era_split_transport_scheduler_note_host_peer_request_attempt(lane_result_code, request_sent);
}

/* Core0's half of the initiator lane, and since Slice 11.6 it reports work
   only when there is work. It used to return true for "outstanding, not ready
   yet" -- two predicates and two field clears -- and because
   era_split_transport_scheduler_housekeeping_event_due()
   (../era_split_transport_scheduler.c) also read the pending flag,
   that combination ran a full housekeeping pass per matrix *scan* for the whole
   in-flight window. On a DUAL-HOST Left that was about 160 Hz of the measured
   `hkwork`, which is what made the Left look like the expensive half.

   Core1 already announces: it publishes a result and core0 reads the ring.
   What is left here is the one case core1 cannot announce -- a request it
   consumed without publishing, which is the full-result-ring error path. That
   unsticks on the next authority sample rather than through a poll, because
   the sample's deadline is always in the scheduler's deadline set. */
bool era_split_transport_scheduler_poll_core1_initiator(void) {
    if (!era_split_communication_core_initiator_result_ready()) {
        if (g_era_split_transport_scheduler.core1_initiator_async_pending && !era_split_communication_core_initiator_pending()) {
            g_era_split_transport_scheduler.core1_initiator_async_pending = false;
            g_era_split_transport_scheduler.core1_initiator_pending_lane  = ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_INVALID;
            return true;
        }
        return false;
    }

    era_split_communication_core_initiator_result_t result;
    bool drained = era_split_communication_core_poll_initiator_result(&result);
    bool matches = drained && era_split_transport_scheduler_core1_result_matches(&result);
    g_era_split_transport_scheduler.core1_initiator_async_pending             = false;
    g_era_split_transport_scheduler.core1_initiator_pending_lane              = ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_INVALID;
    if (!matches) {
        g_era_split_transport_scheduler.core1_initiator_peer_known_before_request = false;
        if (drained) {
            era_split_communication_core_note_initiator_result_stale(result.lane);
        }
        return true;
    }

    if (result.lane == ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS) {
        era_split_transport_scheduler_apply_core1_session_result(&result);
    } else {
        era_split_transport_scheduler_apply_core1_host_peer_result(&result);
    }
    g_era_split_transport_scheduler.core1_initiator_peer_known_before_request = false;
    return true;
}

/* Two arms, and after R2 that is the whole of what core0 puts on a wire it
   owns: the revalidation frame, and this relation's matrix. The heartbeat and
   the runtime-push arms retired with the routes that selected them -- both
   relations' runtime traffic is core1's now, executed from the published grant
   without passing through an owner route at all. */
void era_split_transport_scheduler_execute_owner_route(void) {
    switch (g_era_split_transport_scheduler.last_owner_route) {
        case ERA_SPLIT_ROUTE_HOST_PEER_SOURCE_PUSH:
            (void)era_split_transport_scheduler_try_enqueue_host_peer_source_push();
            break;
        case ERA_SPLIT_ROUTE_ATTACH_STATUS:
            (void)era_split_transport_scheduler_try_enqueue_attach_status();
            break;
        default:
            break;
    }
}
