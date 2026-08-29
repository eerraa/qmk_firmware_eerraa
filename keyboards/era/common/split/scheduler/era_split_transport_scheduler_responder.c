// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "../era_split_transport_scheduler.h"

#include <string.h>

#include "atomic_util.h"
#include "era_split_transport_scheduler_internal.h"
#include "../communication_core/era_split_communication_core_owner.h"
#include "../communication_core/era_split_communication_core_responder.h"
#include "../era_host_peer_matrix_link.h"
#include "../era_split_restart_agreement.h"
#include "../era_split_sync_policy.h"
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "../era_host_peer_storage.h"
#endif
#include "../era_host_peer_transaction.h"
#include "../era_split_peer_layer.h"
#include "../era_split_responder_projection.h"
#include "../era_split_tap_activity.h"
#include "../era_split_scheduler_session.h"
#include "../era_split_wire_payload.h"
#include "action_layer.h"
#include "host.h"

static bool era_split_transport_scheduler_communication_core_responder_owned(void) {
    return g_era_split_transport_scheduler.local_wire_available &&
           !g_era_split_transport_scheduler.local_wire_initiator &&
           era_split_communication_core_owner_current() == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1 &&
           era_split_communication_core_owner_current_role() == ERA_SPLIT_TRANSACTION_BACKEND_ROLE_RESPONDER;
}

static void era_split_transport_scheduler_snapshot_response_plan(era_split_communication_core_responder_snapshot_t *snapshot) {
    /* Relation-neutral: the plan is computed the same way in both relations
       and the eligibility clip decides what survives. The guard is an
       admission question, not a section one -- an unadmitted half publishes no
       plan at all rather than advertising sections it may not answer with. */
    if (snapshot == NULL || (!snapshot->host_matrix_admitted && !snapshot->runtime_allowed)) {
        return;
    }

    /* Each input is read only where its own section is eligible. That is not
       tidiness: this publish is reached once per answered request, so on a
       DUAL-HOST Right -- where LOCK_STATE is ineligible -- an ungated read is a
       host LED read paid at the poll rate for a value that is discarded three
       lines later. The eligibility mask is already resolved on this path, so
       gating costs one AND.

       **The storage mask is eligible in that relation and always has been**
       (`ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_RSP`); this comment claimed
       otherwise and was the source of the misreading that DUAL-HOST does not
       carry the section at all. Its gate below is real work either way: a
       policy bit read and a news read that a HOST-PEER PEER must not pay. */
    uint8_t lock_state_bits = 0;
    if ((snapshot->eligible_rsp_sections & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE) != 0) {
        lock_state_bits = host_keyboard_leds() & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_VALUE_MASK;
    }
    uint8_t storage_news = 0;
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    if ((snapshot->eligible_rsp_sections & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS) != 0) {
        /* The single-field accessor, the same one the INPUT bit below uses.
           The whole-snapshot form took a struct copy and a three-field loop to
           read one bit, on a publish reached once per answered request. */
        bool eeprom_sync_requested = false;
        if (era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_EEPROM, &eeprom_sync_requested) &&
            eeprom_sync_requested) {
            storage_news = era_host_peer_storage_settled_news_value();
        }
        /* The responder's own pending arm, on bit7 of the same byte -- the
           response direction's half of the pair fact the push
           STORAGE_PENDING section carries the other way (entry symmetry,
           2026-08-14). Deliberately outside the policy branch above: the
           accessor is gated in the engine on serviceability AND this half's
           policy -- the same cached gate the lamp itself reads -- so a
           policy-off half advertises zero through the door that keeps its
           own lamp dark, and a second policy read here would be the same
           fact fetched twice. */
        bool storage_pending = era_host_peer_storage_advertised_pending();
#    ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
        era_host_peer_storage_cause_note_advertised(storage_pending);
#    endif
        if (storage_pending) {
            storage_news |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_FLAG_PENDING;
        }
    }
