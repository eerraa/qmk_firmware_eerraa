// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_transaction_engine.h"

#include <string.h>

#include "era_split_transaction_backend.h"
#include "era_split_transaction_io.h"
#include "era_split_wire_frame.h"

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
#    include "era_split_wire_payload.h"
#    include "timer.h"
#    if defined(MCU_RP)
#        include "hardware/structs/timer.h"
#    endif
#endif

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
#    define ERA_SPLIT_TRANSACTION_TIMING_PROXY_PAYLOAD_LEN 10

typedef struct {
    bool     valid;
    bool     total_pending;
    uint8_t  route_kind;
    uint8_t  route_reason;
    uint8_t  request_kind;
    uint8_t  response_kind;
    uint8_t  result;
    uint8_t  request_payload_len;
    uint8_t  response_payload_len;
    uint8_t  response_section_byte;
    uint8_t  payload_proxy;
    uint32_t route_generation;
    uint32_t tx_us;
    uint32_t wait_rx_us;
    uint32_t rx_decode_us;
    uint32_t total_us;
} era_split_transaction_timing_sample_t;
#endif

typedef struct {
    bool     initialized;
    uint8_t  tx_seq;
    uint8_t  ack_seq;
    uint32_t compact_tx_count;
    uint32_t compact_rx_count;
    uint32_t compact_miss_count;
    uint32_t compact_bad_count;
    uint32_t compact_fail_count;
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    bool                                  timing_route_active;
    uint8_t                               timing_route_kind;
    uint8_t                               timing_route_reason;
    uint32_t                              timing_route_generation;
    uint32_t                              timing_route_start_us;
    uint32_t                              timing_sample_count;
    uint32_t                              timing_timeout_count;
    era_split_transaction_timing_sample_t timing_latest;
    era_split_transaction_timing_bucket_diagnostics_t timing_buckets[ERA_SPLIT_TRANSACTION_TIMING_BUCKET_COUNT];
#endif
} era_split_transaction_engine_state_t;

static era_split_transaction_engine_state_t g_era_split_transaction_engine;

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
enum {
    ERA_SPLIT_TRANSACTION_ENGINE_DIAGNOSTICS_READ_RETRIES = 4,
};

static era_split_transaction_engine_diagnostics_t g_era_split_transaction_engine_diagnostics_snapshot;
static volatile uint32_t g_era_split_transaction_engine_diagnostics_publish_seq __attribute__((aligned(4)));
/* Core0-only cache of the last snapshot that passed the odd/even proof. */
static era_split_transaction_engine_diagnostics_t g_era_split_transaction_engine_diagnostics_last_stable;
static uint32_t                                   g_era_split_transaction_engine_diagnostics_fallback_count;
static uint8_t                                    g_era_split_transaction_engine_diagnostics_last_stable_valid;
static uint8_t                                    g_era_split_transaction_engine_diagnostics_snapshot_fresh;
#endif

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
static uint32_t era_split_transaction_engine_timing_read_us(void) {
#    if defined(MCU_RP)
    return timer_hw->timerawl;
#    else
    return timer_read32() * 1000U;
#    endif
}

static uint32_t era_split_transaction_engine_timing_elapsed_us(uint32_t start_us) {
    return (uint32_t)(era_split_transaction_engine_timing_read_us() - start_us);
}

static uint8_t era_split_transaction_engine_timing_classify_payload_kind(const uint8_t *payload, uint8_t payload_len) {
    era_split_wire_payload_kind_t kind = ERA_SPLIT_WIRE_PAYLOAD_INVALID;
    if (!era_split_wire_classify_payload(payload, payload_len, ERA_SPLIT_WIRE_FRAME_LANE_COMPACT, &kind)) {
        return (uint8_t)ERA_SPLIT_WIRE_PAYLOAD_INVALID;
    }
    return (uint8_t)kind;
}

static bool era_split_transaction_engine_timing_is_payload_proxy(const era_split_transaction_timing_sample_t *sample) {
    if (sample == NULL || sample->response_kind != ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER_HOST_SOURCE_RSP) {
        return false;
    }
    return (sample->response_section_byte & ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC) != 0 &&
           sample->response_payload_len >= ERA_SPLIT_TRANSACTION_TIMING_PROXY_PAYLOAD_LEN;
}

static bool era_split_transaction_engine_timing_request_is_heartbeat(const era_split_transaction_timing_sample_t *sample) {
    return sample != NULL && sample->request_kind == ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK && sample->request_payload_len == 1;
}

