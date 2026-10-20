// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ERA_TOMAK_EDGE_IDLE,
    ERA_TOMAK_EDGE_INSTALLED,
    ERA_TOMAK_EDGE_ARMED,
    ERA_TOMAK_EDGE_OUTPUT,
    ERA_TOMAK_EDGE_FAILED,
} era_tomak_edge_phase_t;

/* One transition per pair. Sequence is retained after completion so a duplicate
 * cannot replay an edge. The caller fences every message with the relation. */
typedef struct {
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint16_t sequence;
    uint16_t applied_sequence;
    uint32_t wire_ms;
    uint32_t local_ms;
    era_tomak_edge_phase_t phase;
    bool target;
    bool displayed;
} era_tomak_sync_transition_t;

void era_tomak_sync_transition_bind(era_tomak_sync_transition_t *edge, uint16_t owner, uint16_t relation);
bool era_tomak_sync_transition_install(era_tomak_sync_transition_t *edge, uint16_t owner, uint16_t relation,
                                      uint16_t sequence, bool target, uint32_t wire_ms,
                                      uint32_t shared_now, uint32_t local_now, uint16_t min_lead_ms, uint16_t max_lead_ms);
bool era_tomak_sync_transition_arm(era_tomak_sync_transition_t *edge, uint16_t owner, uint16_t relation,
                                  uint16_t sequence, uint32_t wire_ms);
/* Called only after the renderer has prepared/drained its previous frame.
 * True grants one launch, never one launch on every pass after the deadline. */
bool era_tomak_sync_transition_launch(era_tomak_sync_transition_t *edge, uint32_t now_ms, uint16_t max_late_ms);
void era_tomak_sync_transition_output(era_tomak_sync_transition_t *edge, bool succeeded);
void era_tomak_sync_transition_cancel(era_tomak_sync_transition_t *edge);