#endif
    /* The policy gate's sender arm for the INPUT-class family (storage
       version 4), read once behind the family's eligibility so a HOST-PEER
       HOST never pays it. An off half substitutes the neutral values --
       layer 0 and the zero activity record -- instead of withholding the
       sections: transient correctness state the receiver holds applied must
       retire through the apply path, and the sent shadows make each neutral
       cross exactly once. */
    bool input_sync_requested = false;
    if ((snapshot->eligible_rsp_sections & (ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER |
                                            ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY)) != 0) {
        (void)era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_INPUT, &input_sync_requested);
    }
    /* One byte from a global, on the same cold publish that already reads
       host_keyboard_leds() and the storage news value -- called the
       settled-dirty mask here until D2 replaced the mask with a counter, and
       the read is the same one either way. It is a cached scalar fact in the
       sense the hot-path gate means, not a capture. */
    uint8_t input_layer = input_sync_requested ? (uint8_t)layer_state : 0;

    /* The same cached session facts the snapshot's own local_session_status was
       built from a few lines up, read again through the authority accessor so
       the section and the frame cannot describe this half differently. */
    era_split_wire_authority_section_t authority;
    bool authority_valid = (snapshot->eligible_rsp_sections & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY) != 0 &&
                           era_split_scheduler_session_get_local_authority(&authority);
    /* This half's restart intent, filled into the same body immediately after
       the session facts and never through the session cache. The two are
       separate facts sharing one carrier because the response mask has no
       marker left, and keeping the fill here is what stops a restart intent
       from reading as a peer-session edge on the receiving side. */
    if (authority_valid) {
        era_split_restart_agreement_fill_authority(&authority);
    }

    /* The tap-hold activity (FA-2 S2), behind its own eligibility like every
       other input here. The accessor returns the advertised composition --
       live fields only while the peer's window is up, frozen otherwise -- so
       it is stable between publishes exactly as the plan discipline needs. */
    era_split_wire_activity_section_t activity;
    bool activity_valid = false;
    if ((snapshot->eligible_rsp_sections & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY) != 0) {
        if (input_sync_requested) {
            era_split_tap_activity_wire_value(&activity);
        } else {
            /* The neutral record: valid in both states so an off half's zero
               body crosses once and retires the peer's cached window flag. */
            memset(&activity, 0, sizeof(activity));
        }
        activity_valid = true;
    }

    era_host_peer_transaction_responder_response_plan_t plan;
    era_host_peer_transaction_prepare_responder_response(lock_state_bits, storage_news, input_layer,
                                                         authority_valid ? &authority : NULL,
                                                         activity_valid ? &activity : NULL,
                                                         snapshot->eligible_rsp_sections, &plan);
    snapshot->lock_state_bits = plan.lock_state_bits;
    if (plan.send_input_layer) {
        snapshot->input_layer = plan.input_layer;
        snapshot->response_section_mask |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER;
    }
    if (plan.send_authority) {
        snapshot->authority = plan.authority;
        snapshot->response_section_mask |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY;
    }
    if (plan.send_activity) {
        snapshot->activity = plan.activity;
        snapshot->response_section_mask |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY;
    }
    if (plan.send_lock_state) {
        snapshot->response_section_mask |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE;
    }
    if (plan.send_visual_snapshot) {
        snapshot->visual_baseline_valid = 1;
        snapshot->visual_reason         = plan.visual_snapshot.reason;
        memcpy(snapshot->visual_baseline, plan.visual_snapshot.pressed_baseline, sizeof(snapshot->visual_baseline));
        snapshot->response_section_mask |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC;
    }
    if (plan.send_rgb_state) {
        snapshot->rgb_state_valid = 1;
        snapshot->rgb_state       = plan.rgb_state;
        snapshot->response_section_mask |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE;
    }
    if (plan.send_storage_news) {
        snapshot->storage_news = plan.storage_news;
        snapshot->response_section_mask |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS;
    }
    if (plan.send_time_anchor) {
        snapshot->time_anchor_ms       = plan.time_anchor_ms;
        snapshot->time_anchor_stamp_us = plan.time_anchor_stamp_us;
        snapshot->response_section_mask |= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_TIME_ANCHOR;
    }
}