static void era_split_transaction_engine_timing_add_bucket(era_split_transaction_timing_bucket_t bucket, const era_split_transaction_timing_sample_t *sample) {
    if (sample == NULL || bucket >= ERA_SPLIT_TRANSACTION_TIMING_BUCKET_COUNT) {
        return;
    }

    era_split_transaction_timing_bucket_diagnostics_t *stats = &g_era_split_transaction_engine.timing_buckets[bucket];
    stats->sample_count++;
    if (sample->result == ERA_SPLIT_TRANSACTION_RESULT_MISS) {
        stats->timeout_count++;
    }
    stats->tx_us_total += sample->tx_us;
    stats->wait_rx_us_total += sample->wait_rx_us;
    stats->rx_decode_us_total += sample->rx_decode_us;
    stats->total_us_total += sample->total_us;
    stats->total_us_last = sample->total_us;
}

static void era_split_transaction_engine_timing_record_sample(const era_split_transaction_timing_sample_t *sample) {
    if (sample == NULL || !sample->valid) {
        return;
    }

    if (sample->response_kind == ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER_HOST_SOURCE_RSP) {
        era_split_transaction_engine_timing_add_bucket(ERA_SPLIT_TRANSACTION_TIMING_BUCKET_HOST_SOURCE_RSP, sample);
        if (sample->payload_proxy) {
            era_split_transaction_engine_timing_add_bucket(ERA_SPLIT_TRANSACTION_TIMING_BUCKET_PROXY, sample);
        }
        return;
    }

    if (era_split_transaction_engine_timing_request_is_heartbeat(sample)) {
        era_split_transaction_engine_timing_add_bucket(ERA_SPLIT_TRANSACTION_TIMING_BUCKET_HEARTBEAT_ACK, sample);
        return;
    }

    if (sample->request_kind == ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER) {
        era_split_transaction_engine_timing_add_bucket(ERA_SPLIT_TRANSACTION_TIMING_BUCKET_SOURCE_PUSH_ACK, sample);
    }
}

static void era_split_transaction_engine_timing_begin_sample(era_split_transaction_timing_sample_t *sample, const uint8_t *payload, uint8_t payload_len) {
    if (sample == NULL) {
        return;
    }
    memset(sample, 0, sizeof(*sample));
    sample->valid               = true;
    sample->route_kind          = g_era_split_transaction_engine.timing_route_kind;
    sample->route_reason        = g_era_split_transaction_engine.timing_route_reason;
    sample->request_kind        = era_split_transaction_engine_timing_classify_payload_kind(payload, payload_len);
    sample->request_payload_len = payload_len;
    sample->route_generation    = g_era_split_transaction_engine.timing_route_generation;
}

static void era_split_transaction_engine_timing_note_response(era_split_transaction_timing_sample_t *sample, const era_split_wire_frame_t *response) {
    if (sample == NULL || response == NULL) {
        return;
    }
    sample->response_kind        = (uint8_t)response->kind;
    sample->response_payload_len = (uint8_t)response->payload_len;
    sample->response_section_byte = response->payload_len > 2 ? response->payload[2] : 0;
}

static void era_split_transaction_engine_timing_finish_sample(era_split_transaction_timing_sample_t *sample, era_split_transaction_engine_result_t result) {
    if (sample == NULL || !sample->valid) {
        return;
    }
    sample->result        = (uint8_t)result;
    sample->payload_proxy = era_split_transaction_engine_timing_is_payload_proxy(sample) ? 1 : 0;
    g_era_split_transaction_engine.timing_sample_count++;
    if (result == ERA_SPLIT_TRANSACTION_RESULT_MISS) {
        g_era_split_transaction_engine.timing_timeout_count++;
    }
    sample->total_pending                        = true;
    g_era_split_transaction_engine.timing_latest = *sample;
}
#endif

static void era_split_transaction_engine_reset_seq_state(void) {
    g_era_split_transaction_engine.tx_seq  = 0;
    g_era_split_transaction_engine.ack_seq = 0;
}

static void era_split_transaction_engine_init(void) {
    if (g_era_split_transaction_engine.initialized) {
        return;
    }
    memset(&g_era_split_transaction_engine, 0, sizeof(g_era_split_transaction_engine));
    g_era_split_transaction_engine.initialized = true;
    era_split_transaction_backend_init();
}

void era_split_transaction_engine_init_initiator_driver(void) {
    era_split_transaction_engine_init();
    if (era_split_transaction_backend_init_initiator()) {
        era_split_transaction_engine_reset_seq_state();
    }
}

