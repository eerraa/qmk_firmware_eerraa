// SPDX-License-Identifier: GPL-2.0-or-later
#include "era_tomak_sync_receiver.h"

bool era_tomak_sync_binding_equal(const era_tomak_sync_binding_t *a, const era_tomak_sync_binding_t *b) {
    return a->local_owner == b->local_owner && a->local_relation == b->local_relation &&
           a->peer_owner == b->peer_owner && a->peer_relation == b->peer_relation &&
           a->peer_usb_epoch == b->peer_usb_epoch && a->peer_host_open == b->peer_host_open &&
           a->peer_host_close == b->peer_host_close;
}

void era_tomak_sync_receiver_bind(era_tomak_sync_receiver_t *receiver, const era_tomak_sync_binding_t *binding) {
    if (era_tomak_sync_binding_equal(&receiver->binding, binding)) {
        return;
    }
    receiver->binding = *binding;
    receiver->proof = (era_tomak_sync_wire_t){0};
    /* Peer-session replacement also revokes the old edge, even if the local
     * lease survived it. Preserve only the physical level already output. */
    bool displayed = receiver->edge.displayed;
    receiver->edge = (era_tomak_sync_transition_t){.displayed = displayed};
    era_tomak_sync_transition_bind(&receiver->edge, binding->local_owner, binding->local_relation);
}

bool era_tomak_sync_receiver_accept(era_tomak_sync_receiver_t *receiver, const era_tomak_sync_binding_t *captured,
                                    const era_tomak_sync_wire_t *request, uint32_t shared_now, uint32_t local_now,
                                    uint16_t min_lead_ms, uint16_t max_lead_ms) {
    if (!era_tomak_sync_binding_equal(&receiver->binding, captured) ||
        !captured->local_owner || !captured->local_relation || !captured->peer_owner || !captured->peer_relation ||
        request->owner_epoch != captured->peer_owner || request->relation_generation != captured->peer_relation) {
        return false;
    }
    bool accepted;
    uint8_t response;
    if (request->operation == ERA_TOMAK_SYNC_WIRE_OFFER) {
        accepted = era_tomak_sync_transition_install(&receiver->edge, captured->local_owner, captured->local_relation,
                                                     request->sequence, request->target, request->deadline_ms,
                                                     shared_now, local_now, min_lead_ms, max_lead_ms);
        response = ERA_TOMAK_SYNC_WIRE_INSTALLED;
    } else if (request->operation == ERA_TOMAK_SYNC_WIRE_ARM) {
        /* An ARM cannot switch a target under the accepted tuple. */
        accepted = receiver->edge.target == request->target &&
                   era_tomak_sync_transition_arm(&receiver->edge, captured->local_owner, captured->local_relation,
                                                 request->sequence, request->deadline_ms);
        response = receiver->proof.operation == ERA_TOMAK_SYNC_WIRE_PAIR_READY ?
                       ERA_TOMAK_SYNC_WIRE_PAIR_READY :
                   receiver->edge.applied_sequence == request->sequence ?
                       ERA_TOMAK_SYNC_WIRE_APPLIED : ERA_TOMAK_SYNC_WIRE_ARMED;
    } else if (request->operation == ERA_TOMAK_SYNC_WIRE_APPLIED) {
        /* On requests, APPLIED declares that the coordinator has observed
         * both outputs. Core1 delivery alone cannot promote this proof. */
        accepted = receiver->edge.phase == ERA_TOMAK_EDGE_IDLE &&
                   receiver->edge.applied_sequence == request->sequence &&
                   receiver->edge.displayed == request->target &&
                   era_tomak_sync_wire_matches(request, &receiver->proof, receiver->proof.operation);
        response = ERA_TOMAK_SYNC_WIRE_PAIR_READY;
    } else {
        return false;
    }
    if (!accepted) {
        return false;
    }
    /* A late OFFER retry may obtain its installation receipt, but must not
     * demote a stronger proof already published for ARM/output. */
    if (request->operation != ERA_TOMAK_SYNC_WIRE_OFFER ||
        !era_tomak_sync_wire_matches(request, &receiver->proof, receiver->proof.operation) ||
        receiver->proof.operation == ERA_TOMAK_SYNC_WIRE_INSTALLED) {
        receiver->proof = *request;
        receiver->proof.operation = response;
    }
    return true;
}

void era_tomak_sync_receiver_output(era_tomak_sync_receiver_t *receiver, bool succeeded) {
    bool owned = receiver->edge.phase == ERA_TOMAK_EDGE_OUTPUT;
    era_tomak_sync_transition_output(&receiver->edge, succeeded);
    if (owned) {
        receiver->proof.operation = succeeded ? ERA_TOMAK_SYNC_WIRE_APPLIED : ERA_TOMAK_SYNC_WIRE_REJECT;
    }
}

