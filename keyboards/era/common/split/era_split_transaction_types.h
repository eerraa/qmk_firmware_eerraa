// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

typedef enum {
    ERA_SPLIT_TRANSACTION_RESULT_NONE = 0,
    ERA_SPLIT_TRANSACTION_RESULT_OK,
    ERA_SPLIT_TRANSACTION_RESULT_MISS,
    ERA_SPLIT_TRANSACTION_RESULT_BAD,
    ERA_SPLIT_TRANSACTION_RESULT_FAIL,
} era_split_transaction_engine_result_t;

typedef enum {
    ERA_SPLIT_TRANSACTION_FAILURE_NONE = 0,
    ERA_SPLIT_TRANSACTION_FAILURE_QUEUE_EXPIRED,
    ERA_SPLIT_TRANSACTION_FAILURE_OWNER,
    ERA_SPLIT_TRANSACTION_FAILURE_EPOCH,
    ERA_SPLIT_TRANSACTION_FAILURE_CANCEL,
    ERA_SPLIT_TRANSACTION_FAILURE_RESET,
    ERA_SPLIT_TRANSACTION_FAILURE_PIO_ERROR,
    ERA_SPLIT_TRANSACTION_FAILURE_SEND_TIMEOUT,
    ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT,
    ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME,
    ERA_SPLIT_TRANSACTION_FAILURE_IO,
    ERA_SPLIT_TRANSACTION_FAILURE_DECODE,
    ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_CONTRACT,
} era_split_transaction_failure_t;

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
typedef enum {
    ERA_SPLIT_TRANSACTION_TIMING_BUCKET_HEARTBEAT_ACK = 0,
    ERA_SPLIT_TRANSACTION_TIMING_BUCKET_SOURCE_PUSH_ACK,
    ERA_SPLIT_TRANSACTION_TIMING_BUCKET_HOST_SOURCE_RSP,
    ERA_SPLIT_TRANSACTION_TIMING_BUCKET_PROXY,
    ERA_SPLIT_TRANSACTION_TIMING_BUCKET_COUNT,
} era_split_transaction_timing_bucket_t;

typedef struct {
    uint32_t sample_count;
    uint32_t timeout_count;
    uint32_t tx_us_total;
    uint32_t wait_rx_us_total;
    uint32_t rx_decode_us_total;
    uint32_t total_us_total;
    uint32_t total_us_last;
} era_split_transaction_timing_bucket_diagnostics_t;

/* The published half of one transaction's timing sample. It has three readers
   -- the engine's diagnostics record below, the scheduler snapshot, and the
   peer-era block beside that one -- and each of them used to spell these
   fourteen names out under a prefix of its own, so one sample was written
   field by field three times over. A fifteenth field now costs one member here
   and one struct assignment per reader. That is the argument the era-common
   head in `era_split_transport_scheduler_diagnostics.h` already carries,
   applied to the larger repetition standing beside it.

   The engine's *live* sample keeps a type of its own rather than embedding
   this one: two of its fields are in-flight bookkeeping that no reader may
   see, so the producer owes one projection and the readers owe none. */
typedef struct {
    uint8_t  valid;
    uint8_t  route_kind;
    uint8_t  route_reason;
    uint8_t  request_kind;
    uint8_t  response_kind;
    uint8_t  result;
    uint8_t  request_payload_len;
    uint8_t  response_payload_len;
    uint8_t  response_section_byte;
    uint8_t  payload_proxy;
    uint32_t tx_us;
    uint32_t wait_rx_us;
    uint32_t rx_decode_us;
    uint32_t total_us;
} era_split_transaction_timing_diagnostics_t;
#endif

typedef struct {
    uint8_t  transaction_backend_initialized;
    uint8_t  transaction_backend_init_role;
    uint8_t  transaction_backend_role;
    uint8_t  transaction_backend_reinit_on_role_change;
    uint8_t  tx_seq;
    uint8_t  ack_seq;
    uint32_t compact_tx_count;
    uint32_t compact_rx_count;
    uint32_t compact_miss_count;
    uint32_t compact_bad_count;
    uint32_t compact_fail_count;
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transaction_timing_diagnostics_t timing;
    uint32_t timing_sample_count;
    uint32_t timing_timeout_count;
    era_split_transaction_timing_bucket_diagnostics_t timing_buckets[ERA_SPLIT_TRANSACTION_TIMING_BUCKET_COUNT];
#endif
} era_split_transaction_engine_diagnostics_t;
