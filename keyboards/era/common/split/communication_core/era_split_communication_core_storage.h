// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../era_host_peer_storage.h"
#include "../era_split_transaction_types.h"
#include "../era_split_wire_protocol.h"

/* 32 -> 40 at the Slice 10 push lane: the initiator-sent bulk chunk is
 * served from the initiator's own published image, so the request record
 * carries the image and publication-seq addresses the way the responder
 * snapshot always has. Deliberate budget arithmetic, cap moves with it. */
#define ERA_SPLIT_COMMUNICATION_CORE_STORAGE_INITIATOR_REQUEST_BYTES 40U
#define ERA_SPLIT_COMMUNICATION_CORE_STORAGE_INITIATOR_RESULT_BYTES 280U
#define ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONDER_SNAPSHOT_BYTES 64U
#define ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONDER_RESULT_BYTES 48U
#define ERA_SPLIT_COMMUNICATION_CORE_STORAGE_WIRE_FRAME_BYTES 271U
#define ERA_SPLIT_COMMUNICATION_CORE_STORAGE_COMPACT_PAYLOAD_BYTES 15U
#define ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONSE_WINDOW_MS 25U

typedef enum {
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_INVALID = 0,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_READY,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_RESULT_FULL,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_QUEUE_EXPIRED,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_OWNER_STALE,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_RELATION_STALE,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_GENERATION_STALE,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_POLICY_STALE,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_TRANSACTION_STALE,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_CANCELLED,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_RESET,
} era_split_communication_core_storage_request_classification_t;

typedef struct {
    uint32_t not_after_us;
    uint32_t source_revision;
    uint32_t image_crc32;
    /* Push lane only (operation PUSH_CHUNK_REQ): where core1 reads the
     * chunk bytes it sends — the initiator's own published staging image
     * and its publication-seq word, zero for every other operation. */
    uint32_t image_address;
    uint32_t image_publication_seq_address;
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint16_t request_generation;
    uint16_t policy_generation;
    uint16_t transaction_generation;
    uint16_t image_size;
    uint16_t response_window_ms;
    uint8_t  domain;
    uint8_t  schema;
    uint8_t  operation;
    uint8_t  chunk_id;
    uint8_t  detail;
    uint8_t  reserved;
} era_split_communication_core_storage_initiator_request_t;

typedef struct {
    uint32_t source_revision;
    uint32_t image_crc32;
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint16_t request_generation;
    uint16_t policy_generation;
    uint16_t transaction_generation;
    uint16_t data_length;
    uint8_t  domain;
    uint8_t  schema;
    uint8_t  operation;
    uint8_t  status;
    uint8_t  chunk_id;
    uint8_t  tx_seq;
    uint8_t  result;
    uint8_t  failure;
    uint8_t  data[ERA_HOST_PEER_STORAGE_CHUNK_BYTES];
} era_split_communication_core_storage_initiator_result_t;

typedef struct {
    uint32_t source_revision;
    uint32_t image_crc32;
    uint32_t image_publication_seq;
    uint32_t image_address;
    uint32_t image_publication_seq_address;
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint16_t snapshot_generation;
    uint16_t policy_generation;
    uint16_t transaction_generation;
    uint16_t image_size;
    uint8_t  domain;
    uint8_t  schema;
    uint8_t  expected_operation;
    uint8_t  expected_chunk_id;
    uint8_t  allowed;
    uint8_t  valid;
    uint8_t  pinned;
    /* Recency seat (Slice 10): core0 publishes its cold recency view so the
     * admitted SYNC_STATUS and per-conflict counter responses are answered
     * from the snapshot — core1 never reads EEPROM. Counters are by domain
     * id, LE. */
    uint8_t  recency_changed_mask;
    uint8_t  recency_baseline_valid;
    uint8_t  recency_counter[ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT * 2U];
    /* Push staging progress, core0-published: 0 = no push episode, then
     * staging / applying / durable, which is what the admitted COMPLETE
     * poll answers from. */
    uint8_t  push_state;
    uint8_t  reserved[8];
} era_split_communication_core_storage_responder_snapshot_t;

