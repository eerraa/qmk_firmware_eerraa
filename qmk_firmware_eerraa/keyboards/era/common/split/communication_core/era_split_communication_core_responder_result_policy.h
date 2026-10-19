// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>

#include "era_split_communication_core_responder.h"

/* A successful section-bearing HEARTBEAT result exists only to let Core0
 * commit the responder sent-state shadow after Core1 has already put that
 * response on the wire. While Core0 is unavailable (notably during a
 * synchronous flash Apply), the same immutable responder snapshot can answer
 * many heartbeats before that first result drains. One pending result is enough:
 * each duplicate would commit the same response plan and the same section mask.
 *
 * No other result kind is coalescible. SESSION and runtime/source pushes carry
 * peer input that Core0 must process per arrival, and a failed or unsent result
 * cannot stand in for a successful sent-shadow commit. Exact snapshot identity
 * and exact wire section mask make the rule fail closed across relation/snapshot
 * changes and any future response-plan reshaping. */
static inline bool era_split_communication_core_responder_heartbeat_result_covers_snapshot(
    const era_split_communication_core_responder_snapshot_t *snapshot,
    const era_split_communication_core_responder_result_t *result) {
    return snapshot != NULL && result != NULL && snapshot->response_section_mask != 0 &&
           result->kind == ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_HEARTBEAT &&
           result->response_sent != 0 && result->result == ERA_SPLIT_TRANSACTION_RESULT_OK &&
           result->owner_epoch == snapshot->owner_epoch &&
           result->relation_generation == snapshot->relation_generation &&
           result->snapshot_generation == snapshot->snapshot_generation &&
           result->response_section_mask == snapshot->response_section_mask;
}
