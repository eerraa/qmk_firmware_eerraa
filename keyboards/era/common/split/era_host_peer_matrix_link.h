// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_split_wire_protocol.h"

/* The PEER's key-path span, and why it is measured here rather than inferred.
 *
 * In HOST-PEER the PEER's keys reach the computer over the wire, so core1 is on
 * the key path for half the keyboard -- the one relation where it is. Every
 * performance figure this tree has ever recorded is a core0 scan rate, which
 * says nothing about that half. The span below is the part of the PEER's
 * key-to-HID chain that core0's scan rate cannot see: from core0 publishing a
 * source-push to core0 applying its ACK, which contains core1's pickup, the
 * wire exchange, and the HOST responder's turnaround.
 *
 * It is per key event, not per pass -- a few hundred a second against forty
 * thousand passes -- so two counter reads cost nothing measurable and the
 * instrument cannot distort what it measures. That is why it takes no selector
 * of its own and rides the wire-diagnostics images.
 *
 * The histogram is what decides anything: a mean hides the case that matters,
 * which is the exchange that arrived while core1 was busy. Bucket edges are in
 * microseconds and centred on the measured 402 us source-push exchange -- the
 * span this covers is a round trip, and only its first half is on the key's
 * path, so what the buckets separate is not the latency itself but whether the
 * request waited for core1 before it started. */
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
#    define ERA_HOST_PEER_MATRIX_LINK_SPAN_BUCKETS 8U
#endif

typedef struct {
    uint8_t  local_matrix_ready;
    uint8_t  local_source_push_forced;
    uint8_t  peer_cache_valid;
    uint8_t  local_current_seq8;
    uint8_t  local_host_known_seq8;
    uint8_t  peer_matrix_seq8;
    uint32_t source_push_tx_count;
    uint32_t source_push_ack_count;
    uint32_t ack_status_tx_count;
    uint32_t peer_cache_update_count;
    uint32_t peer_cache_project_count;
    uint32_t peer_cache_flush_count;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    uint32_t push_span_count;
    uint32_t push_span_sum_us;
    uint32_t push_span_max_us;
    uint32_t push_span_bucket[ERA_HOST_PEER_MATRIX_LINK_SPAN_BUCKETS];
#endif
} era_host_peer_matrix_link_diagnostics_t;

bool era_host_peer_matrix_link_capture_source_push(uint8_t packed_rows[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES], uint8_t *matrix_seq);
void era_host_peer_matrix_link_note_source_push_sent(void);
void era_host_peer_matrix_link_note_source_push_accepted(uint8_t matrix_seq);
void era_host_peer_matrix_link_note_ack_status_sent(void);
/* The bulk form, for ACKs core1 answered without publishing a result. Same
   reason as era_split_responder_projection_note_quiet(). */
void era_host_peer_matrix_link_note_ack_status_sent_bulk(uint32_t count);
bool era_host_peer_matrix_link_accept_source_push_packed(const uint8_t packed_rows[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES]);
void era_host_peer_matrix_link_get_diagnostics_snapshot(era_host_peer_matrix_link_diagnostics_t *snapshot);