enum {
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUSH_STATE_NONE = 0,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUSH_STATE_STAGING,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUSH_STATE_APPLYING,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUSH_STATE_DURABLE,
};

typedef struct {
    uint32_t source_revision;
    uint32_t image_crc32;
    uint32_t request_fingerprint;
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint16_t snapshot_generation;
    uint16_t policy_generation;
    uint16_t transaction_generation;
    uint16_t image_size;
    uint8_t  domain;
    uint8_t  schema;
    uint8_t  operation;
    uint8_t  status;
    uint8_t  chunk_id;
    uint8_t  data_length;
    uint8_t  result;
    uint8_t  failure;
    uint8_t  response_sent;
    uint8_t  replayed;
    uint32_t request_source_revision;
    uint32_t request_image_crc32;
    uint16_t request_policy_generation;
    uint16_t request_image_size;
} era_split_communication_core_storage_responder_result_t;

typedef struct {
    uint32_t now_us;
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint16_t request_generation;
    uint16_t policy_generation;
    uint16_t transaction_generation;
    uint8_t  cancel_requested;
    uint8_t  reset_requested;
} era_split_communication_core_storage_execution_context_t;

typedef struct {
    uint32_t responder_full_count;
    uint32_t replay_count;
} era_split_communication_core_storage_lane_diagnostics_t;

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
typedef enum {
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_NONE = 0,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_CLAIM,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_BEGIN,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_ENCODE,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_TX,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_RX,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_CONTRACT,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_PUBLISH,
} era_split_communication_core_storage_probe_stage_t;

typedef struct {
    uint32_t claim_count;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t publish_count;
    uint32_t failure_count;
    uint16_t request_claim_generation;
    uint16_t result_claim_generation;
    uint16_t result_ready_generation;
    uint8_t  last_stage;
    uint8_t  last_result;
    uint8_t  last_failure;
    uint8_t  last_operation;
    uint8_t  last_status;
    uint8_t  reserved;
} era_split_communication_core_storage_probe_diagnostics_t;

_Static_assert(sizeof(era_split_communication_core_storage_probe_diagnostics_t) == 32U,
               "ERA storage probe diagnostics budget changed.");
#endif

_Static_assert(sizeof(uintptr_t) == sizeof(uint32_t), "ERA storage shared pointers require the RP2040 32-bit address space.");
_Static_assert(sizeof(era_split_communication_core_storage_initiator_request_t) == ERA_SPLIT_COMMUNICATION_CORE_STORAGE_INITIATOR_REQUEST_BYTES,
               "ERA storage initiator request must stay 40 bytes.");
_Static_assert(sizeof(era_split_communication_core_storage_initiator_result_t) == ERA_SPLIT_COMMUNICATION_CORE_STORAGE_INITIATOR_RESULT_BYTES,
               "ERA storage initiator result must stay 280 bytes.");
_Static_assert(sizeof(era_split_communication_core_storage_responder_snapshot_t) == ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONDER_SNAPSHOT_BYTES,
               "ERA storage responder snapshot must stay 64 bytes.");
_Static_assert(sizeof(era_split_communication_core_storage_responder_result_t) == ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONDER_RESULT_BYTES,
               "ERA storage responder result must stay 48 bytes.");

void era_split_communication_core_storage_capacity_init(void);
void era_split_communication_core_storage_note_responder_full(void);
void era_split_communication_core_storage_note_replay(void);
void era_split_communication_core_storage_get_lane_diagnostics(era_split_communication_core_storage_lane_diagnostics_t *snapshot);
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
void era_split_communication_core_storage_note_initiator_probe(era_split_communication_core_storage_probe_stage_t stage,
                                                               bool success,
                                                               const era_split_communication_core_storage_initiator_result_t *result);
void era_split_communication_core_storage_get_probe_diagnostics(era_split_communication_core_storage_probe_diagnostics_t *snapshot);
#endif