void era_split_transaction_engine_init_responder_driver(void) {
    era_split_transaction_engine_init();
    if (era_split_transaction_backend_init_responder()) {
        era_split_transaction_engine_reset_seq_state();
    }
}

void era_split_transaction_engine_release_driver(void) {
    era_split_transaction_engine_init();
    era_split_transaction_backend_release();
    era_split_transaction_engine_reset_seq_state();
}

void era_split_transaction_engine_reset_link_state(void) {
    era_split_transaction_engine_init();
    era_split_transaction_engine_reset_seq_state();
    era_split_transaction_backend_reset_link_state();
}

bool era_split_transaction_engine_prepare_control(bool ext, uint8_t *control, uint8_t *tx_seq) {
    if (control == NULL || tx_seq == NULL) {
        return false;
    }

    uint8_t next_tx = era_split_wire_next_seq(g_era_split_transaction_engine.tx_seq);
    *control        = era_split_wire_control_byte(next_tx, g_era_split_transaction_engine.ack_seq, ext);
    *tx_seq         = next_tx;
    return true;
}

void era_split_transaction_engine_commit_prepared_tx(uint8_t tx_seq) {
    g_era_split_transaction_engine.tx_seq = tx_seq & ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK;
}

void era_split_transaction_engine_commit_received_frame(const era_split_wire_frame_t *frame) {
    if (frame == NULL) {
        return;
    }
    g_era_split_transaction_engine.ack_seq = frame->tx_seq & ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK;
}

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
void era_split_transaction_engine_timing_begin_route(uint8_t route_kind, uint8_t route_reason) {
    g_era_split_transaction_engine.timing_route_kind       = route_kind;
    g_era_split_transaction_engine.timing_route_reason     = route_reason;
    g_era_split_transaction_engine.timing_route_generation++;
    g_era_split_transaction_engine.timing_route_start_us = era_split_transaction_engine_timing_read_us();
    g_era_split_transaction_engine.timing_route_active   = true;
}

void era_split_transaction_engine_timing_end_route(void) {
    if (!g_era_split_transaction_engine.timing_route_active) {
        return;
    }
    if (g_era_split_transaction_engine.timing_latest.valid &&
        g_era_split_transaction_engine.timing_latest.total_pending &&
        g_era_split_transaction_engine.timing_latest.route_generation == g_era_split_transaction_engine.timing_route_generation) {
        g_era_split_transaction_engine.timing_latest.total_us      = era_split_transaction_engine_timing_elapsed_us(g_era_split_transaction_engine.timing_route_start_us);
        g_era_split_transaction_engine.timing_latest.total_pending = false;
        era_split_transaction_engine_timing_record_sample(&g_era_split_transaction_engine.timing_latest);
    }
    g_era_split_transaction_engine.timing_route_active = false;
}
#endif

static bool era_split_transaction_engine_response_kind_matches(era_split_wire_payload_kind_t expected_kind, era_split_wire_payload_kind_t alternate_expected_kind, era_split_wire_payload_kind_t response_kind) {
    return response_kind == expected_kind || (alternate_expected_kind != ERA_SPLIT_WIRE_PAYLOAD_INVALID && response_kind == alternate_expected_kind);
}

static void era_split_transaction_engine_note_owned_result(era_split_transaction_engine_result_t result, era_split_transaction_failure_t failure) {
    switch (result) {
        case ERA_SPLIT_TRANSACTION_RESULT_MISS:
            g_era_split_transaction_engine.compact_miss_count++;
            if (failure == ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT) {
                era_split_transaction_backend_clear();
            }
            break;
        case ERA_SPLIT_TRANSACTION_RESULT_BAD:
            g_era_split_transaction_engine.compact_bad_count++;
            if (failure == ERA_SPLIT_TRANSACTION_FAILURE_DECODE || failure == ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_CONTRACT) {
                era_split_transaction_backend_clear();
            }
            break;
        case ERA_SPLIT_TRANSACTION_RESULT_FAIL:
            g_era_split_transaction_engine.compact_fail_count++;
            if (failure == ERA_SPLIT_TRANSACTION_FAILURE_IO || failure == ERA_SPLIT_TRANSACTION_FAILURE_PIO_ERROR || failure == ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME) {
                era_split_transaction_backend_clear();
            }
            break;
        default:
            break;
    }
}