bool era_split_transport_scheduler_publish_communication_core_responder_snapshot(void) {
    if (!g_era_split_transport_scheduler.initialized ||
        !era_split_transport_scheduler_communication_core_responder_owned()) {
        /* A half that cannot publish must not hold housekeeping open for a
           snapshot it does not own: the value is latest-state, and every
           role/mode edge republishes through the dirty-flag path. */
        g_era_split_transport_scheduler.responder_snapshot_publish_due = false;
        /* Clear the advertised-mask shadow with the role. It is written only on
           a successful publish, so a half that loses the responder role would
           otherwise keep its last non-zero value for ever and make every later
           flash write attempt a park that this same early return refuses. */
        return false;
    }
    /* Consume the due latch before building: a producer firing mid-build
       re-marks and the next pass republishes; consuming after the build would
       eat that mark. */
    g_era_split_transport_scheduler.responder_snapshot_publish_due = false;

    era_split_communication_core_responder_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.owner_epoch         = era_split_communication_core_owner_epoch();
    snapshot.relation_generation = g_era_split_transport_scheduler.core1_initiator_relation_generation;
    snapshot.owner_gate_ready    = snapshot.owner_epoch != 0 && snapshot.relation_generation != 0;
    snapshot.session_allowed     = era_split_scheduler_session_build_local_status(false, &snapshot.local_session_status) &&
                                   snapshot.local_session_status.accepted_host_open != snapshot.local_session_status.accepted_no_host;
    snapshot.eligible_push_sections = era_split_wire_eligible_sections((uint8_t)g_era_split_transport_scheduler.mode,
                                                                       ERA_SPLIT_WIRE_SECTION_DIRECTION_PUSH);
    snapshot.eligible_rsp_sections  = era_split_wire_eligible_sections((uint8_t)g_era_split_transport_scheduler.mode,
                                                                       ERA_SPLIT_WIRE_SECTION_DIRECTION_RSP);
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    const bool storage_exclusive = era_host_peer_storage_route_exclusive();
#else
    const bool storage_exclusive = false;
#endif
    /* The relation fact, held separately from the payload admission derived
       from it, because only one of the two may carry storage exclusivity. This
       half is a confirmed HOST-PEER HOST whose session admits its PEER's
       matrix; whether it may *accept rows right now* is the next question and
       not this one. */
    const bool host_peer_relation_admitted = g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_HOST_PEER_HOST &&
                                             era_split_scheduler_session_host_peer_host_matrix_admitted();
    /* The one admission storage exclusivity genuinely closes, and it is a
       payload question rather than an answering one: the exclusive close
       flushes the matrix relation on both roles and forces a fresh source-push
       baseline, so rows accepted mid-transfer are rows the close is about to
       discard (era_route_contract.md). */
    snapshot.host_matrix_admitted = host_peer_relation_admitted && !storage_exclusive;
    /* A confirmed DUAL-HOST Right may answer the initiator's runtime slot. The
       surrounding generation checks -- owner epoch, relation generation,
       snapshot generation -- are what make "confirmed" mean something; this
       field only says which relation this half is in.

       **It carries no exclusivity term, and that is the Slice 11.7 fix.** It
       used to, and a busy responder then answered nothing at all -- which the
       initiator cannot tell from a dead wire, because a refused frame and a
       dead wire look identical to it. Its standing exchange stopped on that
       first failure, core0 turned the stop into a teardown, and the teardown
       aborted the transfer that caused the refusal: one VIA keymap edit on the
       responder cost 645 refusals and 577 forgotten sessions in 35.5 s,
       device-measured 2026-08-01.

       What the contract actually suppresses under exclusivity is *owner
       routes* -- routes an initiator selects -- and it says so. Nothing there
       asks the responder to go mute, and the plan below is where the
       suppression belongs: the slot is answered, and it carries no section. */
    snapshot.runtime_allowed = g_era_split_transport_scheduler.mode == ERA_SPLIT_MODE_DUAL_HOST_RIGHT ? 1 : 0;
    /* Derived from the eligibility table rather than from a mode list, because
       the question it answers is the table's: does this relation carry any
       push section that is not the matrix? DUAL-HOST does (INPUT_LAYER,
       AUTHORITY, and RGB since Slice 12) and HOST-PEER does since Slice 11.6
       (AUTHORITY). A relation that carries none can never reach this arm
       anyway, so the derivation cannot open a surface the table closes.

       Exclusivity came off this one too, and for the same reason plus a second
       door: the initiator's frame carries a section only when one differs, so
       a layer edge during a transfer arrives as a push rather than as a bare
       poll. Refusing that is the identical collapse, reached by holding a
       layer key while a keymap converges. No section it can carry touches
       storage -- a layer byte, the peer's session facts, and a runtime RGB
       config whose apply is RAM-only -- and the session facts are news this
       half must not hold back for a transfer. */
    snapshot.runtime_push_allowed = (snapshot.session_allowed &&
                                     (snapshot.eligible_push_sections & (uint8_t)~ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MATRIX) != 0)
                                        ? 1
                                        : 0;
    /* Both relations answer a heartbeat; only HOST-PEER answers a matrix.
       **The HOST-PEER term is the relation fact, not the matrix admission**,
       and that is the Slice 11.7 rule finally reaching this relation. The
       matrix gate carries storage exclusivity by the payload rule above, so
       deriving the heartbeat from it made a busy HOST-PEER HOST refuse the
       section-less ACK — the exact behaviour 11.7 removed from DUAL-HOST, left
       standing here only because that lane's fix went in through
       `runtime_allowed`, which this relation does not set.

       Latent until now: a HOST-PEER PEER suppresses its own polls during
       exclusivity, so only an onset race reached it. It stops being latent the
       moment a liveness beat runs *through* an apply, which the core1 standing
       exchange now does (`era_route_contract.md`, and
       ERA_SPLIT_STANDING_LIVENESS_MS in
       `scheduler/era_split_transport_scheduler_internal.h`) — every beat would
       have been refused, the standing
       exchange would have stopped on the first one, and the collapse 11.7 fixed
       would have replayed here through the mechanism meant to close it.

       Nothing crosses that could not before: the response plan is still
       suppressed under exclusivity, so an admitted slot is answered with the
       section-less ACK and carries no section by construction. */
    snapshot.heartbeat_allowed = host_peer_relation_admitted || snapshot.runtime_allowed;
    /* There is no source_push_allowed beside it and there was no need for one:
       it was assigned verbatim from host_matrix_admitted here, its only
       producer, and both consumers read the two names in the same &&. Retired
       2026-08-11. Its two neighbours are NOT this shape -- heartbeat_allowed
       above and runtime_push_allowed each carry their own derivation, and the
       comments at both record device incidents caused by folding them
       together. */
    /* The suppression, in the one place that cannot leak: no plan means no
       section byte, and the responder's answer falls back to the one-byte
       compact control ACK it already sends on a quiet poll. The admitted slot
       stays a slot -- answering it under exclusivity is not a way for a
       runtime section to cross during a transfer, because there is nothing to
       cross. Every section here is latest-state and edge-armed, so each stays
       due and goes out on the first poll after the transfer; the sent-state
       shadows advance from the wire's own section byte and never from a plan,
       so a suppressed section cannot retire silently. */
    if (!storage_exclusive) {
        era_split_transport_scheduler_snapshot_response_plan(&snapshot);
    }
    if (!era_split_communication_core_publish_responder_snapshot(&snapshot)) {
        /* A differing snapshot failed to store -- a core1 claim collision or
           an undrained result. The published value is stale, which is exactly
           what the latch means, so re-arm and retry next pass. The abort
           conditions themselves are load-bearing (results are matched by
           snapshot generation) and stay untouched. */
        ATOMIC_BLOCK_RESTORESTATE {
            g_era_split_transport_scheduler.responder_snapshot_publish_due = true;
            g_era_split_transport_scheduler.next_scheduler_deadline_valid  = false;
        }
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
        g_era_split_transport_scheduler.responder_snapshot_publish_retry_count++;
#endif
        return false;
    }
    return true;
}

