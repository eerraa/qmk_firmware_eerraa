// SPDX-License-Identifier: GPL-2.0-or-later
#include "era_tomak_sync_runtime.h"

#include "era_split_scheduler_session.h"
#include "era_split_scheduler_events.h"
#include "era_split_wire_protocol.h"
#include "era_host_peer_transaction.h"
#include "sync_timer.h"
#include "timer.h"

static era_tomak_sync_receiver_t receiver;
static uint8_t peer_progress;
static bool pair_available;

void era_tomak_sync_runtime_relation(bool paired) {
    if (!paired && pair_available) {
        era_tomak_sync_runtime_preempt();
    }
    pair_available = paired;
}

bool era_tomak_sync_runtime_paired(void) {
    return pair_available;
}

bool era_tomak_sync_runtime_failed(void) {
    return receiver.edge.phase == ERA_TOMAK_EDGE_FAILED;
}

void era_tomak_sync_runtime_bind(uint16_t local_owner, uint16_t local_relation) {
    era_split_scheduler_session_diagnostics_t session;
    era_split_scheduler_session_get_diagnostics_snapshot(&session);
    era_tomak_sync_binding_t binding = {
        .local_owner = local_owner,
        .local_relation = local_relation,
        .peer_usb_epoch = session.peer_usb_epoch,
        .peer_host_open = session.peer_host_open_generation,
        .peer_host_close = session.peer_host_close_generation,
    };
    (void)era_split_scheduler_session_sync_peer_channel(&binding.peer_owner, &binding.peer_relation);
    if (!era_tomak_sync_binding_equal(&receiver.binding, &binding)) {
        peer_progress = 0;
    }
    era_tomak_sync_receiver_bind(&receiver, &binding);
}

void era_tomak_sync_runtime_snapshot(era_tomak_sync_binding_t *binding, era_tomak_sync_wire_t *proof) {
    *binding = receiver.binding;
    *proof = receiver.proof;
}

void era_tomak_sync_runtime_receive(const era_tomak_sync_binding_t *captured, const era_tomak_sync_wire_t *request) {
    /* Installation needs at least one full compact exchange of lead time.
     * The coordinator's larger proposal budget is not a physical-skew claim. */
    if (!era_tomak_sync_receiver_accept(&receiver, captured, request, sync_timer_read32(), timer_read32(),
                                        ERA_SPLIT_PEER_RESPONSE_WINDOW_MS, 6U * ERA_SPLIT_PEER_RESPONSE_WINDOW_MS) &&
        era_tomak_sync_binding_equal(&receiver.binding, captured) &&
        request->owner_epoch == captured->peer_owner && request->relation_generation == captured->peer_relation) {
        receiver.proof = *request;
        receiver.proof.operation = ERA_TOMAK_SYNC_WIRE_REJECT;
    }
}

bool era_tomak_sync_runtime_launch(bool *target) {
    /* A renderer which misses the protected edge must report failure, not
     * replay a supposedly synchronous output late. */
    if (!era_tomak_sync_transition_launch(&receiver.edge, timer_read32(), 2)) {
        if (receiver.edge.phase == ERA_TOMAK_EDGE_FAILED && receiver.proof.operation != ERA_TOMAK_SYNC_WIRE_REJECT) {
            receiver.proof.operation = ERA_TOMAK_SYNC_WIRE_REJECT;
            era_split_transport_scheduler_mark_maintenance_due(ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE);
        }
        return false;
    }
    *target = receiver.edge.target;
    return true;
}

void era_tomak_sync_runtime_output(bool succeeded) {
    era_tomak_sync_receiver_output(&receiver, succeeded);
    era_split_transport_scheduler_mark_maintenance_due(ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE);
}

bool era_tomak_sync_runtime_protected(void) {
    return receiver.edge.phase == ERA_TOMAK_EDGE_INSTALLED ||
           receiver.edge.phase == ERA_TOMAK_EDGE_ARMED || receiver.edge.phase == ERA_TOMAK_EDGE_OUTPUT;
}

bool era_tomak_sync_runtime_request(bool work, era_tomak_sync_wire_t *request) {
    if (!era_host_peer_transaction_time_anchor_adopted()) {
        return false;
    }
    return era_tomak_sync_coordinator_request(&receiver, &peer_progress, work, sync_timer_read32(), timer_read32(),
                                             6U * ERA_SPLIT_PEER_RESPONSE_WINDOW_MS, request);
}

bool era_tomak_sync_runtime_transition_pending(void) {
    return receiver.edge.sequence != 0 && receiver.edge.phase != ERA_TOMAK_EDGE_FAILED &&
        (receiver.edge.phase != ERA_TOMAK_EDGE_IDLE || peer_progress != ERA_TOMAK_SYNC_WIRE_PAIR_READY);
}

void era_tomak_sync_runtime_reply(const era_tomak_sync_wire_t *reply) {
    (void)era_tomak_sync_coordinator_reply(&receiver, &peer_progress, reply, timer_read32());
}

bool era_tomak_sync_runtime_pair_ready(void) {
    return pair_available &&
        (era_tomak_sync_coordinator_ready(&receiver, peer_progress) || era_tomak_sync_receiver_ready(&receiver));
}

bool era_tomak_sync_runtime_controls_output(void) {
    return pair_available && (receiver.edge.sequence != 0 || receiver.edge.displayed);
}

bool era_tomak_sync_runtime_target(void) {
    return era_tomak_sync_runtime_protected() ? receiver.edge.target : receiver.edge.displayed;
}

bool era_tomak_sync_runtime_output_pending(void) {
    return receiver.edge.phase == ERA_TOMAK_EDGE_OUTPUT;
}

void era_tomak_sync_runtime_preempt(void) {
    if (era_tomak_sync_runtime_controls_output()) {
        era_tomak_sync_transition_cancel(&receiver.edge);
        receiver.proof.operation = ERA_TOMAK_SYNC_WIRE_REJECT;
        peer_progress = 0;
        era_split_transport_scheduler_mark_maintenance_due(ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE);
    }
}

int8_t era_tomak_sync_runtime_output_gate(bool matching_frame, bool driver_ready) {
    if (receiver.edge.phase == ERA_TOMAK_EDGE_OUTPUT) {
        return 0;
    }
    if (!era_tomak_sync_runtime_protected()) {
        return 1;
    }
    uint32_t now = timer_read32();
    int32_t late = (int32_t)(now - receiver.edge.local_ms);
    if (late > 2 || (late >= 0 && (!driver_ready || receiver.edge.phase != ERA_TOMAK_EDGE_ARMED))) {
        era_tomak_sync_transition_cancel(&receiver.edge);
        receiver.proof.operation = ERA_TOMAK_SYNC_WIRE_REJECT;
        era_split_transport_scheduler_mark_maintenance_due(ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE);
        return -1;
    }
    if (!matching_frame) {
        return -1; /* Re-render, never submit the previous policy's pixels. */
    }
    if (late < 0) {
        return 0;
    }
    bool target;
    return era_tomak_sync_runtime_launch(&target) ? 1 : -1;
}
