// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "matrix.h"

/* True when this scan's debounced local rows differ from the previous scan's.
 *
 * This is the whole of the engine a non-split build sees. Everything below is
 * the split relation's view of the same engine — the peer rows, the source-push
 * lane, the projection, and their diagnostics — and none of it has a non-split
 * caller. The guard is the boundary rather than a convenience: the declarations
 * used to sit outside it while their definitions sat inside, so the non-split
 * path had never been compiled and did not build at all when first tried. */
bool era_matrix_engine_local_changed(void);

#ifdef SPLIT_KEYBOARD

typedef enum {
    ERA_MATRIX_ENGINE_PEER_PROJECTION_UNCHANGED = 0,
    ERA_MATRIX_ENGINE_PEER_PROJECTION_CLEARED,
    ERA_MATRIX_ENGINE_PEER_PROJECTION_APPLIED,
} era_matrix_engine_peer_projection_result_t;

typedef enum {
    ERA_MATRIX_ENGINE_PEER_MATRIX_UNCHANGED = 0,
    ERA_MATRIX_ENGINE_PEER_MATRIX_INVALID,
    ERA_MATRIX_ENGINE_PEER_MATRIX_COPIED,
} era_matrix_engine_peer_matrix_copy_result_t;

typedef struct {
    uint8_t  local_matrix_ready;
    uint8_t  local_source_push_forced;
    uint8_t  peer_cache_valid;
    uint8_t  local_current_seq8;
    uint8_t  local_host_known_seq8;
    uint8_t  peer_matrix_seq8;
    uint32_t peer_cache_update_count;
    uint32_t peer_cache_project_count;
    uint32_t peer_cache_flush_count;
} era_matrix_engine_host_peer_diagnostics_t;

/* This is the split relation's view of the engine, and only that: a
   declaration earns a place here by having a caller outside
   era_rp2040_matrix_core.c. The engine's own peer-row bookkeeping —
   apply/clear peer rows, the peer-cache dirty test, and the seq-gated peer
   matrix copy — is declared in that unit instead. */
bool                                       era_matrix_engine_copy_local_rows(matrix_row_t rows[MATRIX_ROWS_PER_HAND]);
bool                                       era_matrix_engine_publish_local_snapshot_if_needed(bool *first_ready);
bool                                       era_matrix_engine_local_matrix_ready(void);
bool                                       era_matrix_engine_source_push_due(void);
bool                                       era_matrix_engine_copy_source_push_rows(matrix_row_t rows[MATRIX_ROWS_PER_HAND], uint8_t *matrix_seq);
void                                       era_matrix_engine_note_source_push_accepted(uint8_t matrix_seq);
bool                                       era_matrix_engine_accept_peer_snapshot(const matrix_row_t rows[MATRIX_ROWS_PER_HAND]);
era_matrix_engine_peer_projection_result_t era_matrix_engine_sync_peer_projection(bool host_mode);
bool                                       era_matrix_engine_peer_projection_scan_idle(bool host_mode);
void                                       era_matrix_engine_flush_host_peer_relation(void);
void                                       era_matrix_engine_get_host_peer_diagnostics(era_matrix_engine_host_peer_diagnostics_t *snapshot);

#endif /* SPLIT_KEYBOARD */