static void era_split_transport_scheduler_commit_communication_core_host_response(const era_split_communication_core_responder_result_t *result) {
    era_host_peer_transaction_responder_response_t response = {
        .result               = (era_split_transaction_engine_result_t)result->result,
        .ack_status_sent      = result->response_sent != 0,
        .lock_state_sent      = result->response_sent && (result->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE) != 0,
        .visual_snapshot_sent = result->response_sent && (result->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC) != 0,
        .rgb_state_sent       = result->response_sent && (result->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE) != 0,
        .storage_news_sent = result->response_sent && (result->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS) != 0,
        .time_anchor_sent     = result->response_sent && (result->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_TIME_ANCHOR) != 0,
        /* The section mask here is the byte the wire actually carried, not the
           mask the snapshot advertised, so a section the eligibility clip or
           the frame budget dropped never advances its sent-state shadow. */
        .input_layer_sent     = result->response_sent && (result->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER) != 0,
        .authority_sent       = result->response_sent && (result->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY) != 0,
        .activity_sent        = result->response_sent && (result->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY) != 0,
    };
    era_host_peer_transaction_commit_responder_response(&result->response_plan, &response);
    if (response.ack_status_sent) {
        era_host_peer_matrix_link_note_ack_status_sent();
    }
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    /* Counted from the section byte the wire carried, not from the plan: a
       response that dropped its runtime section must not read as one that sent
       it, or the silence leg passes on a frame it should have counted.

       `rt` counts runtime *sections*, never polls, and that is what keeps the
       steady-state leg readable under a constant cadence: an idle window still
       reads rt=0/0 while `io` rises at the poll rate. The ACTIVITY marker used
       to count here too, which would have broken exactly that. */
    if (result->response_sent &&
        (result->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_INPUT_LAYER) != 0) {
        g_era_split_transport_scheduler.dual_runtime_tx_count++;
    }
    if (result->response_sent &&
        (result->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY) != 0) {
        g_era_split_transport_scheduler.dual_runtime_tx_count++;
    }
    if (result->response_sent &&
        (result->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE) != 0) {
        g_era_split_transport_scheduler.dual_runtime_tx_count++;
    }
    if (result->response_sent &&
        (result->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_ACTIVITY) != 0) {
        g_era_split_transport_scheduler.dual_runtime_tx_count++;
        era_split_tap_activity_note_sent();
    }
#endif
}

