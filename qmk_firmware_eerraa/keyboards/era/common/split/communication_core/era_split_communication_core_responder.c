// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_communication_core_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "../era_host_peer_transaction.h"
#include "../era_split_transaction_engine.h"
#include "era_split_communication_core_responder_internal.h"

enum {
    ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_SNAPSHOT_SOURCE_NONE  = 0,
    ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_SNAPSHOT_SOURCE_CORE0 = 2,
};

_Static_assert(ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_GENERAL_RESULT_SLOTS >= 2U, "ERA responder general result ring is too small.");

era_split_communication_core_responder_snapshot_t g_era_split_communication_core_responder_snapshot;
volatile uint32_t g_era_split_communication_core_responder_snapshot_publish_seq __attribute__((aligned(4)));
volatile uint32_t g_era_split_communication_core_responder_snapshot_claim_generation __attribute__((aligned(4)));
era_split_communication_core_responder_result_t g_era_split_communication_core_responder_general_results[ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_GENERAL_RESULT_SLOTS];
volatile uint32_t g_era_split_communication_core_responder_general_read __attribute__((aligned(4)));
volatile uint32_t g_era_split_communication_core_responder_general_write __attribute__((aligned(4)));
static bool g_era_split_communication_core_responder_refresh_due;

void era_split_communication_core_refresh_responder_snapshot(void) {
    if (get_core_num() == 0) g_era_split_communication_core_responder_refresh_due = true;
}

static bool era_split_communication_core_responder_result_ready_internal(void) {
    return g_era_split_communication_core_responder_general_read != g_era_split_communication_core_responder_general_write;
}
static void era_split_communication_core_responder_sanitize_snapshot(era_split_communication_core_responder_snapshot_t *snapshot) {
    snapshot->owner_gate_ready           = snapshot->owner_gate_ready ? 1 : 0;
    snapshot->session_allowed            = snapshot->session_allowed ? 1 : 0;
    snapshot->host_matrix_admitted       = snapshot->host_matrix_admitted ? 1 : 0;
    snapshot->heartbeat_allowed          = snapshot->heartbeat_allowed ? 1 : 0;
    snapshot->source_push_slot_available = snapshot->source_push_slot_available ? 1 : 0;
    snapshot->runtime_allowed            = snapshot->runtime_allowed ? 1 : 0;
    snapshot->lock_state_bits &= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_VALUE_MASK;
    snapshot->response_section_mask &= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_MASK;
    /* The send-clip for the response direction, and the only one: every
       responder section reaches the wire through this publish, so a planner
       that advertises a section its relation does not carry has it removed
       here rather than on the wire. */
    snapshot->eligible_rsp_sections &= ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_MASK;
    snapshot->eligible_push_sections &= ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MASK;
    snapshot->response_section_mask &= snapshot->eligible_rsp_sections;
    if (!snapshot->visual_baseline_valid || snapshot->visual_reason > ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_MAX) {
        snapshot->visual_baseline_valid = 0;
        snapshot->visual_reason         = 0;
        memset(snapshot->visual_baseline, 0, sizeof(snapshot->visual_baseline));
        snapshot->response_section_mask &= (uint8_t)~ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC;
    }
    if (!snapshot->rgb_state_valid) {
        snapshot->rgb_state = (era_host_peer_rgb_state_t){0};
        snapshot->response_section_mask &= (uint8_t)~ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE;
    }
    if ((snapshot->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC) != 0 &&
        (snapshot->response_section_mask & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE) != 0) {
        snapshot->rgb_state_valid = 0;
        snapshot->rgb_state       = (era_host_peer_rgb_state_t){0};
        snapshot->response_section_mask &= (uint8_t)~ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_RGB_STATE;
    }
}