bool era_tomak_sync_coordinator_request(era_tomak_sync_receiver_t *endpoint, uint8_t *peer_progress, bool work,
                                       uint32_t shared_now, uint32_t local_now, uint16_t lead_ms,
                                       era_tomak_sync_wire_t *request) {
    era_tomak_sync_transition_t *edge = &endpoint->edge;
    if (!endpoint->binding.peer_owner || !endpoint->binding.peer_relation ||
        !edge->owner_epoch || !edge->relation_generation || edge->phase == ERA_TOMAK_EDGE_FAILED) {
        return false;
    }
    if (edge->phase == ERA_TOMAK_EDGE_IDLE &&
        (!edge->sequence || *peer_progress == ERA_TOMAK_SYNC_WIRE_PAIR_READY) &&
        (work != edge->displayed || (work && !edge->sequence))) {
        if (edge->sequence == UINT16_MAX ||
            !era_tomak_sync_transition_install(edge, edge->owner_epoch, edge->relation_generation,
                                               edge->sequence + 1U, work, shared_now + lead_ms,
                                               shared_now, local_now, lead_ms, lead_ms)) {
            return false;
        }
        *peer_progress = 0;
        endpoint->proof = (era_tomak_sync_wire_t){
            .owner_epoch = edge->owner_epoch,
            .relation_generation = edge->relation_generation,
            .sequence = edge->sequence,
            .deadline_ms = edge->wire_ms,
            .target = edge->target,
            .operation = ERA_TOMAK_SYNC_WIRE_INSTALLED,
        };
    }
    if (!edge->sequence || (edge->phase == ERA_TOMAK_EDGE_IDLE && *peer_progress == ERA_TOMAK_SYNC_WIRE_PAIR_READY)) {
        return false;
    }
    if ((int32_t)(local_now - edge->local_ms) >= 0 && edge->phase == ERA_TOMAK_EDGE_INSTALLED) {
        era_tomak_sync_transition_cancel(edge);
        endpoint->proof.operation = ERA_TOMAK_SYNC_WIRE_REJECT;
        return false;
    }
    *request = endpoint->proof;
    request->operation = *peer_progress == 0 ? ERA_TOMAK_SYNC_WIRE_OFFER :
        (*peer_progress == ERA_TOMAK_SYNC_WIRE_APPLIED && edge->phase == ERA_TOMAK_EDGE_IDLE &&
         edge->applied_sequence == edge->sequence) ? ERA_TOMAK_SYNC_WIRE_APPLIED : ERA_TOMAK_SYNC_WIRE_ARM;
    return true;
}

bool era_tomak_sync_coordinator_reply(era_tomak_sync_receiver_t *endpoint, uint8_t *peer_progress,
                                     const era_tomak_sync_wire_t *reply, uint32_t local_now) {
    if (endpoint->edge.phase == ERA_TOMAK_EDGE_FAILED ||
        !era_tomak_sync_wire_matches(&endpoint->proof, reply, reply->operation)) {
        return false;
    }
    if (reply->operation == ERA_TOMAK_SYNC_WIRE_PENDING) {
        return true; /* Delivery only: no state/phase advancement. */
    }
    if (reply->operation == ERA_TOMAK_SYNC_WIRE_REJECT) {
        era_tomak_sync_transition_cancel(&endpoint->edge);
        endpoint->proof.operation = ERA_TOMAK_SYNC_WIRE_REJECT;
        return true;
    }
    if (reply->operation == ERA_TOMAK_SYNC_WIRE_INSTALLED) {
        if (*peer_progress == 0) {
            *peer_progress = ERA_TOMAK_SYNC_WIRE_INSTALLED;
        }
        return true;
    }
    if (reply->operation == ERA_TOMAK_SYNC_WIRE_PAIR_READY) {
        if (endpoint->edge.phase != ERA_TOMAK_EDGE_IDLE ||
            endpoint->edge.applied_sequence != endpoint->edge.sequence ||
            (*peer_progress != ERA_TOMAK_SYNC_WIRE_APPLIED && *peer_progress != ERA_TOMAK_SYNC_WIRE_PAIR_READY)) {
            return false;
        }
        *peer_progress = ERA_TOMAK_SYNC_WIRE_PAIR_READY;
        return true;
    }
    if (*peer_progress == 0 ||
        (reply->operation != ERA_TOMAK_SYNC_WIRE_ARMED && reply->operation != ERA_TOMAK_SYNC_WIRE_APPLIED)) {
        return false;
    }
    if (endpoint->edge.phase == ERA_TOMAK_EDGE_INSTALLED) {
        if ((int32_t)(local_now - endpoint->edge.local_ms) >= 0) {
            era_tomak_sync_transition_cancel(&endpoint->edge);
            endpoint->proof.operation = ERA_TOMAK_SYNC_WIRE_REJECT;
            return false;
        }
        (void)era_tomak_sync_transition_arm(&endpoint->edge, endpoint->edge.owner_epoch, endpoint->edge.relation_generation,
                                           endpoint->edge.sequence, endpoint->edge.wire_ms);
    }
    if (*peer_progress != ERA_TOMAK_SYNC_WIRE_APPLIED && *peer_progress != ERA_TOMAK_SYNC_WIRE_PAIR_READY) {
        *peer_progress = reply->operation;
    }
    return true;
}

static bool output_ready(const era_tomak_sync_receiver_t *endpoint) {
    return endpoint->edge.phase == ERA_TOMAK_EDGE_IDLE && endpoint->edge.displayed &&
           endpoint->edge.sequence != 0 && endpoint->edge.applied_sequence == endpoint->edge.sequence;
}

bool era_tomak_sync_coordinator_ready(const era_tomak_sync_receiver_t *endpoint, uint8_t peer_progress) {
    return output_ready(endpoint) && peer_progress == ERA_TOMAK_SYNC_WIRE_PAIR_READY;
}

bool era_tomak_sync_receiver_ready(const era_tomak_sync_receiver_t *endpoint) {
    return output_ready(endpoint) && endpoint->proof.operation == ERA_TOMAK_SYNC_WIRE_PAIR_READY;
}