static void era_split_transport_scheduler_apply_communication_core_responder_result(const era_split_communication_core_responder_result_t *result) {
    bool session     = result->kind == ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_SESSION;
    bool source_push = result->kind == ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_SOURCE_PUSH;
    if (session) {
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
        uint8_t cause_detail = (uint8_t)result->result & 0x07U;
        if (result->response_sent) {
            cause_detail |= 0x08U;
        }
        era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_SESSION_RX, cause_detail);
#endif
        era_split_scheduler_session_note_peer_status(&result->peer_status);
    } else {
        /* One eligibility read for the whole result. Each section arm below
           used to take its own, five identical calls into another translation
           unit that nothing folds -- the accessor is neither static nor pure
           and this image links without LTO. Reading it once is also the
           stricter answer: `mode` is core0 state the planner rewrites, so five
           reads could in principle clip two arms of one frame against two
           different tables. Hoisted into this arm and not to the top of the
           function, so the SESSION arm above still pays nothing. */
        const uint8_t eligible_push = era_split_wire_eligible_sections((uint8_t)g_era_split_transport_scheduler.mode,
                                                                       ERA_SPLIT_WIRE_SECTION_DIRECTION_PUSH);
        if (source_push) {
            (void)era_host_peer_matrix_link_accept_source_push_packed(result->packed_matrix);
        }
        /* The accept-clip on this side. Core1 already refused a section its
           relation does not carry, and this repeats the question against the
           same table on core0 rather than trusting the frame -- the value
           shifts which keycode this half resolves, which is not a place to
           rely on one check. */
        if (result->peer_input_layer_valid &&
            (eligible_push & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER) != 0) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
            g_era_split_transport_scheduler.dual_runtime_rx_count++;
#endif
            /* Arrival before the gate (the RGB arm's order): rt moves either
               way, the apply and its counter only when this half's own INPUT
               bit requests it (storage version 4). */
            bool input_sync_requested = false;
            if (era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_INPUT, &input_sync_requested) && input_sync_requested) {
                if (era_split_peer_layer_apply((layer_state_t)result->peer_input_layer)) {
                    g_era_split_transport_scheduler.host_peer_input_layer_apply_count++;
                }
            }
        }
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
        /* The initiator's storage-pending fact, on the same accept-clip,
           latched into the engine's mirror — the EEPROM SYNC lamp's peer
           arm. A latch write is idempotent, so the latest-state record
           re-handing it on an unrelated edge costs nothing; the engine
           clears the mirror itself when the relation leaves service. */
        if (result->peer_storage_pending_valid &&
            (eligible_push & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING) != 0) {
            era_host_peer_storage_note_peer_pending(result->peer_storage_pending != 0);
#    ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
            g_era_split_transport_scheduler.dual_runtime_rx_count++;
#    endif
        }