era_split_transaction_engine_result_t era_split_transaction_engine_receive_idle_owned(era_split_wire_direction_t expected_direction,
                                                                                       uint16_t first_byte_timeout_ms,
                                                                                       uint16_t frame_timeout_ms,
                                                                                       uint16_t owner_epoch,
                                                                                       uint8_t *wire,
                                                                                       uint16_t wire_capacity,
                                                                                       era_split_wire_frame_t *frame,
                                                                                       era_split_transaction_failure_t *failure) {
    era_split_transaction_engine_result_t result = era_split_transaction_io_receive_idle_owned(expected_direction,
                                                                                                 first_byte_timeout_ms,
                                                                                                 frame_timeout_ms,
                                                                                                 owner_epoch,
                                                                                                 wire,
                                                                                                 wire_capacity,
                                                                                                 frame,
                                                                                                 failure);
    if (result == ERA_SPLIT_TRANSACTION_RESULT_OK) {
        g_era_split_transaction_engine.compact_rx_count++;
    } else if (result != ERA_SPLIT_TRANSACTION_RESULT_NONE) {
        era_split_transaction_engine_note_owned_result(result, failure != NULL ? *failure : ERA_SPLIT_TRANSACTION_FAILURE_IO);
    }
    return result;
}

era_split_transaction_engine_result_t era_split_transaction_engine_send_compact_response_owned(era_split_wire_direction_t response_direction,
                                                                                                const uint8_t *payload,
                                                                                                uint8_t payload_len,
                                                                                                uint8_t tx_seq,
                                                                                                uint16_t owner_epoch,
                                                                                                era_split_transaction_failure_t *failure) {
    era_split_transaction_engine_result_t result = era_split_transaction_io_send_compact_owned(response_direction,
                                                                                                 payload,
                                                                                                 payload_len,
                                                                                                 owner_epoch,
                                                                                                 failure
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
                                                                                                 ,
                                                                                                 NULL
#endif
    );
    if (result != ERA_SPLIT_TRANSACTION_RESULT_OK) {
        era_split_transaction_engine_note_owned_result(result, failure != NULL ? *failure : ERA_SPLIT_TRANSACTION_FAILURE_IO);
        return result;
    }
    era_split_transaction_engine_commit_prepared_tx(tx_seq);
    g_era_split_transaction_engine.compact_tx_count++;
    return result;
}

era_split_transaction_engine_result_t era_split_transaction_engine_transact_compact_owned(era_split_wire_direction_t request_direction,
                                                                                          const uint8_t *payload,
                                                                                          uint8_t payload_len,
                                                                                          uint8_t tx_seq,
                                                                                          era_split_wire_payload_kind_t expected_kind,
                                                                                          era_split_wire_payload_kind_t alternate_expected_kind,
                                                                                          uint16_t response_window_ms,
                                                                                          uint16_t owner_epoch,
                                                                                          era_split_wire_frame_t *response,
                                                                                          bool *request_sent,
                                                                                          era_split_transaction_failure_t *failure) {
    if (request_sent != NULL) {
        *request_sent = false;
    }
    if (failure == NULL || response == NULL) {
        return ERA_SPLIT_TRANSACTION_RESULT_BAD;
    }
    *failure = ERA_SPLIT_TRANSACTION_FAILURE_NONE;

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transaction_timing_sample_t       timing_sample;
    era_split_transaction_backend_send_timing_t send_timing = {0};
    era_split_transaction_engine_timing_begin_sample(&timing_sample, payload, payload_len);
    era_split_transaction_engine_result_t result = era_split_transaction_io_send_compact_owned(request_direction, payload, payload_len, owner_epoch, failure, &send_timing);
    timing_sample.tx_us                         = send_timing.tx_us;
    timing_sample.wait_rx_us                    = send_timing.wait_rx_us;
#else
    era_split_transaction_engine_result_t result = era_split_transaction_io_send_compact_owned(request_direction, payload, payload_len, owner_epoch, failure);
#endif
    if (result != ERA_SPLIT_TRANSACTION_RESULT_OK) {
        era_split_transaction_engine_note_owned_result(result, *failure);
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
        era_split_transaction_engine_timing_finish_sample(&timing_sample, result);
#endif
        return result;
    }

    era_split_transaction_engine_commit_prepared_tx(tx_seq);
    g_era_split_transaction_engine.compact_tx_count++;
    if (request_sent != NULL) {
        *request_sent = true;
    }

    era_split_wire_direction_t response_direction = request_direction == ERA_SPLIT_WIRE_DIRECTION_PRIMARY_TO_SECONDARY ? ERA_SPLIT_WIRE_DIRECTION_SECONDARY_TO_PRIMARY : ERA_SPLIT_WIRE_DIRECTION_PRIMARY_TO_SECONDARY;
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transaction_io_receive_timing_t receive_timing = {0};
    result = era_split_transaction_io_receive_compact_owned(response_direction, response_window_ms, owner_epoch, response, failure, &receive_timing);
    timing_sample.wait_rx_us += receive_timing.wait_rx_us;
    timing_sample.rx_decode_us += receive_timing.rx_decode_us;
    if (result == ERA_SPLIT_TRANSACTION_RESULT_OK) {
        era_split_transaction_engine_timing_note_response(&timing_sample, response);
    }
#else
    result = era_split_transaction_io_receive_compact_owned(response_direction, response_window_ms, owner_epoch, response, failure);
#endif
    if (result != ERA_SPLIT_TRANSACTION_RESULT_OK) {
        era_split_transaction_engine_note_owned_result(result, *failure);
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
        era_split_transaction_engine_timing_finish_sample(&timing_sample, result);
#endif
        return result;
    }
    g_era_split_transaction_engine.compact_rx_count++;

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    uint32_t validation_start_us = era_split_transaction_engine_timing_read_us();
#endif
    if (!era_split_transaction_engine_response_kind_matches(expected_kind, alternate_expected_kind, response->kind) || response->ack_seq != tx_seq) {
        *failure = ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_CONTRACT;
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
        timing_sample.rx_decode_us += era_split_transaction_engine_timing_elapsed_us(validation_start_us);
#endif
        era_split_transaction_engine_note_owned_result(ERA_SPLIT_TRANSACTION_RESULT_BAD, *failure);
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
        era_split_transaction_engine_timing_finish_sample(&timing_sample, ERA_SPLIT_TRANSACTION_RESULT_BAD);
#endif
        return ERA_SPLIT_TRANSACTION_RESULT_BAD;
    }
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    timing_sample.rx_decode_us += era_split_transaction_engine_timing_elapsed_us(validation_start_us);
#endif

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transaction_engine_timing_finish_sample(&timing_sample, ERA_SPLIT_TRANSACTION_RESULT_OK);
#endif
    return ERA_SPLIT_TRANSACTION_RESULT_OK;
}

