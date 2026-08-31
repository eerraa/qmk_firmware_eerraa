// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_host_peer_matrix_link.h"

#include <string.h>

#include "../system/era_matrix_engine.h"
#include "atomic_util.h"
#include "era_split_matrix_frame.h"

#if !defined(ERA_RP2040_MATRIX_ENABLE)
#    error "era_host_peer_matrix_link.c requires the ERA RP2040 matrix engine."
#endif

#if defined(ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE) && defined(MCU_RP)
/* The register struct only: pico-sdk's hardware/timer.h clashes with ChibiOS's
   TIMER macro (the PIO sampler carries the same note). Raw microseconds, no
   conversion -- the span is stamped from a key-event path, but the same rule
   that priced conversions off the scan path applies to anything that runs
   beside it. */
#    include "hardware/structs/timer.h"
#    define ERA_HOST_PEER_MATRIX_LINK_SPAN_ENABLE
#endif

typedef struct {
    uint32_t source_push_tx_count;
    uint32_t source_push_ack_count;
    uint32_t ack_status_tx_count;
#ifdef ERA_HOST_PEER_MATRIX_LINK_SPAN_ENABLE
    /* Armed by the first capture after an ACK, closed by that ACK. A second
       capture before the ACK leaves the arm alone deliberately: the question is
       how long the oldest unacknowledged key state waited, not how long the
       newest copy of it did. */
    bool     push_armed;
    uint32_t push_start_us;
    uint32_t push_span_count;
    uint32_t push_span_sum_us;
    uint32_t push_span_max_us;
    uint32_t push_span_bucket[ERA_HOST_PEER_MATRIX_LINK_SPAN_BUCKETS];
#endif
} era_host_peer_matrix_link_state_t;

static era_host_peer_matrix_link_state_t g_era_host_peer_matrix_link;

#ifdef ERA_HOST_PEER_MATRIX_LINK_SPAN_ENABLE
/* Upper edges in microseconds; the last bucket is everything above. Centred on
   the **source-push** exchange, measured 2026-08-16 at 402 us wall clock
   (`wire hp peer_tavg sp`), not on the 189 us heartbeat -- the first edges were
   written against the wrong one of the two. A clean round trip lands in the
   third bucket; one that waited behind a heartbeat lands around the sixth and
   one behind another source-push around the seventh, which is the separation
   this histogram exists to make. */
static const uint16_t era_host_peer_matrix_link_span_edges_us[ERA_HOST_PEER_MATRIX_LINK_SPAN_BUCKETS - 1U] = {320U, 384U, 448U, 512U, 640U, 896U, 1408U};

static void era_host_peer_matrix_link_span_close(void) {
    era_host_peer_matrix_link_state_t *state = &g_era_host_peer_matrix_link;
    if (!state->push_armed) {
        return;
    }
    state->push_armed = false;
    uint32_t span     = (uint32_t)(timer_hw->timerawl - state->push_start_us);
    state->push_span_count++;
    state->push_span_sum_us += span;
    if (span > state->push_span_max_us) {
        state->push_span_max_us = span;
    }
    uint8_t bucket = ERA_HOST_PEER_MATRIX_LINK_SPAN_BUCKETS - 1U;
    for (uint8_t index = 0; index < ERA_HOST_PEER_MATRIX_LINK_SPAN_BUCKETS - 1U; index++) {
        if (span < era_host_peer_matrix_link_span_edges_us[index]) {
            bucket = index;
            break;
        }
    }
    state->push_span_bucket[bucket]++;
}
#endif

bool era_host_peer_matrix_link_capture_source_push(uint8_t packed_rows[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES], uint8_t *matrix_seq) {
    if (packed_rows == NULL || matrix_seq == NULL) {
        return false;
    }

    matrix_row_t rows[MATRIX_ROWS_PER_HAND];
    uint8_t      seq = 0;
    if (!era_matrix_engine_copy_source_push_rows(rows, &seq) || !era_split_wire_pack_matrix(rows, packed_rows)) {
        return false;
    }

#ifdef ERA_HOST_PEER_MATRIX_LINK_SPAN_ENABLE
    if (!g_era_host_peer_matrix_link.push_armed) {
        g_era_host_peer_matrix_link.push_armed    = true;
        g_era_host_peer_matrix_link.push_start_us = timer_hw->timerawl;
    }
#endif
    *matrix_seq = seq;
    return true;
}

void era_host_peer_matrix_link_note_source_push_sent(void) {
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_host_peer_matrix_link.source_push_tx_count++;
    }
}

void era_host_peer_matrix_link_note_source_push_accepted(uint8_t matrix_seq) {
    era_matrix_engine_note_source_push_accepted(matrix_seq);
#ifdef ERA_HOST_PEER_MATRIX_LINK_SPAN_ENABLE
    era_host_peer_matrix_link_span_close();
#endif
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_host_peer_matrix_link.source_push_ack_count++;
    }
}

void era_host_peer_matrix_link_note_ack_status_sent(void) {
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_host_peer_matrix_link.ack_status_tx_count++;
    }
}

void era_host_peer_matrix_link_note_ack_status_sent_bulk(uint32_t count) {
    if (count == 0) {
        return;
    }
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_host_peer_matrix_link.ack_status_tx_count += count;
    }
}

bool era_host_peer_matrix_link_accept_source_push_packed(const uint8_t packed_rows[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES]) {
    matrix_row_t rows[MATRIX_ROWS_PER_HAND];
    if (packed_rows == NULL || !era_split_wire_unpack_matrix(packed_rows, rows)) {
        return false;
    }
    return era_matrix_engine_accept_peer_snapshot(rows);
}

void era_host_peer_matrix_link_get_diagnostics_snapshot(era_host_peer_matrix_link_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    era_matrix_engine_host_peer_diagnostics_t matrix;
    era_matrix_engine_get_host_peer_diagnostics(&matrix);

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->local_matrix_ready       = matrix.local_matrix_ready;
    snapshot->local_source_push_forced = matrix.local_source_push_forced;
    snapshot->peer_cache_valid         = matrix.peer_cache_valid;
    snapshot->local_current_seq8       = matrix.local_current_seq8;
    snapshot->local_host_known_seq8    = matrix.local_host_known_seq8;
    snapshot->peer_matrix_seq8         = matrix.peer_matrix_seq8;
    snapshot->peer_cache_update_count  = matrix.peer_cache_update_count;
    snapshot->peer_cache_project_count = matrix.peer_cache_project_count;
    snapshot->peer_cache_flush_count   = matrix.peer_cache_flush_count;
    ATOMIC_BLOCK_RESTORESTATE {
        snapshot->source_push_tx_count  = g_era_host_peer_matrix_link.source_push_tx_count;
        snapshot->source_push_ack_count = g_era_host_peer_matrix_link.source_push_ack_count;
        snapshot->ack_status_tx_count   = g_era_host_peer_matrix_link.ack_status_tx_count;
#ifdef ERA_HOST_PEER_MATRIX_LINK_SPAN_ENABLE
        snapshot->push_span_count  = g_era_host_peer_matrix_link.push_span_count;
        snapshot->push_span_sum_us = g_era_host_peer_matrix_link.push_span_sum_us;
        snapshot->push_span_max_us = g_era_host_peer_matrix_link.push_span_max_us;
        for (uint8_t bucket = 0; bucket < ERA_HOST_PEER_MATRIX_LINK_SPAN_BUCKETS; bucket++) {
            snapshot->push_span_bucket[bucket] = g_era_host_peer_matrix_link.push_span_bucket[bucket];
        }
#endif
    }
}