#endif
        /* The initiator's authority, on the same accept-clip. It reaches the
           same cache SESSION_STATUS writes, which is what lets that frame stop
           running post-relation. */
        /* The initiator's restart arm, on the same accept-clip. This is the
           responder's whole half of the agreement: it arms on what the
           initiator advertises and, for an act that needs confirming, disarms
           when the initiator retires it -- so the deadline has exactly one
           owner and a lost confirmation cannot leave this half resetting
           alone. */
        if (result->peer_restart_valid &&
            (eligible_push & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM) != 0) {
            era_split_restart_agreement_note_peer_arm(result->peer_restart_act, result->peer_restart_param, result->peer_restart_commit_ms);
        }
        if (result->peer_authority_valid &&
            (eligible_push & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY) != 0) {
            era_split_restart_agreement_note_peer_authority(&result->peer_authority);
            /* No `app=auth` arm here, deliberately: that counter has one
               producer, the standing apply, and one producer is what makes it
               path-complete by construction (era_capture_reading.md). */
            (void)era_split_scheduler_session_note_peer_authority(&result->peer_authority);
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
            g_era_split_transport_scheduler.dual_runtime_rx_count++;
#endif
        }
        /* The initiator's RGB config from the push cell (Slice 12), on the
           same accept-clip. The arrival counts before the policy gate and the
           apply counts after it, which is what makes "sent but not applied"
           read differently from "applied" on the console: `rt` moves either
           way, `app=rgb` only on an apply. The receiver's own requested bit is
           the gate -- the EEPROM precedent's apply arm -- and the sleep bit is
           skipped, both per era_authority_contract.md. */
        if (result->peer_rgb_state_valid &&
            (eligible_push & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RGB_STATE) != 0) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
            g_era_split_transport_scheduler.dual_runtime_rx_count++;
#endif
            bool rgb_requested = false;
            if (era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_RGB, &rgb_requested) && rgb_requested) {
                if (era_host_peer_transaction_apply_rgb_state(&result->peer_rgb_state, false)) {
                    g_era_split_transport_scheduler.host_peer_rgb_state_rx_count++;
                }
            }
        }
        /* The initiator's tap-hold activity (FA-2 S2), on the same accept-clip.
           The arrival counts on `rt`'s rx side; the apply counts inside the tap
           activity unit, so a body the unit refuses reads as "sent but not
           applied" on the console rather than as silence. */
        if (result->peer_activity_valid &&
            (eligible_push & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY) != 0) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
            g_era_split_transport_scheduler.dual_runtime_rx_count++;
#endif
            /* Behind the INPUT-class apply arm, the layer byte's twin. */
            bool input_sync_requested = false;
            if (era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_INPUT, &input_sync_requested) && input_sync_requested) {
                era_host_peer_transaction_apply_activity(&result->peer_activity);
            }
        }
        /* The initiator's visual baseline from the push cell (Slice 14), on
           the same accept-clip then the RGB policy's apply arm. Per-exchange
           results carry no sequence -- each result is one arrival -- so the
           diff-replay apply runs at most once per received frame. */
        if (result->peer_visual_valid &&
            (eligible_push & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL) != 0) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
            g_era_split_transport_scheduler.dual_runtime_rx_count++;
#endif
            bool rgb_requested = false;
            if (era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_RGB, &rgb_requested) && rgb_requested) {
                era_host_peer_transaction_apply_visual_snapshot(&result->peer_visual_snapshot);
                g_era_split_transport_scheduler.host_peer_visual_snapshot_rx_count++;
            }
        }
        era_split_transport_scheduler_commit_communication_core_host_response(result);
    }
    era_split_responder_projection_note_result(session,
                                                       source_push,
                                                       result->response_sent != 0,
                                                       result->response_section_mask,
                                                       result->result);
}

/* The bulk fold for requests core1 answered without publishing a result. They
   are real received frames and real sent ACKs, so the projection must count
   them -- but counting them one at a time is the per-exchange core0 wake that
   skipping the publish removed. So they arrive as a delta, at whatever cadence
   this drain happens to run, and are at most one pass stale at print time.

   It returns nothing and deliberately does not report "work performed": a fold
   that marked the housekeeping pass as productive would re-arm the very wake
   this exists to avoid. */
static void era_split_transport_scheduler_fold_quiet_responses(void) {
    uint32_t current = era_split_communication_core_responder_quiet_count();
    uint32_t folded  = g_era_split_transport_scheduler.responder_quiet_folded;
    if (current == folded) {
        return;
    }
    uint32_t delta = current - folded;
    g_era_split_transport_scheduler.responder_quiet_folded = current;
    era_split_responder_projection_note_quiet(delta);
    era_host_peer_matrix_link_note_ack_status_sent_bulk(delta);
}

bool era_split_transport_scheduler_drain_communication_core_responder_results(void) {
    era_split_transport_scheduler_fold_quiet_responses();
    bool drained_any = false;
    era_split_communication_core_responder_result_t result;
    while (era_split_communication_core_drain_responder_result(&result)) {
        drained_any = true;
        if (era_split_communication_core_responder_result_matches_current(&result)) {
            era_split_transport_scheduler_apply_communication_core_responder_result(&result);
        }
    }
    return drained_any;
}