static void era_split_transaction_engine_write_current_diagnostics(era_split_transaction_engine_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    era_split_transaction_backend_diagnostics_t backend;
    era_split_transaction_backend_get_diagnostics_snapshot(&backend);
    snapshot->transaction_backend_initialized           = backend.initialized;
    snapshot->transaction_backend_init_role             = backend.init_role;
    snapshot->transaction_backend_role                  = backend.role;
    snapshot->transaction_backend_reinit_on_role_change = backend.reinit_on_role_change;
    snapshot->tx_seq                                    = g_era_split_transaction_engine.tx_seq;
    snapshot->ack_seq                                   = g_era_split_transaction_engine.ack_seq;
    snapshot->compact_tx_count                          = g_era_split_transaction_engine.compact_tx_count;
    snapshot->compact_rx_count                          = g_era_split_transaction_engine.compact_rx_count;
    snapshot->compact_miss_count                        = g_era_split_transaction_engine.compact_miss_count;
    snapshot->compact_bad_count                         = g_era_split_transaction_engine.compact_bad_count;
    snapshot->compact_fail_count                        = g_era_split_transaction_engine.compact_fail_count;
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    /* The one projection this concept owes: the live sample carries two
       in-flight bookkeeping fields no reader may see, so it is copied out
       field by field here and travels as one struct from this point on. */
    snapshot->timing.valid                 = g_era_split_transaction_engine.timing_latest.valid ? 1 : 0;
    snapshot->timing.route_kind            = g_era_split_transaction_engine.timing_latest.route_kind;
    snapshot->timing.route_reason          = g_era_split_transaction_engine.timing_latest.route_reason;
    snapshot->timing.request_kind          = g_era_split_transaction_engine.timing_latest.request_kind;
    snapshot->timing.response_kind         = g_era_split_transaction_engine.timing_latest.response_kind;
    snapshot->timing.result                = g_era_split_transaction_engine.timing_latest.result;
    snapshot->timing.request_payload_len   = g_era_split_transaction_engine.timing_latest.request_payload_len;
    snapshot->timing.response_payload_len  = g_era_split_transaction_engine.timing_latest.response_payload_len;
    snapshot->timing.response_section_byte = g_era_split_transaction_engine.timing_latest.response_section_byte;
    snapshot->timing.payload_proxy         = g_era_split_transaction_engine.timing_latest.payload_proxy;
    snapshot->timing.tx_us                 = g_era_split_transaction_engine.timing_latest.tx_us;
    snapshot->timing.wait_rx_us            = g_era_split_transaction_engine.timing_latest.wait_rx_us;
    snapshot->timing.rx_decode_us          = g_era_split_transaction_engine.timing_latest.rx_decode_us;
    snapshot->timing.total_us              = g_era_split_transaction_engine.timing_latest.total_us;
    snapshot->timing_sample_count          = g_era_split_transaction_engine.timing_sample_count;
    snapshot->timing_timeout_count         = g_era_split_transaction_engine.timing_timeout_count;
    for (uint8_t index = 0; index < ERA_SPLIT_TRANSACTION_TIMING_BUCKET_COUNT; index++) {
        snapshot->timing_buckets[index] = g_era_split_transaction_engine.timing_buckets[index];
    }
#endif
}

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
void era_split_transaction_engine_publish_diagnostics_snapshot(void) {
    uint32_t odd_seq = (g_era_split_transaction_engine_diagnostics_publish_seq + 1U) | 1U;
    g_era_split_transaction_engine_diagnostics_publish_seq = odd_seq;
    __DMB();
    era_split_transaction_engine_write_current_diagnostics(&g_era_split_transaction_engine_diagnostics_snapshot);
    __DMB();
    g_era_split_transaction_engine_diagnostics_publish_seq = odd_seq + 1U;
    __DMB();
}