bool era_split_communication_core_storage_validate_wire_payload(const uint8_t *payload, uint16_t payload_len, era_split_wire_frame_lane_t lane);
bool era_split_communication_core_storage_encode_request_payload(uint8_t control, const era_split_communication_core_storage_initiator_request_t *request, uint8_t *payload, uint16_t *payload_len);
bool era_split_communication_core_storage_decode_request_payload(const uint8_t *payload, uint16_t payload_len, era_split_wire_frame_lane_t lane, era_split_communication_core_storage_initiator_request_t *request);
bool era_split_communication_core_storage_decode_response_payload(const era_split_communication_core_storage_initiator_request_t *request, const uint8_t *payload, uint16_t payload_len, era_split_wire_frame_lane_t lane, uint8_t expected_ack_seq, era_split_communication_core_storage_initiator_result_t *result);

bool era_split_communication_core_storage_reserve_initiator_result(uint16_t request_generation);
bool era_split_communication_core_storage_publish_initiator_request(const era_split_communication_core_storage_initiator_request_t *request);
bool era_split_communication_core_storage_claim_initiator_request(era_split_communication_core_storage_initiator_request_t *request);
void era_split_communication_core_storage_release_initiator_request(uint16_t request_generation);
era_split_communication_core_storage_initiator_result_t *era_split_communication_core_storage_begin_initiator_result(uint16_t request_generation);
bool era_split_communication_core_storage_publish_initiator_result(const era_split_communication_core_storage_initiator_result_t *result);
bool era_split_communication_core_storage_acquire_initiator_result(const era_split_communication_core_storage_initiator_result_t **result);
bool era_split_communication_core_storage_release_initiator_result(uint16_t request_generation);
bool era_split_communication_core_storage_initiator_result_ready(void);
bool era_split_communication_core_storage_result_due(void);
void era_split_communication_core_storage_cancel_initiator_result(uint16_t request_generation);
era_split_communication_core_storage_request_classification_t era_split_communication_core_storage_classify_request(const era_split_communication_core_storage_initiator_request_t *request, const era_split_communication_core_storage_execution_context_t *context);

bool era_split_communication_core_storage_publish_responder_snapshot(const era_split_communication_core_storage_responder_snapshot_t *snapshot);
bool era_split_communication_core_storage_claim_responder_snapshot(era_split_communication_core_storage_responder_snapshot_t *snapshot);
void era_split_communication_core_storage_release_responder_snapshot(uint16_t snapshot_generation);
bool era_split_communication_core_storage_reserve_responder_result(uint16_t snapshot_generation);
bool era_split_communication_core_storage_publish_responder_result(const era_split_communication_core_storage_responder_result_t *result);
bool era_split_communication_core_storage_drain_responder_result(era_split_communication_core_storage_responder_result_t *result);
bool era_split_communication_core_storage_responder_result_ready(void);
void era_split_communication_core_storage_cancel_responder_result(uint16_t snapshot_generation);
bool era_split_communication_core_storage_copy_previous_responder_result(era_split_communication_core_storage_responder_result_t *result);
bool era_split_communication_core_storage_plan_responder(const era_split_communication_core_storage_responder_snapshot_t *snapshot, const era_split_communication_core_storage_initiator_request_t *request, const era_split_communication_core_storage_responder_result_t *previous, era_split_communication_core_storage_responder_result_t *result);
bool era_split_communication_core_storage_prepare_responder_payload(uint8_t control, const era_split_communication_core_storage_responder_snapshot_t *snapshot, era_split_communication_core_storage_responder_result_t *result, uint8_t *payload, uint16_t *payload_len, era_split_wire_frame_lane_t *lane);
bool era_split_communication_core_storage_stage_push_chunk(const era_split_communication_core_storage_responder_snapshot_t *snapshot, uint8_t chunk_id, const uint8_t *data, uint8_t length);
bool era_split_communication_core_storage_build_push_chunk_payload(uint8_t control, const era_split_communication_core_storage_initiator_request_t *request, uint8_t *payload, uint16_t *payload_len);

uint8_t *era_split_communication_core_storage_wire_frame_scratch(void);
uint8_t *era_split_communication_core_storage_decoded_frame_scratch(void);
era_split_wire_frame_t *era_split_communication_core_storage_decoded_semantic_frame_scratch(void);
