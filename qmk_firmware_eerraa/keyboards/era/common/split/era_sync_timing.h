// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
void era_sync_timing_write_begin(uint32_t bytes);
void era_sync_timing_write_end(uint32_t elapsed_ms);
void era_sync_timing_commit(uint32_t bytes, bool remote);
void era_sync_timing_apply_end(uint32_t elapsed_ms);
void era_sync_timing_pending(bool pending);
/* First abort: packed state/operation/status/path, emitted only after F24.
 * Paths: 1 peer reset, 2 host close, 3 active host identity replacement,
 * 4 peer phase expiry, 5 host phase expiry. */
void era_sync_timing_abort(uint8_t state, uint8_t operation, uint8_t status, uint8_t path);

#ifdef ERA_TOMAK_SYNC_STATE_DIAGNOSTICS
typedef struct {
    uint32_t generation, deadline_ms;
    uint8_t remaining_mask, preparing_mask, phase, outcome;
    uint8_t settle_mask, local_mask, news_pending_mask;
} era_sync_timing_operation_t;

typedef struct {
    uint32_t token, operation_generation, deadline_ms, idle_deadline_ms;
    uint8_t mask, live;
} era_sync_timing_contribution_t;

typedef struct {
    uint32_t at_ms, generation, current_generation;
    uint32_t episode_generation, snapshot_generation;
    uint8_t phase, mask, eligible_mask, retired_mask;
    uint8_t episode_mask, snapshot_mask;
} era_sync_timing_proof_t;

typedef struct {
    uint32_t at_ms, operation_generation, crc32;
    uint8_t domain, ok;
} era_sync_timing_metadata_t;

typedef struct {
    uint32_t at_ms;
    uint16_t owner_epoch, relation_generation, policy_generation;
    uint8_t valid, mode, owner_ready, local_initiator;
    uint8_t input_recovery_pending, status_revalidation_due;
} era_sync_timing_context_t;

/* First ceiling only, latched before terminal cleanup. No payload bytes. */
typedef struct {
    era_sync_timing_operation_t operations[2];
    era_sync_timing_contribution_t contributions[4];
    era_sync_timing_proof_t proof;
    era_sync_timing_metadata_t metadata;
    era_sync_timing_context_t context;
    uint32_t at_ms, macro_token, episode_generation, snapshot_generation;
    uint32_t match_count, transfer_count, complete_count, abort_count, stale_count, retry_count;
    uint16_t owner_epoch, relation_generation, policy_generation;
    uint16_t transaction_generation, request_generation, responder_snapshot_generation;
    /* 1 current ceiling, 2 successor ceiling, 3 upload ceiling, 4 upload idle. */
    uint8_t reason;
    uint8_t state, role, domain, pending_operation, status, flags;
    uint8_t dirty_mask, intent_mask, failed_mask, eligible_mask;
    uint8_t episode_mask, snapshot_mask;
    uint8_t active, retiring, result_due, macro_open;
    uint8_t arbitration_flags, probe_mask, push_mask, conflict_mask;
    uint8_t news, peer_news;
} era_sync_timing_storage_snapshot_t;

const era_sync_timing_storage_snapshot_t *era_host_peer_storage_timing_snapshot(void);
#endif