void era_split_transaction_engine_get_diagnostics_snapshot(era_split_transaction_engine_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

#ifndef ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE
    for (uint8_t retry = 0; retry < ERA_SPLIT_TRANSACTION_ENGINE_DIAGNOSTICS_READ_RETRIES; retry++) {
        uint32_t first_seq = g_era_split_transaction_engine_diagnostics_publish_seq;
        if ((first_seq & 1U) != 0) {
            continue;
        }
        __DMB();
        *snapshot = g_era_split_transaction_engine_diagnostics_snapshot;
        __DMB();
        uint32_t second_seq = g_era_split_transaction_engine_diagnostics_publish_seq;
        if (first_seq == second_seq && (second_seq & 1U) == 0) {
            g_era_split_transaction_engine_diagnostics_last_stable       = *snapshot;
            g_era_split_transaction_engine_diagnostics_last_stable_valid = 1;
            g_era_split_transaction_engine_diagnostics_snapshot_fresh    = 1;
            return;
        }
    }
#else
    /* Injection arm: the accept path is compiled out, so every read takes the
     * fallback below. It exists because the collision this fallback exists for
     * has been observed once and has resisted four attempts to provoke it, and
     * a repair validated only by the reasoning that produced it is what the
     * evidence rule forbids.
     *
     * It recreates the failure's exact shape rather than an approximation of
     * it. Nothing writes `last_stable` while this arm is compiled in, so an
     * era block's activate and its capture read identical bytes and their
     * difference is identically zero - which is what the production defect
     * produced and what made it indistinguishable from a quiet window.
     *
     * What the image proves is the half that matters: that a boundary the
     * block could not measure is declared as `meas=0` instead of reported as
     * that zero. It does not exercise the retry recovery, which needs a real
     * race. Selector-gated and absent from every production profile; the
     * whole surface it touches is diagnostics, so no behaviour depends on it
     * (`era_performance_gates.md` Source Gate). */
#endif

    g_era_split_transaction_engine_diagnostics_snapshot_fresh = 0;
    g_era_split_transaction_engine_diagnostics_fallback_count++;
    /* A writer collision discards the candidate, not the last proven state. */
    if (g_era_split_transaction_engine_diagnostics_last_stable_valid) {
        *snapshot = g_era_split_transaction_engine_diagnostics_last_stable;
    } else {
        memset(snapshot, 0, sizeof(*snapshot));
    }
}

bool era_split_transaction_engine_diagnostics_snapshot_fresh(void) {
    return g_era_split_transaction_engine_diagnostics_snapshot_fresh != 0;
}

uint32_t era_split_transaction_engine_diagnostics_fallback_count(void) {
    return g_era_split_transaction_engine_diagnostics_fallback_count;
}
#else
void era_split_transaction_engine_get_diagnostics_snapshot(era_split_transaction_engine_diagnostics_t *snapshot) {
    era_split_transaction_engine_write_current_diagnostics(snapshot);
}
#endif