bool era_split_communication_core_publish_responder_snapshot(const era_split_communication_core_responder_snapshot_t *snapshot) {
    if (snapshot == NULL || get_core_num() != 0) {
        return false;
    }

    era_split_communication_core_init();
    era_split_communication_core_responder_snapshot_t published = *snapshot;
    published.source_push_slot_available = 0; /* Input packet capacity is Core1-owned. */
    era_split_communication_core_responder_sanitize_snapshot(&published);
    /* The generation is excluded from the comparison, and the exclusion is done
       in place: `published` is this call's own copy and its generation is
       overwritten unconditionally below, so borrowing the published record's
       value here costs nothing and the comparison needs no second copy. The
       comparator this replaced took a full 84-byte copy of *each* side to null
       one field, on core0's publish path. Excluding the field at all is what
       makes "on change" true rather than intended -- a value that moves on every
       publish makes every publish differ, which is the failure this path
       measured at 911 publishes per second. */
    published.snapshot_generation = g_era_split_communication_core_responder_snapshot.snapshot_generation;
    if (!g_era_split_communication_core_responder_refresh_due &&
        g_era_split_communication_core.responder_snapshot_valid &&
        memcmp(&published, &g_era_split_communication_core_responder_snapshot, sizeof(published)) == 0) {
        return true;
    }

    uint16_t generation = (uint16_t)(g_era_split_communication_core.responder_snapshot_generation + 1U);
    if (generation == 0) {
        generation = 1;
    }
    published.snapshot_generation = generation;

    uint32_t odd_seq = g_era_split_communication_core_responder_snapshot_publish_seq + 1U;
    if ((odd_seq & 1U) == 0) {
        odd_seq++;
    }
    g_era_split_communication_core_responder_snapshot_publish_seq = odd_seq;
    __DMB();
    if (g_era_split_communication_core_responder_snapshot_claim_generation != 0 ||
        era_split_communication_core_responder_result_ready_internal()) {
        g_era_split_communication_core_responder_snapshot_publish_seq = odd_seq + 1U;
        __DMB();
        return false;
    }
    g_era_split_communication_core_responder_snapshot = published;
    __DMB();
    g_era_split_communication_core_responder_snapshot_publish_seq = odd_seq + 1U;
    __DMB();

    g_era_split_communication_core.responder_snapshot_valid       = 1;
    g_era_split_communication_core_responder_refresh_due = false;
    g_era_split_communication_core.responder_snapshot_source      = ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_SNAPSHOT_SOURCE_CORE0;
    g_era_split_communication_core.responder_owner_gate_ready     = published.owner_gate_ready;
    g_era_split_communication_core.responder_owner_epoch          = published.owner_epoch;
    g_era_split_communication_core.responder_relation_generation  = published.relation_generation;
    g_era_split_communication_core.responder_snapshot_generation  = generation;
    g_era_split_communication_core.responder_snapshot_publish_count++;
    if (published.owner_gate_ready) {
        g_era_split_communication_core.responder_owner_gate_ready_count++;
    } else {
        g_era_split_communication_core.responder_owner_gate_block_count++;
    }
    g_era_split_communication_core.responder_last_request_kind_flags =
        (published.heartbeat_allowed ? 0x01U : 0U) |
        (published.host_matrix_admitted ? 0x02U : 0U) |
        (published.runtime_allowed ? 0x04U : 0U);
    g_era_split_communication_core.responder_last_response_section_mask = published.response_section_mask;
    g_era_split_communication_core.responder_last_visual_reason         = published.visual_reason;
    return true;
}

bool era_split_communication_core_responder_result_ready(void) {
    __DMB();
    return era_split_communication_core_responder_result_ready_internal();
}

static bool era_split_communication_core_responder_result_pop(era_split_communication_core_responder_result_t *result) {
    uint32_t read = g_era_split_communication_core_responder_general_read;
    __DMB();
    if (read == g_era_split_communication_core_responder_general_write) return false;
    *result = g_era_split_communication_core_responder_general_results[read];
    __DMB();
    g_era_split_communication_core_responder_general_read = era_split_communication_core_responder_ring_next(read, ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_GENERAL_RESULT_SLOTS);
    g_era_split_communication_core.responder_drain_count++;
    g_era_split_communication_core.responder_last_result_generation = result->snapshot_generation;
    __DMB(); return true;
}

bool era_split_communication_core_drain_responder_result(era_split_communication_core_responder_result_t *result) {
    if (result == NULL || get_core_num() != 0) {
        return false;
    }
    return era_split_communication_core_responder_result_pop(result);
}

bool era_split_communication_core_responder_result_matches_current(const era_split_communication_core_responder_result_t *result) {
    if (result == NULL || get_core_num() != 0) {
        return false;
    }

    __DMB();
    bool matches = g_era_split_communication_core.responder_snapshot_valid &&
                   result->owner_epoch == g_era_split_communication_core.responder_owner_epoch &&
                   result->relation_generation == g_era_split_communication_core.responder_relation_generation &&
                   result->snapshot_generation == g_era_split_communication_core.responder_snapshot_generation;
    if (!matches) {
        g_era_split_communication_core.responder_accept_stale_count++;
    }
    return matches;
}

/* The responder's own clear retired with the scaffold that called it (D3
   sweep). Its live sibling era_split_communication_core_clear_standing() is
   still called on every relation rotation, and the asymmetry is the design
   rather than a missing clear: the standing plan must be cleared because
   clearing it is what makes core1 observe the generation change at all, while
   the responder rotates by republish-plus-generation-fence -- core0 republishes
   the snapshot on every settle and every consumer is fenced by
   era_split_communication_core_responder_result_matches_current(). */
