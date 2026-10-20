// SPDX-License-Identifier: GPL-2.0-or-later
#include "era_tomak_sync_transition.h"

static bool identity(const era_tomak_sync_transition_t *edge, uint16_t owner, uint16_t relation) {
    return owner != 0 && relation != 0 && edge->owner_epoch == owner && edge->relation_generation == relation;
}

void era_tomak_sync_transition_bind(era_tomak_sync_transition_t *edge, uint16_t owner, uint16_t relation) {
    if (edge->owner_epoch == owner && edge->relation_generation == relation) {
        return;
    }
    /* Relation loss revokes execution, not the physical output already held.
     * The fault/sleep renderer owns any subsequent unsynchronized clearing. */
    edge->owner_epoch = owner;
    edge->relation_generation = relation;
    edge->sequence = 0;
    edge->applied_sequence = 0;
    edge->phase = ERA_TOMAK_EDGE_IDLE;
}

bool era_tomak_sync_transition_install(era_tomak_sync_transition_t *edge, uint16_t owner, uint16_t relation,
                                      uint16_t sequence, bool target, uint32_t wire_ms,
                                      uint32_t shared_now, uint32_t local_now, uint16_t min_lead_ms, uint16_t max_lead_ms) {
    if (!identity(edge, owner, relation) || sequence == 0) {
        return false;
    }
    if (sequence == edge->sequence) {
        /* Retransmission never reanchors an accepted monotonic deadline. */
        return edge->phase != ERA_TOMAK_EDGE_FAILED && edge->wire_ms == wire_ms && edge->target == target;
    }
    if (edge->phase != ERA_TOMAK_EDGE_IDLE || edge->sequence == UINT16_MAX ||
        sequence != (uint16_t)(edge->sequence + 1U)) {
        /* Rotate relation before sequence reuse; do not alias old receipts. */
        return false;
    }
    int32_t lead_ms = (int32_t)(wire_ms - shared_now);
    if (lead_ms <= 0 || lead_ms < min_lead_ms || lead_ms > max_lead_ms) {
        return false;
    }
    edge->sequence = sequence;
    edge->target = target;
    edge->wire_ms = wire_ms;
    edge->local_ms = local_now + (uint32_t)lead_ms;
    edge->phase = ERA_TOMAK_EDGE_INSTALLED;
    return true;
}

bool era_tomak_sync_transition_arm(era_tomak_sync_transition_t *edge, uint16_t owner, uint16_t relation,
                                  uint16_t sequence, uint32_t wire_ms) {
    if (!identity(edge, owner, relation) || edge->sequence != sequence || edge->wire_ms != wire_ms || sequence == 0) {
        return false;
    }
    if (edge->phase == ERA_TOMAK_EDGE_INSTALLED) {
        edge->phase = ERA_TOMAK_EDGE_ARMED;
    }
    return edge->phase == ERA_TOMAK_EDGE_ARMED || edge->phase == ERA_TOMAK_EDGE_OUTPUT ||
           (edge->phase == ERA_TOMAK_EDGE_IDLE && edge->applied_sequence == sequence);
}

bool era_tomak_sync_transition_launch(era_tomak_sync_transition_t *edge, uint32_t now_ms, uint16_t max_late_ms) {
    if (edge->phase != ERA_TOMAK_EDGE_ARMED) {
        return false;
    }
    int32_t late_ms = (int32_t)(now_ms - edge->local_ms);
    if (late_ms < 0) {
        return false;
    }
    if (late_ms > max_late_ms) {
        /* A missed edge is a failure, not a late synchronized success. */
        edge->phase = ERA_TOMAK_EDGE_FAILED;
        return false;
    }
    edge->phase = ERA_TOMAK_EDGE_OUTPUT;
    return true;
}

void era_tomak_sync_transition_output(era_tomak_sync_transition_t *edge, bool succeeded) {
    if (edge->phase != ERA_TOMAK_EDGE_OUTPUT) {
        return;
    }
    if (!succeeded) {
        edge->phase = ERA_TOMAK_EDGE_FAILED;
        return;
    }
    edge->displayed = edge->target;
    edge->applied_sequence = edge->sequence;
    edge->phase = ERA_TOMAK_EDGE_IDLE;
}

void era_tomak_sync_transition_cancel(era_tomak_sync_transition_t *edge) {
    /* Keep the consumed identity: cancellation must not authorize replay of
     * the same transition. Recovery binds a freshly proven relation. */
    edge->phase = ERA_TOMAK_EDGE_FAILED;
}
