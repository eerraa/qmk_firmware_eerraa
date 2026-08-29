// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_communication_core_storage.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "hardware/structs/timer.h"

#include "../era_split_wire_frame.h"

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
#    include "../era_split_transport_scheduler_diagnostics.h"
#endif

typedef struct {
    era_split_communication_core_storage_initiator_request_t record;
    volatile uint32_t                                        publication_seq;
    volatile uint32_t                                        claim_generation;
} era_split_communication_core_storage_initiator_request_slot_t;

typedef struct {
    era_split_communication_core_storage_initiator_result_t record;
    volatile uint32_t                                       publication_seq;
    volatile uint32_t                                       claim_generation;
    volatile uint32_t                                       ready_generation;
} era_split_communication_core_storage_initiator_result_slot_t;

typedef struct {
    era_split_communication_core_storage_responder_snapshot_t record;
    volatile uint32_t                                         publication_seq;
    volatile uint32_t                                         claim_generation;
} era_split_communication_core_storage_responder_snapshot_slot_t;

typedef struct {
    era_split_communication_core_storage_responder_result_t record;
    volatile uint32_t                                       publication_seq;
    volatile uint32_t                                       claim_generation;
    volatile uint32_t                                       ready_generation;
} era_split_communication_core_storage_responder_result_slot_t;

#define ERA_SPLIT_COMMUNICATION_CORE_STORAGE_ALIGN4(value) (((value) + 3U) & ~3U)
/* publication_begin() reserves the top two values against wrap. The odd top
 * value is consequently a terminal seqlock state: readers cannot claim it and
 * publishers cannot advance it for the rest of this boot. */
#define ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED UINT32_MAX

enum {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_DIAGNOSTIC_BYTES = sizeof(era_split_communication_core_storage_probe_diagnostics_t),
#else
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_DIAGNOSTIC_BYTES = 0,
#endif
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_STATIC_BYTES =
        sizeof(era_split_communication_core_storage_initiator_request_slot_t) +
        sizeof(era_split_communication_core_storage_initiator_result_slot_t) +
        sizeof(era_split_communication_core_storage_responder_snapshot_slot_t) +
        sizeof(era_split_communication_core_storage_responder_result_slot_t) +
        (ERA_SPLIT_COMMUNICATION_CORE_STORAGE_ALIGN4(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_WIRE_FRAME_BYTES) * 2U) +
        ERA_SPLIT_COMMUNICATION_CORE_STORAGE_ALIGN4(ERA_HOST_PEER_STORAGE_CHUNK_BYTES) +
        sizeof(era_split_communication_core_storage_lane_diagnostics_t) +
        ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_DIAGNOSTIC_BYTES +
        ERA_SPLIT_COMMUNICATION_CORE_STORAGE_ALIGN4(sizeof(era_split_wire_frame_t)),
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_READ_RETRIES = 4,
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_DIAGNOSTIC_COPY_BYTES = ERA_HOST_PEER_STORAGE_DIAGNOSTICS_BYTES + ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_DIAGNOSTIC_BYTES,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_EDGE_DIAGNOSTIC_BYTES = 0,
#else
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_DIAGNOSTIC_COPY_BYTES = 0,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_EDGE_DIAGNOSTIC_BYTES = 0,
#endif
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_CAUSE_TIMELINE_BYTES = ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_STATIC_BYTES,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_CAUSE_EDGE_BYTES = ERA_HOST_PEER_STORAGE_CAUSE_EDGE_STATIC_BYTES,
#else
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_CAUSE_TIMELINE_BYTES = 0,
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_CAUSE_EDGE_BYTES = 0,
#endif
    ERA_SPLIT_COMMUNICATION_CORE_STORAGE_AGGREGATE_STATIC_BYTES =
        ERA_HOST_PEER_STORAGE_IMAGE_BYTES +
        (ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT * ERA_HOST_PEER_STORAGE_MANIFEST_ENTRY_BYTES) +
        ERA_HOST_PEER_STORAGE_CORE0_STATE_BYTES + ERA_HOST_PEER_STORAGE_DIAGNOSTICS_BYTES + ERA_SPLIT_COMMUNICATION_CORE_STORAGE_STATIC_BYTES +
        ERA_SPLIT_COMMUNICATION_CORE_STORAGE_DIAGNOSTIC_COPY_BYTES +
        ERA_SPLIT_COMMUNICATION_CORE_STORAGE_EDGE_DIAGNOSTIC_BYTES +
        ERA_SPLIT_COMMUNICATION_CORE_STORAGE_CAUSE_TIMELINE_BYTES +
        ERA_SPLIT_COMMUNICATION_CORE_STORAGE_CAUSE_EDGE_BYTES,
};

/* 274 until the write-only `subtype` byte left the frame record. The struct's
   alignment is 2 (its widest member is `payload_len`), so the byte does not
   vanish into padding: 274 -> 272, and `ALIGN4` of it 276 -> 272, which is the
   whole of the -4 in the budget below. Both figures are equalities rather than
   bounds on purpose — a frame record that grows silently is what these catch,
   and they caught this one. */
_Static_assert(sizeof(era_split_wire_frame_t) == 272U, "ERA storage decoded semantic frame budget changed.");
_Static_assert(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_STATIC_BYTES ==
                   1548U + ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_DIAGNOSTIC_BYTES +
                       (ERA_SPLIT_COMMUNICATION_CORE_STORAGE_INITIATOR_REQUEST_BYTES - 40U),
               "ERA communication-core storage capacity budget changed.");
/* The cap guard is unconditional. It used to live in the non-cause arm only, so
   the one profile that exceeds ERA_HOST_PEER_STORAGE_STATIC_BUDGET_BYTES was
   also the one profile with no cap check — its equality against an over-cap
   constant read as review rather than as an overage. The cause timeline's
   records are now an explicit addition to the profile budget instead of a
   silent exemption. ERA NVM removes the old flash-write edge record entirely;
   physical program/erase failures are counted by the NVM engine instead. */
_Static_assert(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_AGGREGATE_STATIC_BYTES <= ERA_HOST_PEER_STORAGE_PROFILE_BUDGET_BYTES,
               "ERA HOST-PEER storage static budget exceeds the profile cap.");
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
/* 18544 -> 18672 when the timeline was resized to span a macro episode: its
   two records went 48 -> 112 bytes each, so the addition over the then
   18448-byte wire-diagnostics aggregate went 96 -> 224. Then 18672 -> 18676
   with that aggregate's own move to 18452, and 18676 -> 18672 at D2 as it
   moved back, 18672 -> 18668 with the frame record's `subtype` byte, and
   18668 -> 18660 with the core0 state's two write-only fields (2026-08-11),
   and 18660 -> 18668 with the indicator redesign's core0 state move
   136 -> 144 (2026-08-14), 18668 -> 18660 when the wire-reset edge record
   lost the two `hbmiss` counters with the heartbeat lane, and 18660 -> 19988
   for the interval-scoped indicator/write edge recorder, then 19988 -> 20024
   for the replacement-Apply slice swap's 36-byte staged-old state, and
   20024 -> 20048 for two copies of the 12-byte retained Core1 failure detail,
   and 20048 -> 20124 for the diagnostic request timestamp and two copies of
   the 36-byte retained queue/preceding-route context. ERA NVM then removes the
   40-byte slice/raw-facade state and the 8-byte flash-edge live/snapshot pair:
   20124 -> 20076.
   Deliberate arithmetic on a selector-gated variant that is never an
   acceptance build. */
_Static_assert(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_AGGREGATE_STATIC_BYTES == 20076U,
               "ERA HOST-PEER cause diagnostic budget changed.");
#else
#    ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
/* 18452 -> 18448 at D2, the core0 state's four bytes coming back, and
   18448 -> 18444 when the write-only `subtype` byte left the frame record and
   took four bytes of its ALIGN4 with it, and 18444 -> 18436 when the core0
   state's two write-only fields left the local truth record (2026-08-11), and
   18436 -> 18444 at the 2026-08-14 indicator redesign — the changed shadow in
   local truth and the indicator bits in the relation record, the RAM the
   retired 1500 ms lamp bridge's job now lives in, and 18444 -> 18436 when the
   wire-reset edge record lost its two `hbmiss` counters with the heartbeat
   lane (2026-08-17), 18436 -> 18472 for the replacement-Apply slice swap's
   36-byte staged-old state, 18472 -> 18496 for two copies of the 12-byte
   retained Core1 failure detail, and 18496 -> 18572 for the diagnostic
   request timestamp and two copies of the 36-byte retained queue/preceding-
   route context. ERA NVM removes the 40-byte slice/raw-facade state and the
   8-byte flash-edge live/snapshot pair, 18572 -> 18524. */
_Static_assert(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_AGGREGATE_STATIC_BYTES == 18524U,
               "ERA HOST-PEER wire-diagnostics static budget changed.");
#    endif
#endif
_Static_assert(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_COMPACT_PAYLOAD_BYTES == ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN,
               "ERA storage compact payload must fill the compact lane.");
_Static_assert(12U + ERA_HOST_PEER_STORAGE_CHUNK_BYTES == ERA_SPLIT_WIRE_BULK_PAGE_MAX_PAYLOAD_LEN,
               "ERA storage chunk response must fill the bounded bulk payload.");

static era_split_communication_core_storage_initiator_request_slot_t g_era_split_communication_core_storage_initiator_request __attribute__((aligned(4)));
static era_split_communication_core_storage_initiator_result_slot_t g_era_split_communication_core_storage_initiator_result __attribute__((aligned(4)));
static era_split_communication_core_storage_responder_snapshot_slot_t g_era_split_communication_core_storage_responder_snapshot __attribute__((aligned(4)));
static era_split_communication_core_storage_responder_result_slot_t g_era_split_communication_core_storage_responder_result __attribute__((aligned(4)));
static uint8_t g_era_split_communication_core_storage_wire_frame[ERA_SPLIT_COMMUNICATION_CORE_STORAGE_WIRE_FRAME_BYTES] __attribute__((aligned(4)));
static uint8_t g_era_split_communication_core_storage_decoded_frame[ERA_SPLIT_COMMUNICATION_CORE_STORAGE_WIRE_FRAME_BYTES] __attribute__((aligned(4)));
static uint8_t g_era_split_communication_core_storage_chunk_scratch[ERA_HOST_PEER_STORAGE_CHUNK_BYTES] __attribute__((aligned(4)));
static era_split_communication_core_storage_lane_diagnostics_t g_era_split_communication_core_storage_lane_diagnostics __attribute__((aligned(4)));
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
static era_split_communication_core_storage_probe_diagnostics_t g_era_split_communication_core_storage_probe_diagnostics __attribute__((aligned(4)));
#endif
static era_split_wire_frame_t g_era_split_communication_core_storage_decoded_semantic_frame __attribute__((aligned(4)));

static bool era_split_communication_core_storage_reserved_zero(const uint8_t *payload, uint16_t first, uint16_t last) {
    if (payload == NULL || first > last) {
        return false;
    }
    for (uint16_t index = first; index <= last; index++) {
        if (payload[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool era_split_communication_core_storage_control_valid(uint8_t control) {
    return (control & ERA_SPLIT_WIRE_CONTROL_RESERVED) == 0 &&
           (control & ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK) != 0 &&
           (control & ERA_SPLIT_WIRE_CONTROL_EXT) != 0;
}

static bool era_split_communication_core_storage_status_valid(uint8_t status) {
    return status <= (uint8_t)ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED;
}

static bool era_split_communication_core_storage_request_operation_valid(uint8_t operation) {
    switch ((era_split_eeprom_sync_op_t)operation) {
        case ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ:
        case ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ:
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ:
        case ERA_SPLIT_EEPROM_SYNC_OP_COMPLETE_REQ:
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_REQ:
        case ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ:
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ:
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ:
            return true;
        default:
            return false;
    }
}

static bool era_split_communication_core_storage_response_operation_valid(uint8_t operation) {
    switch ((era_split_eeprom_sync_op_t)operation) {
        case ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP:
        case ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_RSP:
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_RSP:
        case ERA_SPLIT_EEPROM_SYNC_OP_CLOSE_RSP:
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP:
        case ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_RSP:
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_RSP:
            return true;
        default:
            return false;
    }
}

static bool era_split_communication_core_storage_response_matches_request(uint8_t request_operation, uint8_t response_operation, uint8_t status) {
    if (response_operation == era_split_eeprom_sync_response_operation(request_operation)) {
        return true;
    }
    return request_operation == ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ &&
           response_operation == ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP &&
           status == ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED;
}

static uint16_t era_split_communication_core_storage_domain_size(uint8_t domain) {
    /* The shared schema-1 sizes, never literals here: core0 asserts each of
     * these macros against the real EEPROM layout it can see and core1
     * cannot, so this table cannot drift from that layout without failing
     * the build on core0's side. See era_host_peer_storage.h. */
    static const uint16_t domain_sizes[ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT] = {
        [ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG]         = ERA_HOST_PEER_STORAGE_DOMAIN_ERA_CONFIG_BYTES,
        [ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP]     = ERA_HOST_PEER_STORAGE_DOMAIN_DYNAMIC_KEYMAP_BYTES,
        [ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_MACRO]      = ERA_HOST_PEER_STORAGE_DOMAIN_DYNAMIC_MACRO_BYTES,
        [ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_RGB_MATRIX]     = ERA_HOST_PEER_STORAGE_DOMAIN_QMK_RGB_MATRIX_BYTES,
        [ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_KEYMAP_CONFIG]  = ERA_HOST_PEER_STORAGE_DOMAIN_QMK_KEYMAP_CONFIG_BYTES,
        [ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_DEFAULT_LAYER]  = ERA_HOST_PEER_STORAGE_DOMAIN_QMK_DEFAULT_LAYER_BYTES,
        [ERA_SPLIT_EEPROM_SYNC_DOMAIN_VIA_LAYOUT_OPTIONS] = ERA_HOST_PEER_STORAGE_DOMAIN_VIA_LAYOUT_OPTIONS_BYTES,
    };
    return domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT ? domain_sizes[domain] : 0;
}

static uint8_t era_split_communication_core_storage_chunk_length(uint16_t image_size, uint8_t chunk_id) {
    uint32_t offset = (uint32_t)chunk_id * ERA_HOST_PEER_STORAGE_CHUNK_BYTES;
    if (offset >= image_size) {
        return 0;
    }
    uint16_t remaining = (uint16_t)(image_size - offset);
    return (uint8_t)(remaining > ERA_HOST_PEER_STORAGE_CHUNK_BYTES ? ERA_HOST_PEER_STORAGE_CHUNK_BYTES : remaining);
}

static bool era_split_communication_core_storage_compact_payload_valid(const uint8_t *payload, uint16_t payload_len) {
    if (payload == NULL || payload_len != ERA_SPLIT_COMMUNICATION_CORE_STORAGE_COMPACT_PAYLOAD_BYTES ||
        !era_split_communication_core_storage_control_valid(payload[0]) ||
        era_split_wire_get16(&payload[4]) == 0) {
        return false;
    }

    switch ((era_split_eeprom_sync_op_t)payload[1]) {
        case ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ:
            return era_split_wire_get16(&payload[8]) != 0 && payload[14] == 0;
        case ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP:
            return era_split_communication_core_storage_status_valid(payload[6]) &&
                   ((payload[6] != ERA_SPLIT_EEPROM_SYNC_STATUS_MATCH && payload[6] != ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER) ||
                    era_split_wire_get32(&payload[7]) != 0);
        case ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ:
            /* Bytes 12..14 carry the 24-bit local-chunk CRC hint (zero
             * means no hint); any value is valid. */
            return era_split_wire_get32(&payload[6]) != 0 &&
                   payload[10] < ERA_HOST_PEER_STORAGE_MAX_CHUNKS &&
                   payload[11] > 0 && payload[11] <= ERA_HOST_PEER_STORAGE_CHUNK_BYTES;
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ:
        case ERA_SPLIT_EEPROM_SYNC_OP_COMPLETE_REQ:
            return era_split_wire_get32(&payload[6]) != 0 && payload[14] == 0;
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_RSP:
        case ERA_SPLIT_EEPROM_SYNC_OP_CLOSE_RSP:
            return era_split_communication_core_storage_status_valid(payload[6]);
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_REQ:
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP:
            return era_split_communication_core_storage_status_valid(payload[10]) &&
                   era_split_communication_core_storage_reserved_zero(payload, 11, 14);
        case ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ:
            /* Summary form (domain 0xFF): initiator changed mask + baseline
             * validity. Per-conflict form (real domain): changed flag +
             * 16-bit divergence counter. */
            if (payload[2] == ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE) {
                return (payload[6] & 0x80U) == 0 && payload[7] <= 1U &&
                       era_split_communication_core_storage_reserved_zero(payload, 8, 14);
            }
            return payload[2] < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT && payload[6] <= 1U && payload[7] <= 1U &&
                   era_split_communication_core_storage_reserved_zero(payload, 10, 14);
        case ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_RSP:
            /* The summary response carries the relation time-anchor seat at
             * bytes 8..11. **It MUST read zero permanently, and this
             * validator is that enforcement.** The seat exists at all because
             * DUAL-HOST once had no response slot that could carry an HSRSP
             * section, and this summary response was the only
             * responder-to-initiator frame that relation had. Slice 11 gave
             * the relation a runtime response slot and Slice 13 put the
             * anchor on the `TIME_ANCHOR` section, so the anchor now has one
             * carrier in every relation and this seat is reserved for
             * nothing.
             *
             * This comment used to read "until Slice 12 arms writer and
             * consumer together", which invited the next session to finish an
             * unfinished job. Slice 12 landed, opened RGB_STATE, and did not
             * touch the seat; any writer of it is on
             * `era_closed_surface_contract.md`'s permanently-closed list, and
             * the zero reading is collected as a gate leg. Arming it is
             * forbidden, not pending. */
            if (payload[2] == ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE) {
                return (payload[6] & 0x80U) == 0 && payload[7] <= 1U &&
                       era_split_communication_core_storage_reserved_zero(payload, 8, 14);
            }
            return payload[2] < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT && payload[6] <= 1U && payload[7] <= 1U &&
                   era_split_communication_core_storage_reserved_zero(payload, 10, 14);
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ:
            if (payload[2] >= ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT ||
                payload[6] > (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_ABORT) {
                return false;
            }
            if (payload[6] == (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_ABORT) {
                return era_split_wire_get32(&payload[7]) != 0 &&
                       era_split_communication_core_storage_status_valid(payload[11]) &&
                       era_split_communication_core_storage_reserved_zero(payload, 12, 14);
            }
            return era_split_wire_get32(&payload[7]) != 0;
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_RSP:
            return payload[2] < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT &&
                   era_split_communication_core_storage_status_valid(payload[6]);
        default:
            return false;
    }
}

static bool era_split_communication_core_storage_bulk_payload_valid(const uint8_t *payload, uint16_t payload_len) {
    if (payload == NULL || payload_len < 12U || payload_len > ERA_SPLIT_WIRE_BULK_PAGE_MAX_PAYLOAD_LEN ||
        !era_split_communication_core_storage_control_valid(payload[0]) ||
        (payload[1] != ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_RSP && payload[1] != ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ) ||
        era_split_wire_get16(&payload[4]) == 0 ||
        era_split_wire_get32(&payload[6]) == 0 ||
        payload[10] >= ERA_HOST_PEER_STORAGE_MAX_CHUNKS ||
        payload[11] > ERA_HOST_PEER_STORAGE_CHUNK_BYTES) {
        return false;
    }
    if (payload[1] == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ) {
        /* The initiator-sent bulk request always carries data: the push lane
         * has no zero-length content-match form (no delta-hint mirror is
         * open), and its domain byte must be real. */
        if (payload[2] >= ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT || payload[11] == 0) {
            return false;
        }
    }
    /* For CHUNK_RSP a zero data length is the content-match acknowledgement. */
    return payload_len == (uint16_t)(12U + payload[11]);
}

bool era_split_communication_core_storage_validate_wire_payload(const uint8_t *payload, uint16_t payload_len, era_split_wire_frame_lane_t lane) {
    if (lane == ERA_SPLIT_WIRE_FRAME_LANE_COMPACT) {
        return era_split_communication_core_storage_compact_payload_valid(payload, payload_len);
    }
    if (lane == ERA_SPLIT_WIRE_FRAME_LANE_BULK_PAGE) {
        return era_split_communication_core_storage_bulk_payload_valid(payload, payload_len);
    }
    return false;
}

static bool era_split_communication_core_storage_initiator_request_valid(const era_split_communication_core_storage_initiator_request_t *request) {
    if (request == NULL || request->owner_epoch == 0 || request->relation_generation == 0 ||
        request->request_generation == 0 || request->transaction_generation == 0 ||
        request->response_window_ms != ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONSE_WINDOW_MS ||
        request->schema != ERA_HOST_PEER_STORAGE_SCHEMA_V1 ||
        request->reserved != 0 || !era_split_communication_core_storage_request_operation_valid(request->operation)) {
        return false;
    }
    /* The whole-family SYNC_STATUS summary is the one request with no real
     * domain; everything else stays domain-bound to its schema-fixed size. */
    bool summary = request->operation == ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ &&
                   request->domain == ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    if (summary) {
        if (request->image_size != 0) {
            return false;
        }
    } else if (request->domain >= ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT ||
               request->image_size != era_split_communication_core_storage_domain_size(request->domain)) {
        return false;
    }
    /* The image addresses exist for exactly one operation: the bulk chunk
     * TX that serves data from the initiator's own published image. */
    if (request->operation == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ) {
        if (request->image_address == 0 || request->image_publication_seq_address == 0) {
            return false;
        }
    } else if (request->image_address != 0 || request->image_publication_seq_address != 0) {
        return false;
    }

    switch ((era_split_eeprom_sync_op_t)request->operation) {
        case ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ:
            return request->source_revision == 0 && request->chunk_id == 0 && request->detail == 0;
        case ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ:
            /* image_crc32 carries the 24-bit local-chunk CRC hint. */
            return request->source_revision != 0 &&
                   request->image_crc32 <= 0xFFFFFFUL &&
                   request->chunk_id < ERA_HOST_PEER_STORAGE_MAX_CHUNKS &&
                   request->detail == era_split_communication_core_storage_chunk_length(request->image_size, request->chunk_id);
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ:
        case ERA_SPLIT_EEPROM_SYNC_OP_COMPLETE_REQ:
            return request->source_revision != 0 && request->chunk_id == 0 && request->detail == 0;
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_REQ:
            return request->chunk_id == 0 && era_split_communication_core_storage_status_valid(request->detail);
        case ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ:
            /* detail: changed flag (per-domain) or summary mask, bit7
             * clear; chunk_id: baseline validity; image_crc32: the 16-bit
             * divergence counter on the per-conflict form only. */
            return request->source_revision == 0 && request->chunk_id <= 1U &&
                   (summary ? (request->detail & 0x80U) == 0 && request->image_crc32 == 0
                            : request->detail <= 1U && request->image_crc32 <= 0xFFFFUL);
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ:
            if (request->source_revision == 0 ||
                request->detail > (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_ABORT) {
                return false;
            }
            if (request->detail == (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_ABORT) {
                /* chunk_id carries the abort reason. */
                return era_split_communication_core_storage_status_valid(request->chunk_id) && request->image_crc32 == 0;
            }
            return request->chunk_id == 0;
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ:
            return request->source_revision != 0 && request->image_crc32 == 0 &&
                   request->chunk_id < ERA_HOST_PEER_STORAGE_MAX_CHUNKS &&
                   request->detail != 0 &&
                   request->detail == era_split_communication_core_storage_chunk_length(request->image_size, request->chunk_id);
        default:
            return false;
    }
}

bool era_split_communication_core_storage_encode_request_payload(uint8_t control, const era_split_communication_core_storage_initiator_request_t *request, uint8_t *payload, uint16_t *payload_len) {
    if (!era_split_communication_core_storage_initiator_request_valid(request) || payload == NULL || payload_len == NULL ||
        !era_split_communication_core_storage_control_valid((uint8_t)(control | ERA_SPLIT_WIRE_CONTROL_EXT))) {
        return false;
    }

    memset(payload, 0, ERA_SPLIT_COMMUNICATION_CORE_STORAGE_COMPACT_PAYLOAD_BYTES);
    payload[0] = (uint8_t)(control | ERA_SPLIT_WIRE_CONTROL_EXT);
    payload[1] = request->operation;
    payload[2] = request->domain;
    payload[3] = request->schema;
    era_split_wire_put16(&payload[4], request->transaction_generation);

    switch ((era_split_eeprom_sync_op_t)request->operation) {
        case ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ:
            era_split_wire_put16(&payload[6], request->policy_generation);
            era_split_wire_put16(&payload[8], request->image_size);
            era_split_wire_put32(&payload[10], request->image_crc32);
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ:
            era_split_wire_put32(&payload[6], request->source_revision);
            payload[10] = request->chunk_id;
            payload[11] = request->detail;
            era_split_wire_put24(&payload[12], request->image_crc32);
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ:
        case ERA_SPLIT_EEPROM_SYNC_OP_COMPLETE_REQ:
            era_split_wire_put32(&payload[6], request->source_revision);
            era_split_wire_put32(&payload[10], request->image_crc32);
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_REQ:
            era_split_wire_put32(&payload[6], request->source_revision);
            payload[10] = request->detail;
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ:
            payload[6] = request->detail;
            payload[7] = request->chunk_id;
            if (request->domain != ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE) {
                era_split_wire_put16(&payload[8], (uint16_t)request->image_crc32);
            }
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ:
            payload[6] = request->detail;
            era_split_wire_put32(&payload[7], request->source_revision);
            if (request->detail == (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_ABORT) {
                payload[11] = request->chunk_id;
            } else {
                era_split_wire_put32(&payload[11], request->image_crc32);
            }
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ:
            /* Bulk-lane only: the service builds this frame from the
             * published image, never through the compact encoder. */
            return false;
        default:
            return false;
    }

    *payload_len = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_COMPACT_PAYLOAD_BYTES;
    return era_split_communication_core_storage_compact_payload_valid(payload, *payload_len);
}

bool era_split_communication_core_storage_build_push_chunk_payload(uint8_t control, const era_split_communication_core_storage_initiator_request_t *request, uint8_t *payload, uint16_t *payload_len) {
    /* The initiator-side mirror of the responder's chunk serving: copy the
     * chunk from this half's own published image under the odd/even
     * publication discipline, straight into the bulk payload. A torn or
     * invalidated publication (a local write superseding the push source)
     * refuses the build; core0's episode sees the failure and re-plans. */
    if (!era_split_communication_core_storage_initiator_request_valid(request) || payload == NULL || payload_len == NULL ||
        request->operation != ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ ||
        !era_split_communication_core_storage_control_valid((uint8_t)(control | ERA_SPLIT_WIRE_CONTROL_EXT))) {
        return false;
    }
    volatile const uint32_t *seq_word = (volatile const uint32_t *)(uintptr_t)request->image_publication_seq_address;
    const uint8_t *image = (const uint8_t *)(uintptr_t)request->image_address;
    uint32_t offset = (uint32_t)request->chunk_id * ERA_HOST_PEER_STORAGE_CHUNK_BYTES;
    uint32_t seq_before = *seq_word;
    __DMB();
    if (seq_before == 0 || (seq_before & 1U) != 0) {
        return false;
    }
    payload[0] = (uint8_t)(control | ERA_SPLIT_WIRE_CONTROL_EXT);
    payload[1] = request->operation;
    payload[2] = request->domain;
    payload[3] = request->schema;
    era_split_wire_put16(&payload[4], request->transaction_generation);
    era_split_wire_put32(&payload[6], request->source_revision);
    payload[10] = request->chunk_id;
    payload[11] = request->detail;
    memcpy(&payload[12], &image[offset], request->detail);
    __DMB();
    if (*seq_word != seq_before) {
        return false;
    }
    *payload_len = (uint16_t)(12U + request->detail);
    return era_split_communication_core_storage_bulk_payload_valid(payload, *payload_len);
}

bool era_split_communication_core_storage_decode_request_payload(const uint8_t *payload, uint16_t payload_len, era_split_wire_frame_lane_t lane, era_split_communication_core_storage_initiator_request_t *request) {
    if (request == NULL) {
        return false;
    }
    if (lane == ERA_SPLIT_WIRE_FRAME_LANE_BULK_PAGE) {
        /* The one bulk request: PUSH_CHUNK_REQ metadata. The chunk bytes
         * stay in the frame; the service writes them to the staging image
         * against the snapshot, so no request record carries data. */
        if (!era_split_communication_core_storage_bulk_payload_valid(payload, payload_len) ||
            payload[1] != ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ) {
            return false;
        }
        memset(request, 0, sizeof(*request));
        request->transaction_generation = era_split_wire_get16(&payload[4]);
        request->domain                 = payload[2];
        request->schema                 = payload[3];
        request->operation              = payload[1];
        request->image_size             = era_split_communication_core_storage_domain_size(request->domain);
        request->response_window_ms     = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONSE_WINDOW_MS;
        request->source_revision        = era_split_wire_get32(&payload[6]);
        request->chunk_id               = payload[10];
        request->detail                 = payload[11];
        return true;
    }
    if (lane != ERA_SPLIT_WIRE_FRAME_LANE_COMPACT ||
        !era_split_communication_core_storage_compact_payload_valid(payload, payload_len) ||
        !era_split_communication_core_storage_request_operation_valid(payload[1])) {
        return false;
    }

    memset(request, 0, sizeof(*request));
    request->transaction_generation = era_split_wire_get16(&payload[4]);
    request->domain                 = payload[2];
    request->schema                 = payload[3];
    request->operation              = payload[1];
    request->image_size             = era_split_communication_core_storage_domain_size(request->domain);
    request->response_window_ms     = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONSE_WINDOW_MS;

    switch ((era_split_eeprom_sync_op_t)request->operation) {
        case ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ:
            request->policy_generation = era_split_wire_get16(&payload[6]);
            request->image_size         = era_split_wire_get16(&payload[8]);
            request->image_crc32        = era_split_wire_get32(&payload[10]);
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ:
            request->source_revision = era_split_wire_get32(&payload[6]);
            request->chunk_id        = payload[10];
            request->detail          = payload[11];
            request->image_crc32     = era_split_wire_get24(&payload[12]);
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ:
        case ERA_SPLIT_EEPROM_SYNC_OP_COMPLETE_REQ:
            request->source_revision = era_split_wire_get32(&payload[6]);
            request->image_crc32     = era_split_wire_get32(&payload[10]);
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_REQ:
            request->source_revision = era_split_wire_get32(&payload[6]);
            request->detail          = payload[10];
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ:
            request->detail   = payload[6];
            request->chunk_id = payload[7];
            if (request->domain != ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE) {
                request->image_crc32 = era_split_wire_get16(&payload[8]);
            }
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ:
            request->detail          = payload[6];
            request->source_revision = era_split_wire_get32(&payload[7]);
            if (request->detail == (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_ABORT) {
                request->chunk_id = payload[11];
            } else {
                request->image_crc32 = era_split_wire_get32(&payload[11]);
            }
            break;
        default:
            return false;
    }
    return true;
}

static bool era_split_communication_core_storage_encode_response_payload(uint8_t control, const era_split_communication_core_storage_responder_result_t *result, const uint8_t *data, uint8_t *payload, uint16_t *payload_len, era_split_wire_frame_lane_t *lane) {
    if (result == NULL || payload == NULL || payload_len == NULL || lane == NULL ||
        result->transaction_generation == 0 || !era_split_communication_core_storage_response_operation_valid(result->operation) ||
        !era_split_communication_core_storage_control_valid((uint8_t)(control | ERA_SPLIT_WIRE_CONTROL_EXT))) {
        return false;
    }

    uint16_t clear_bytes = result->operation == ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_RSP ?
                               ERA_SPLIT_WIRE_BULK_PAGE_MAX_PAYLOAD_LEN :
                               ERA_SPLIT_COMMUNICATION_CORE_STORAGE_COMPACT_PAYLOAD_BYTES;
    memset(payload, 0, clear_bytes);
    payload[0] = (uint8_t)(control | ERA_SPLIT_WIRE_CONTROL_EXT);
    payload[1] = result->operation;
    payload[2] = result->domain;
    payload[3] = result->schema;
    era_split_wire_put16(&payload[4], result->transaction_generation);

    switch ((era_split_eeprom_sync_op_t)result->operation) {
        case ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP:
            if (!era_split_communication_core_storage_status_valid(result->status)) {
                return false;
            }
            payload[6] = result->status;
            era_split_wire_put32(&payload[7], result->source_revision);
            era_split_wire_put32(&payload[11], result->image_crc32);
            *payload_len = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_COMPACT_PAYLOAD_BYTES;
            *lane        = ERA_SPLIT_WIRE_FRAME_LANE_COMPACT;
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_RSP:
            /* A zero data length with no data bytes is the content-match
             * acknowledgement for a hinted chunk request. */
            if ((result->data_length > 0 && data == NULL) || result->source_revision == 0 ||
                result->chunk_id >= ERA_HOST_PEER_STORAGE_MAX_CHUNKS ||
                result->data_length > ERA_HOST_PEER_STORAGE_CHUNK_BYTES) {
                return false;
            }
            era_split_wire_put32(&payload[6], result->source_revision);
            payload[10] = result->chunk_id;
            payload[11] = result->data_length;
            if (result->data_length > 0) {
                memcpy(&payload[12], data, result->data_length);
            }
            *payload_len = (uint16_t)(12U + result->data_length);
            *lane        = ERA_SPLIT_WIRE_FRAME_LANE_BULK_PAGE;
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_RSP:
        case ERA_SPLIT_EEPROM_SYNC_OP_CLOSE_RSP:
            if (!era_split_communication_core_storage_status_valid(result->status)) {
                return false;
            }
            payload[6] = result->status;
            era_split_wire_put32(&payload[7], result->source_revision);
            era_split_wire_put32(&payload[11], result->image_crc32);
            *payload_len = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_COMPACT_PAYLOAD_BYTES;
            *lane        = ERA_SPLIT_WIRE_FRAME_LANE_COMPACT;
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP:
            if (!era_split_communication_core_storage_status_valid(result->status)) {
                return false;
            }
            era_split_wire_put32(&payload[6], result->source_revision);
            payload[10] = result->status;
            *payload_len = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_COMPACT_PAYLOAD_BYTES;
            *lane        = ERA_SPLIT_WIRE_FRAME_LANE_COMPACT;
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_RSP:
            /* Carriers: chunk_id = changed mask/flag, status = baseline
             * validity, image_crc32 = the per-conflict counter. The summary
             * form's bytes 8..14 stay zero from the memset above, and bytes
             * 8..11 of that run are the relation time-anchor seat: **its
             * enforced value is zero permanently**, not until some slice arms
             * it. This comment used to say "until Slice 12". Slice 12 landed
             * and opened RGB_STATE instead, Slice 13 gave the anchor the
             * `TIME_ANCHOR` section — one carrier in every relation — and a
             * writer of this seat is now on the permanently-closed list in
             * `era_closed_surface_contract.md`. The validator arm above is
             * the enforcement; this encoder simply never has anything to put
             * there. */
            if (result->status > 1U) {
                return false;
            }
            payload[6] = result->chunk_id;
            payload[7] = result->status;
            if (result->domain != ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE) {
                era_split_wire_put16(&payload[8], (uint16_t)result->image_crc32);
            }
            *payload_len = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_COMPACT_PAYLOAD_BYTES;
            *lane        = ERA_SPLIT_WIRE_FRAME_LANE_COMPACT;
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_RSP:
            if (!era_split_communication_core_storage_status_valid(result->status)) {
                return false;
            }
            payload[6] = result->status;
            era_split_wire_put32(&payload[7], result->source_revision);
            era_split_wire_put32(&payload[11], result->image_crc32);
            *payload_len = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_COMPACT_PAYLOAD_BYTES;
            *lane        = ERA_SPLIT_WIRE_FRAME_LANE_COMPACT;
            break;
        default:
            return false;
    }

    return era_split_communication_core_storage_validate_wire_payload(payload, *payload_len, *lane);
}

/* Forward declaration for the tail call below. It used to come from the
   header, which declared it for a reader outside this unit that never
   existed. */
static bool era_split_communication_core_storage_result_matches_request(const era_split_communication_core_storage_initiator_request_t *request, const era_split_communication_core_storage_initiator_result_t *result);

bool era_split_communication_core_storage_decode_response_payload(const era_split_communication_core_storage_initiator_request_t *request, const uint8_t *payload, uint16_t payload_len, era_split_wire_frame_lane_t lane, uint8_t expected_ack_seq, era_split_communication_core_storage_initiator_result_t *result) {
    if (request == NULL || result == NULL || expected_ack_seq == 0 ||
        !era_split_communication_core_storage_validate_wire_payload(payload, payload_len, lane) ||
        ((payload[0] & ERA_SPLIT_WIRE_CONTROL_ACK_SEQ_MASK) >> 3) != expected_ack_seq) {
        return false;
    }
    uint8_t response_status = payload[1] == ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP ? payload[10] : 0;
    if (!era_split_communication_core_storage_response_matches_request(request->operation, payload[1], response_status)) {
        return false;
    }

    memset(result, 0, sizeof(*result));
    result->owner_epoch            = request->owner_epoch;
    result->relation_generation    = request->relation_generation;
    result->request_generation     = request->request_generation;
    result->policy_generation      = request->policy_generation;
    result->transaction_generation = era_split_wire_get16(&payload[4]);
    result->domain                 = payload[2];
    result->schema                 = payload[3];
    result->operation              = payload[1];
    result->chunk_id               = request->chunk_id;
    result->source_revision        = request->source_revision;
    result->image_crc32            = request->image_crc32;
    result->tx_seq                 = payload[0] & ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK;
    result->result                 = ERA_SPLIT_TRANSACTION_RESULT_OK;

    switch ((era_split_eeprom_sync_op_t)result->operation) {
        case ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP:
            result->status          = payload[6];
            result->source_revision = era_split_wire_get32(&payload[7]);
            result->image_crc32     = era_split_wire_get32(&payload[11]);
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_RSP:
            result->source_revision = era_split_wire_get32(&payload[6]);
            result->chunk_id        = payload[10];
            result->data_length     = payload[11];
            memcpy(result->data, &payload[12], result->data_length);
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_RSP:
        case ERA_SPLIT_EEPROM_SYNC_OP_CLOSE_RSP:
            result->status          = payload[6];
            result->source_revision = era_split_wire_get32(&payload[7]);
            result->image_crc32     = era_split_wire_get32(&payload[11]);
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP:
            result->source_revision = era_split_wire_get32(&payload[6]);
            result->status          = payload[10];
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_RSP:
            /* Body bytes 6..14 travel to core0 verbatim — changed
             * mask/flag, baseline validity, then the counter or the zero
             * anchor seat — and core0 parses them by the request's form. */
            result->data_length = 9;
            memcpy(result->data, &payload[6], 9);
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_RSP:
            result->status          = payload[6];
            result->source_revision = era_split_wire_get32(&payload[7]);
            result->image_crc32     = era_split_wire_get32(&payload[11]);
            break;
        default:
            return false;
    }

    return era_split_communication_core_storage_result_matches_request(request, result);
}

static bool era_split_communication_core_storage_result_matches_request(const era_split_communication_core_storage_initiator_request_t *request, const era_split_communication_core_storage_initiator_result_t *result) {
    if (request == NULL || result == NULL || result->result != ERA_SPLIT_TRANSACTION_RESULT_OK || result->failure != ERA_SPLIT_TRANSACTION_FAILURE_NONE ||
        result->owner_epoch != request->owner_epoch || result->relation_generation != request->relation_generation ||
        result->request_generation != request->request_generation || result->policy_generation != request->policy_generation ||
        result->transaction_generation != request->transaction_generation || result->domain != request->domain || result->schema != request->schema ||
        !era_split_communication_core_storage_response_matches_request(request->operation, result->operation, result->status)) {
        return false;
    }

    switch ((era_split_eeprom_sync_op_t)result->operation) {
        case ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP:
            if (!era_split_communication_core_storage_status_valid(result->status)) {
                return false;
            }
            if (result->status == ERA_SPLIT_EEPROM_SYNC_STATUS_MATCH) {
                return result->source_revision != 0 && result->image_crc32 == request->image_crc32;
            }
            return result->status != ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER || result->source_revision != 0;
        case ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_RSP:
            /* A zero data length is the content-match acknowledgement for a
             * hinted request; core0 validates the hint precondition. */
            return result->source_revision == request->source_revision && result->image_crc32 == request->image_crc32 &&
                   result->chunk_id == request->chunk_id &&
                   (result->data_length == request->detail || result->data_length == 0);
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_RSP:
        case ERA_SPLIT_EEPROM_SYNC_OP_CLOSE_RSP:
            return era_split_communication_core_storage_status_valid(result->status) &&
                   result->source_revision == request->source_revision && result->image_crc32 == request->image_crc32;
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP:
            return era_split_communication_core_storage_status_valid(result->status) && result->source_revision == request->source_revision &&
                   (request->operation != ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ || result->status == ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED);
        case ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_RSP:
            return result->data_length == 9;
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_RSP:
            /* The push source revision must echo; the CRC field echoes the
             * episode's full-image CRC, which a chunk request does not
             * carry, so it is informational here. */
            return era_split_communication_core_storage_status_valid(result->status) &&
                   result->source_revision == request->source_revision;
        default:
            return false;
    }
}

static uint32_t era_split_communication_core_storage_request_fingerprint(const era_split_communication_core_storage_initiator_request_t *request) {
    if (request == NULL) {
        return 0;
    }

    uint32_t hash = 2166136261UL;
#define ERA_STORAGE_FINGERPRINT_BYTE(value) \
    do {                                     \
        hash ^= (uint8_t)(value);            \
        hash *= 16777619UL;                  \
    } while (0)
    for (uint8_t shift = 0; shift < 32; shift += 8) {
        ERA_STORAGE_FINGERPRINT_BYTE(request->source_revision >> shift);
        ERA_STORAGE_FINGERPRINT_BYTE(request->image_crc32 >> shift);
    }
    ERA_STORAGE_FINGERPRINT_BYTE(request->transaction_generation);
    ERA_STORAGE_FINGERPRINT_BYTE(request->transaction_generation >> 8);
    ERA_STORAGE_FINGERPRINT_BYTE(request->policy_generation);
    ERA_STORAGE_FINGERPRINT_BYTE(request->policy_generation >> 8);
    ERA_STORAGE_FINGERPRINT_BYTE(request->image_size);
    ERA_STORAGE_FINGERPRINT_BYTE(request->image_size >> 8);
    ERA_STORAGE_FINGERPRINT_BYTE(request->domain);
    ERA_STORAGE_FINGERPRINT_BYTE(request->schema);
    ERA_STORAGE_FINGERPRINT_BYTE(request->operation);
    ERA_STORAGE_FINGERPRINT_BYTE(request->chunk_id);
    ERA_STORAGE_FINGERPRINT_BYTE(request->detail);
#undef ERA_STORAGE_FINGERPRINT_BYTE
    return hash != 0 ? hash : 1U;
}

static bool era_split_communication_core_storage_publication_begin(volatile uint32_t *publication_seq, uint32_t *odd_seq) {
    if (publication_seq == NULL || odd_seq == NULL || *publication_seq >= UINT32_MAX - 1U) {
        return false;
    }
    uint32_t next = *publication_seq + 1U;
    if ((next & 1U) == 0) {
        next++;
    }
    *publication_seq = next;
    __DMB();
    *odd_seq = next;
    return true;
}

static void era_split_communication_core_storage_publication_finish(volatile uint32_t *publication_seq, uint32_t odd_seq) {
    __DMB();
    *publication_seq = odd_seq + 1U;
    __DMB();
    __SEV();
}

static void era_split_communication_core_storage_result_publish_ready(volatile uint32_t *ready_generation, uint16_t generation) {
    __DMB();
    *ready_generation = generation;
    __DMB();
    __SEV();
}

void era_split_communication_core_storage_capacity_init(void) {
    bool publications_retired =
        g_era_split_communication_core_storage_initiator_request.publication_seq ==
            ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED ||
        g_era_split_communication_core_storage_responder_snapshot.publication_seq ==
            ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED;
    memset(&g_era_split_communication_core_storage_initiator_request, 0, sizeof(g_era_split_communication_core_storage_initiator_request));
    memset(&g_era_split_communication_core_storage_initiator_result, 0, sizeof(g_era_split_communication_core_storage_initiator_result));
    memset(&g_era_split_communication_core_storage_responder_snapshot, 0, sizeof(g_era_split_communication_core_storage_responder_snapshot));
    memset(&g_era_split_communication_core_storage_responder_result, 0, sizeof(g_era_split_communication_core_storage_responder_result));
    if (publications_retired) {
        /* Owner-role rebuilds reuse this capacity initializer. CLEAN
         * quarantine is monotonic for the boot, so a rebuild clears stale
         * records/results but must not turn either source publication back
         * into an admissible slot. Either sentinel is fail-closed evidence for
         * the pair; normal sequence allocation can never produce UINT32_MAX. */
        g_era_split_communication_core_storage_initiator_request.publication_seq =
            ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED;
        g_era_split_communication_core_storage_responder_snapshot.publication_seq =
            ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED;
    }
    memset(g_era_split_communication_core_storage_wire_frame, 0, sizeof(g_era_split_communication_core_storage_wire_frame));
    memset(g_era_split_communication_core_storage_decoded_frame, 0, sizeof(g_era_split_communication_core_storage_decoded_frame));
    memset(g_era_split_communication_core_storage_chunk_scratch, 0, sizeof(g_era_split_communication_core_storage_chunk_scratch));
    memset(&g_era_split_communication_core_storage_decoded_semantic_frame, 0, sizeof(g_era_split_communication_core_storage_decoded_semantic_frame));
    __DMB();
}

bool era_split_communication_core_storage_retire_publications(void) {
    volatile uint32_t *initiator_publication_seq = &g_era_split_communication_core_storage_initiator_request.publication_seq;
    volatile uint32_t *responder_publication_seq = &g_era_split_communication_core_storage_responder_snapshot.publication_seq;
    uint32_t initiator_seq = *initiator_publication_seq;
    uint32_t responder_seq = *responder_publication_seq;

    /* Core0 is the sole publisher of both records, so an ordinary odd value
     * means this call did not arrive at a publication boundary. A nonzero
     * claim is core1's service ownership and must drain before retirement. */
    uint32_t initiator_result_seq = g_era_split_communication_core_storage_initiator_result.publication_seq;
    uint32_t responder_result_seq = g_era_split_communication_core_storage_responder_result.publication_seq;
    if ((initiator_seq != ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED && (initiator_seq & 1U) != 0) ||
        (responder_seq != ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED && (responder_seq & 1U) != 0) ||
        (initiator_result_seq & 1U) != 0 || (responder_result_seq & 1U) != 0 ||
        g_era_split_communication_core_storage_initiator_request.claim_generation != 0 ||
        g_era_split_communication_core_storage_responder_snapshot.claim_generation != 0) {
        return false;
    }

    bool initiator_changed = initiator_seq != ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED;
    bool responder_changed = responder_seq != ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED;
    if (initiator_changed) {
        *initiator_publication_seq = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED;
    }
    __DMB();
    if (responder_changed) {
        *responder_publication_seq = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED;
    }
    __DMB();

    /* A reader may have copied an even publication just before the terminal
     * store and raised its claim afterwards. Its final sequence check will
     * reject the claim, but until it does this call must report busy. Restore
     * the unchanged records' prior even sequences; observing either the
     * terminal value or the restored value is safe because no record bytes
     * changed and retirement has not been reported complete. Each source
     * claim remains held until its result record is even and ready is
     * published, so after both claims reach zero an even result is discardable
     * only when its ready marker is either zero or agrees with its immutable
     * record and reservation. */
    uint32_t checked_initiator_result_seq = g_era_split_communication_core_storage_initiator_result.publication_seq;
    uint32_t checked_responder_result_seq = g_era_split_communication_core_storage_responder_result.publication_seq;
    __DMB();
    uint32_t initiator_result_claim       = g_era_split_communication_core_storage_initiator_result.claim_generation;
    uint32_t initiator_result_ready       = g_era_split_communication_core_storage_initiator_result.ready_generation;
    uint32_t responder_result_claim       = g_era_split_communication_core_storage_responder_result.claim_generation;
    uint32_t responder_result_ready       = g_era_split_communication_core_storage_responder_result.ready_generation;
    uint16_t initiator_record_generation  = g_era_split_communication_core_storage_initiator_result.record.request_generation;
    uint16_t responder_record_generation  = g_era_split_communication_core_storage_responder_result.record.snapshot_generation;
    __DMB();
    uint32_t final_initiator_result_seq = g_era_split_communication_core_storage_initiator_result.publication_seq;
    uint32_t final_responder_result_seq = g_era_split_communication_core_storage_responder_result.publication_seq;
    bool initiator_ready_valid =
        initiator_result_ready == 0 ||
        (checked_initiator_result_seq != 0 && initiator_result_claim == initiator_result_ready &&
         initiator_record_generation == (uint16_t)initiator_result_ready);
    bool responder_ready_valid =
        responder_result_ready == 0 ||
        (checked_responder_result_seq != 0 && responder_result_claim == responder_result_ready &&
         responder_record_generation == (uint16_t)responder_result_ready);

    if (*initiator_publication_seq != ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED ||
        *responder_publication_seq != ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED ||
        g_era_split_communication_core_storage_initiator_request.claim_generation != 0 ||
        g_era_split_communication_core_storage_responder_snapshot.claim_generation != 0 ||
        checked_initiator_result_seq != initiator_result_seq || final_initiator_result_seq != checked_initiator_result_seq ||
        (checked_initiator_result_seq & 1U) != 0 ||
        checked_responder_result_seq != responder_result_seq || final_responder_result_seq != checked_responder_result_seq ||
        (checked_responder_result_seq & 1U) != 0 ||
        (initiator_result_claim == 0 && initiator_result_ready != 0) ||
        (responder_result_claim == 0 && responder_result_ready != 0) ||
        !initiator_ready_valid || !responder_ready_valid) {
        if (responder_changed) {
            *responder_publication_seq = responder_seq;
        }
        __DMB();
        if (initiator_changed) {
            *initiator_publication_seq = initiator_seq;
        }
        __DMB();
        __SEV();
        return false;
    }

    /* With both source publications held at an odd terminal sequence, no new
     * core1 claim can succeed. The remaining nonzero result markers therefore
     * name either a core0 reservation that never started or an immutable ready
     * result that the closing runtime deliberately discards. Clear claim
     * before ready, matching the ordinary core0 release discipline. */
    g_era_split_communication_core_storage_initiator_result.claim_generation = 0;
    __DMB();
    g_era_split_communication_core_storage_initiator_result.ready_generation = 0;
    __DMB();
    g_era_split_communication_core_storage_responder_result.claim_generation = 0;
    __DMB();
    g_era_split_communication_core_storage_responder_result.ready_generation = 0;
    __DMB();
    __SEV();
    return true;
}

void era_split_communication_core_storage_note_responder_full(void) {
    g_era_split_communication_core_storage_lane_diagnostics.responder_full_count++;
    __DMB();
}

void era_split_communication_core_storage_note_replay(void) {
    g_era_split_communication_core_storage_lane_diagnostics.replay_count++;
    __DMB();
}

void era_split_communication_core_storage_get_lane_diagnostics(era_split_communication_core_storage_lane_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }
    __DMB();
    *snapshot = g_era_split_communication_core_storage_lane_diagnostics;
    __DMB();
}

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
void era_split_communication_core_storage_note_initiator_probe(era_split_communication_core_storage_probe_stage_t stage,
                                                               bool success,
                                                               const era_split_communication_core_storage_initiator_result_t *result) {
    g_era_split_communication_core_storage_probe_diagnostics.last_stage = (uint8_t)stage;
    if (result != NULL) {
        g_era_split_communication_core_storage_probe_diagnostics.last_result    = result->result;
        g_era_split_communication_core_storage_probe_diagnostics.last_failure   = result->failure;
        g_era_split_communication_core_storage_probe_diagnostics.last_operation = result->operation;
        g_era_split_communication_core_storage_probe_diagnostics.last_status    = result->status;
    }
    if (!success) {
        g_era_split_communication_core_storage_probe_diagnostics.failure_count++;
    }
    switch (stage) {
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_CLAIM:
            if (success) {
                g_era_split_communication_core_storage_probe_diagnostics.claim_count++;
            }
            break;
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_TX:
            if (success) {
                g_era_split_communication_core_storage_probe_diagnostics.tx_count++;
            }
            break;
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_RX:
            if (success) {
                g_era_split_communication_core_storage_probe_diagnostics.rx_count++;
            }
            break;
        case ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_PUBLISH:
            if (success) {
                g_era_split_communication_core_storage_probe_diagnostics.publish_count++;
            }
            break;
        default:
            break;
    }
    __DMB();
}

void era_split_communication_core_storage_note_initiator_failure(era_split_communication_core_storage_probe_stage_t stage,
                                                                 const era_split_communication_core_storage_initiator_result_t *result,
                                                                 uint8_t request_operation,
                                                                 uint8_t classification,
                                                                 uint8_t access,
                                                                 int32_t deadline_delta_us,
                                                                 const era_split_communication_core_storage_probe_failure_context_t *failure_context) {
    /* Write the retained detail before failure_count becomes the visible
     * commit marker in note_initiator_probe(). A print racing this tiny window
     * may see new detail with the old count, never a new count with stale
     * detail; the next paced line is coherent in either case. */
    g_era_split_communication_core_storage_probe_diagnostics.failure_stage             = (uint8_t)stage;
    g_era_split_communication_core_storage_probe_diagnostics.failure_result            = result != NULL ? result->result : 0;
    g_era_split_communication_core_storage_probe_diagnostics.failure_failure           = result != NULL ? result->failure : 0;
    g_era_split_communication_core_storage_probe_diagnostics.failure_operation         = request_operation;
    g_era_split_communication_core_storage_probe_diagnostics.failure_status            = result != NULL ? result->status : 0;
    g_era_split_communication_core_storage_probe_diagnostics.failure_classification    = classification;
    g_era_split_communication_core_storage_probe_diagnostics.failure_access            = access;
    g_era_split_communication_core_storage_probe_diagnostics.failure_deadline_delta_us = deadline_delta_us;
    if (failure_context != NULL) {
        g_era_split_communication_core_storage_probe_diagnostics.failure_context = *failure_context;
    } else {
        memset(&g_era_split_communication_core_storage_probe_diagnostics.failure_context, 0,
               sizeof(g_era_split_communication_core_storage_probe_diagnostics.failure_context));
        g_era_split_communication_core_storage_probe_diagnostics.failure_context.queue_delay_us                 = UINT32_MAX;
        g_era_split_communication_core_storage_probe_diagnostics.failure_context.queue_window_us                = INT32_MIN;
        g_era_split_communication_core_storage_probe_diagnostics.failure_context.prior_route_start_delta_us     = INT32_MIN;
        g_era_split_communication_core_storage_probe_diagnostics.failure_context.prior_route_end_delta_us       = INT32_MIN;
        g_era_split_communication_core_storage_probe_diagnostics.failure_context.prior_route_to_failure_delta_us = INT32_MIN;
    }
    __DMB();
    era_split_communication_core_storage_note_initiator_probe(stage, false, result);
}

void era_split_communication_core_storage_get_probe_diagnostics(era_split_communication_core_storage_probe_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }
    __DMB();
    *snapshot = g_era_split_communication_core_storage_probe_diagnostics;
    snapshot->request_claim_generation = (uint16_t)g_era_split_communication_core_storage_initiator_request.claim_generation;
    snapshot->result_claim_generation  = (uint16_t)g_era_split_communication_core_storage_initiator_result.claim_generation;
    snapshot->result_ready_generation  = (uint16_t)g_era_split_communication_core_storage_initiator_result.ready_generation;
    __DMB();
}
#endif

uint8_t *era_split_communication_core_storage_wire_frame_scratch(void) {
    return g_era_split_communication_core_storage_wire_frame;
}

uint8_t *era_split_communication_core_storage_decoded_frame_scratch(void) {
    return g_era_split_communication_core_storage_decoded_frame;
}

era_split_wire_frame_t *era_split_communication_core_storage_decoded_semantic_frame_scratch(void) {
    return &g_era_split_communication_core_storage_decoded_semantic_frame;
}

bool era_split_communication_core_storage_reserve_initiator_result(uint16_t request_generation) {
    if (request_generation == 0 ||
        g_era_split_communication_core_storage_initiator_request.publication_seq == ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED ||
        g_era_split_communication_core_storage_initiator_result.claim_generation != 0 ||
        g_era_split_communication_core_storage_initiator_result.ready_generation != 0) {
        return false;
    }
    g_era_split_communication_core_storage_initiator_result.claim_generation = request_generation;
    __DMB();
    return true;
}

bool era_split_communication_core_storage_publish_initiator_request(const era_split_communication_core_storage_initiator_request_t *request, uint32_t queue_window_us) {
    if (queue_window_us == 0 || !era_split_communication_core_storage_initiator_request_valid(request) ||
        g_era_split_communication_core_storage_initiator_request.claim_generation != 0 ||
        g_era_split_communication_core_storage_initiator_result.claim_generation != request->request_generation) {
        return false;
    }

    uint32_t odd_seq;
    if (!era_split_communication_core_storage_publication_begin(&g_era_split_communication_core_storage_initiator_request.publication_seq, &odd_seq)) {
        return false;
    }
    era_split_communication_core_storage_initiator_request_t published = *request;
    uint32_t published_at_us = timer_hw->timerawl;
    published.not_after_us   = published_at_us + queue_window_us;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    published.published_at_us = published_at_us;
#endif
    g_era_split_communication_core_storage_initiator_request.record = published;
    era_split_communication_core_storage_publication_finish(&g_era_split_communication_core_storage_initiator_request.publication_seq, odd_seq);
    return true;
}

bool era_split_communication_core_storage_claim_initiator_request(era_split_communication_core_storage_initiator_request_t *request) {
    if (request == NULL || g_era_split_communication_core_storage_initiator_request.claim_generation != 0 ||
        g_era_split_communication_core_storage_initiator_result.ready_generation != 0) {
        return false;
    }

    for (uint8_t retry = 0; retry < ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_READ_RETRIES; retry++) {
        uint32_t first_seq = g_era_split_communication_core_storage_initiator_request.publication_seq;
        if (first_seq == 0 || (first_seq & 1U) != 0) {
            continue;
        }
        __DMB();
        *request = g_era_split_communication_core_storage_initiator_request.record;
        __DMB();
        uint32_t second_seq = g_era_split_communication_core_storage_initiator_request.publication_seq;
        if (first_seq != second_seq || (second_seq & 1U) != 0 || request->request_generation == 0 ||
            g_era_split_communication_core_storage_initiator_result.ready_generation != 0 ||
            g_era_split_communication_core_storage_initiator_result.claim_generation != request->request_generation) {
            continue;
        }
        g_era_split_communication_core_storage_initiator_request.claim_generation = request->request_generation;
        __DMB();
        if (second_seq == g_era_split_communication_core_storage_initiator_request.publication_seq) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
            era_split_communication_core_storage_note_initiator_probe(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_CLAIM, true, NULL);
#endif
            return true;
        }
        g_era_split_communication_core_storage_initiator_request.claim_generation = 0;
        __DMB();
    }
    return false;
}

void era_split_communication_core_storage_release_initiator_request(uint16_t request_generation) {
    if (request_generation != 0 && g_era_split_communication_core_storage_initiator_request.claim_generation == request_generation) {
        g_era_split_communication_core_storage_initiator_request.claim_generation = 0;
        __DMB();
        __SEV();
    }
}

era_split_communication_core_storage_initiator_result_t *era_split_communication_core_storage_begin_initiator_result(uint16_t request_generation) {
    if (request_generation == 0 ||
        g_era_split_communication_core_storage_initiator_request.claim_generation != request_generation ||
        g_era_split_communication_core_storage_initiator_result.claim_generation != request_generation ||
        g_era_split_communication_core_storage_initiator_result.ready_generation != 0) {
        return NULL;
    }
    // Ready remains zero while Core1 fills the reserved record in place.
    return &g_era_split_communication_core_storage_initiator_result.record;
}

bool era_split_communication_core_storage_publish_initiator_result(const era_split_communication_core_storage_initiator_result_t *result) {
    if (result == NULL || result->request_generation == 0 ||
        g_era_split_communication_core_storage_initiator_request.claim_generation != result->request_generation ||
        g_era_split_communication_core_storage_initiator_result.claim_generation != result->request_generation) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
        era_split_communication_core_storage_note_initiator_failure(
            ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_PUBLISH,
            result,
            result != NULL ? result->operation : 0,
            ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_DETAIL_NONE,
            ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_DETAIL_NONE,
            INT32_MIN,
            NULL);
#endif
        return false;
    }

    uint32_t odd_seq;
    if (!era_split_communication_core_storage_publication_begin(&g_era_split_communication_core_storage_initiator_result.publication_seq, &odd_seq)) {
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
        era_split_communication_core_storage_note_initiator_failure(
            ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_PUBLISH,
            result,
            result->operation,
            ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_DETAIL_NONE,
            ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_DETAIL_NONE,
            INT32_MIN,
            NULL);
#endif
        return false;
    }
    if (result != &g_era_split_communication_core_storage_initiator_result.record) {
        g_era_split_communication_core_storage_initiator_result.record = *result;
    }
    era_split_communication_core_storage_publication_finish(&g_era_split_communication_core_storage_initiator_result.publication_seq, odd_seq);
    era_split_communication_core_storage_result_publish_ready(&g_era_split_communication_core_storage_initiator_result.ready_generation, result->request_generation);
    era_split_communication_core_storage_release_initiator_request(result->request_generation);
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    era_split_communication_core_storage_note_initiator_probe(ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PROBE_STAGE_PUBLISH, true, result);
#endif
    return true;
}

bool era_split_communication_core_storage_acquire_initiator_result(const era_split_communication_core_storage_initiator_result_t **result) {
    if (result == NULL) {
        return false;
    }
    *result = NULL;
    if (g_era_split_communication_core_storage_initiator_result.claim_generation == 0) {
        return false;
    }
    for (uint8_t retry = 0; retry < ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_READ_RETRIES; retry++) {
        uint32_t ready_generation = g_era_split_communication_core_storage_initiator_result.ready_generation;
        uint32_t first_seq = g_era_split_communication_core_storage_initiator_result.publication_seq;
        if (ready_generation == 0 || first_seq == 0 || (first_seq & 1U) != 0) {
            continue;
        }
        __DMB();
        const era_split_communication_core_storage_initiator_result_t *candidate =
            &g_era_split_communication_core_storage_initiator_result.record;
        uint16_t request_generation = candidate->request_generation;
        __DMB();
        uint32_t second_seq = g_era_split_communication_core_storage_initiator_result.publication_seq;
        if (first_seq == second_seq && (second_seq & 1U) == 0 && request_generation != 0 &&
            g_era_split_communication_core_storage_initiator_result.claim_generation == request_generation &&
            ready_generation == request_generation &&
            g_era_split_communication_core_storage_initiator_result.ready_generation == request_generation) {
            // Claim and ready keep the record immutable until explicit release.
            *result = candidate;
            return true;
        }
    }
    return false;
}

bool era_split_communication_core_storage_release_initiator_result(uint16_t request_generation) {
    if (request_generation == 0 ||
        g_era_split_communication_core_storage_initiator_result.claim_generation != request_generation ||
        g_era_split_communication_core_storage_initiator_result.ready_generation != request_generation) {
        return false;
    }
    g_era_split_communication_core_storage_initiator_result.claim_generation = 0;
    __DMB();
    g_era_split_communication_core_storage_initiator_result.ready_generation = 0;
    __DMB();
    __SEV();
    return true;
}

bool era_split_communication_core_storage_initiator_result_ready(void) {
    uint32_t ready_generation = g_era_split_communication_core_storage_initiator_result.ready_generation;
    if (ready_generation == 0) {
        return false;
    }
    __DMB();
    uint32_t publication_seq  = g_era_split_communication_core_storage_initiator_result.publication_seq;
    uint32_t claim_generation = g_era_split_communication_core_storage_initiator_result.claim_generation;
    return claim_generation == ready_generation && publication_seq != 0 && (publication_seq & 1U) == 0 &&
           g_era_split_communication_core_storage_initiator_result.record.request_generation == claim_generation;
}

bool era_split_communication_core_storage_result_due(void) {
    return g_era_split_communication_core_storage_initiator_result.ready_generation != 0 ||
           g_era_split_communication_core_storage_responder_result.ready_generation != 0;
}

void era_split_communication_core_storage_cancel_initiator_result(uint16_t request_generation) {
    if (request_generation == 0 ||
        g_era_split_communication_core_storage_initiator_result.claim_generation != request_generation) {
        return;
    }
    g_era_split_communication_core_storage_initiator_request.claim_generation = 0;
    g_era_split_communication_core_storage_initiator_result.claim_generation  = 0;
    __DMB();
    g_era_split_communication_core_storage_initiator_result.ready_generation = 0;
    __DMB();
    __SEV();
}

era_split_communication_core_storage_request_classification_t era_split_communication_core_storage_classify_request(const era_split_communication_core_storage_initiator_request_t *request, const era_split_communication_core_storage_execution_context_t *context) {
    if (!era_split_communication_core_storage_initiator_request_valid(request) || context == NULL) {
        return ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_INVALID;
    }
    if (context->reset_requested) {
        return ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_RESET;
    }
    if (context->cancel_requested) {
        return ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_CANCELLED;
    }
    if (request->owner_epoch != context->owner_epoch) {
        return ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_OWNER_STALE;
    }
    if (request->relation_generation != context->relation_generation) {
        return ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_RELATION_STALE;
    }
    if (request->request_generation != context->request_generation) {
        return ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_GENERATION_STALE;
    }
    if (request->policy_generation != context->policy_generation) {
        return ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_POLICY_STALE;
    }
    if (request->transaction_generation != context->transaction_generation) {
        return ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_TRANSACTION_STALE;
    }
    if ((int32_t)(context->now_us - request->not_after_us) >= 0) {
        return ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_QUEUE_EXPIRED;
    }
    if (g_era_split_communication_core_storage_initiator_result.claim_generation != request->request_generation) {
        return ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_RESULT_FULL;
    }
    return ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_READY;
}

static bool era_split_communication_core_storage_responder_snapshot_valid(const era_split_communication_core_storage_responder_snapshot_t *snapshot) {
    if (snapshot == NULL || snapshot->owner_epoch == 0 || snapshot->relation_generation == 0 || snapshot->snapshot_generation == 0 ||
        snapshot->transaction_generation == 0 || snapshot->domain >= ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT ||
        snapshot->schema != ERA_HOST_PEER_STORAGE_SCHEMA_V1 || snapshot->image_size != era_split_communication_core_storage_domain_size(snapshot->domain) ||
        !era_split_communication_core_storage_request_operation_valid(snapshot->expected_operation) ||
        ((snapshot->expected_operation == ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ ||
          snapshot->expected_operation == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ) ?
             snapshot->expected_chunk_id >= ERA_HOST_PEER_STORAGE_MAX_CHUNKS :
             snapshot->expected_chunk_id != 0) ||
        snapshot->push_state > (uint8_t)ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUSH_STATE_DURABLE ||
        (snapshot->recency_changed_mask & 0x80U) != 0 || snapshot->recency_baseline_valid > 1U ||
        snapshot->pinned > 1) {
        return false;
    }
    if (snapshot->valid) {
        return snapshot->source_revision != 0 && snapshot->image_address != 0 && snapshot->image_publication_seq_address != 0 &&
               snapshot->image_publication_seq != 0 && (snapshot->image_publication_seq & 1U) == 0;
    }
    return true;
}

bool era_split_communication_core_storage_publish_responder_snapshot(const era_split_communication_core_storage_responder_snapshot_t *snapshot) {
    if (!era_split_communication_core_storage_responder_snapshot_valid(snapshot) ||
        g_era_split_communication_core_storage_responder_snapshot.claim_generation != 0 ||
        g_era_split_communication_core_storage_responder_result.claim_generation != 0) {
        return false;
    }

    uint32_t odd_seq;
    if (!era_split_communication_core_storage_publication_begin(&g_era_split_communication_core_storage_responder_snapshot.publication_seq, &odd_seq)) {
        return false;
    }
    g_era_split_communication_core_storage_responder_snapshot.record = *snapshot;
    era_split_communication_core_storage_publication_finish(&g_era_split_communication_core_storage_responder_snapshot.publication_seq, odd_seq);
    return true;
}

bool era_split_communication_core_storage_claim_responder_snapshot(era_split_communication_core_storage_responder_snapshot_t *snapshot) {
    if (snapshot == NULL || g_era_split_communication_core_storage_responder_snapshot.claim_generation != 0) {
        return false;
    }
    for (uint8_t retry = 0; retry < ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_READ_RETRIES; retry++) {
        uint32_t first_seq = g_era_split_communication_core_storage_responder_snapshot.publication_seq;
        if (first_seq == 0 || (first_seq & 1U) != 0) {
            continue;
        }
        __DMB();
        *snapshot = g_era_split_communication_core_storage_responder_snapshot.record;
        __DMB();
        uint32_t second_seq = g_era_split_communication_core_storage_responder_snapshot.publication_seq;
        if (first_seq != second_seq || (second_seq & 1U) != 0 || !era_split_communication_core_storage_responder_snapshot_valid(snapshot)) {
            continue;
        }
        g_era_split_communication_core_storage_responder_snapshot.claim_generation = snapshot->snapshot_generation;
        __DMB();
        if (second_seq == g_era_split_communication_core_storage_responder_snapshot.publication_seq) {
            return true;
        }
        g_era_split_communication_core_storage_responder_snapshot.claim_generation = 0;
        __DMB();
    }
    return false;
}

void era_split_communication_core_storage_release_responder_snapshot(uint16_t snapshot_generation) {
    if (snapshot_generation != 0 && g_era_split_communication_core_storage_responder_snapshot.claim_generation == snapshot_generation) {
        g_era_split_communication_core_storage_responder_snapshot.claim_generation = 0;
        __DMB();
        __SEV();
    }
}

bool era_split_communication_core_storage_reserve_responder_result(uint16_t snapshot_generation) {
    if (snapshot_generation == 0 ||
        g_era_split_communication_core_storage_responder_snapshot.claim_generation != snapshot_generation ||
        g_era_split_communication_core_storage_responder_result.claim_generation != 0 ||
        g_era_split_communication_core_storage_responder_result.ready_generation != 0) {
        return false;
    }
    g_era_split_communication_core_storage_responder_result.claim_generation = snapshot_generation;
    __DMB();
    return true;
}

bool era_split_communication_core_storage_publish_responder_result(const era_split_communication_core_storage_responder_result_t *result) {
    if (result == NULL || result->snapshot_generation == 0 || result->request_fingerprint == 0 ||
        g_era_split_communication_core_storage_responder_snapshot.claim_generation != result->snapshot_generation ||
        g_era_split_communication_core_storage_responder_result.claim_generation != result->snapshot_generation) {
        return false;
    }

    uint32_t odd_seq;
    if (!era_split_communication_core_storage_publication_begin(&g_era_split_communication_core_storage_responder_result.publication_seq, &odd_seq)) {
        return false;
    }
    g_era_split_communication_core_storage_responder_result.record = *result;
    era_split_communication_core_storage_publication_finish(&g_era_split_communication_core_storage_responder_result.publication_seq, odd_seq);
    era_split_communication_core_storage_result_publish_ready(&g_era_split_communication_core_storage_responder_result.ready_generation, result->snapshot_generation);
    era_split_communication_core_storage_release_responder_snapshot(result->snapshot_generation);
    return true;
}

bool era_split_communication_core_storage_drain_responder_result(era_split_communication_core_storage_responder_result_t *result) {
    if (result == NULL || g_era_split_communication_core_storage_responder_result.claim_generation == 0) {
        return false;
    }
    for (uint8_t retry = 0; retry < ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_READ_RETRIES; retry++) {
        uint32_t first_seq = g_era_split_communication_core_storage_responder_result.publication_seq;
        if (first_seq == 0 || (first_seq & 1U) != 0) {
            continue;
        }
        __DMB();
        *result = g_era_split_communication_core_storage_responder_result.record;
        __DMB();
        uint32_t second_seq = g_era_split_communication_core_storage_responder_result.publication_seq;
        if (first_seq == second_seq && (second_seq & 1U) == 0 && result->snapshot_generation != 0 &&
            g_era_split_communication_core_storage_responder_result.claim_generation == result->snapshot_generation &&
            g_era_split_communication_core_storage_responder_result.ready_generation == result->snapshot_generation) {
            g_era_split_communication_core_storage_responder_result.claim_generation = 0;
            __DMB();
            g_era_split_communication_core_storage_responder_result.ready_generation = 0;
            __DMB();
            return true;
        }
    }
    return false;
}

bool era_split_communication_core_storage_responder_result_ready(void) {
    uint32_t ready_generation = g_era_split_communication_core_storage_responder_result.ready_generation;
    if (ready_generation == 0) {
        return false;
    }
    __DMB();
    uint32_t publication_seq  = g_era_split_communication_core_storage_responder_result.publication_seq;
    uint32_t claim_generation = g_era_split_communication_core_storage_responder_result.claim_generation;
    return claim_generation == ready_generation && publication_seq != 0 && (publication_seq & 1U) == 0 &&
           g_era_split_communication_core_storage_responder_result.record.snapshot_generation == claim_generation;
}

void era_split_communication_core_storage_cancel_responder_result(uint16_t snapshot_generation) {
    if (snapshot_generation == 0 ||
        g_era_split_communication_core_storage_responder_result.claim_generation != snapshot_generation) {
        return;
    }
    g_era_split_communication_core_storage_responder_result.claim_generation = 0;
    __DMB();
    g_era_split_communication_core_storage_responder_result.ready_generation = 0;
    __DMB();
    era_split_communication_core_storage_release_responder_snapshot(snapshot_generation);
}

bool era_split_communication_core_storage_copy_previous_responder_result(era_split_communication_core_storage_responder_result_t *result) {
    if (result == NULL) {
        return false;
    }
    for (uint8_t retry = 0; retry < ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_READ_RETRIES; retry++) {
        uint32_t first_seq = g_era_split_communication_core_storage_responder_result.publication_seq;
        if (first_seq == 0 || (first_seq & 1U) != 0) {
            continue;
        }
        __DMB();
        *result = g_era_split_communication_core_storage_responder_result.record;
        __DMB();
        uint32_t second_seq = g_era_split_communication_core_storage_responder_result.publication_seq;
        if (first_seq == second_seq && (second_seq & 1U) == 0 && result->request_fingerprint != 0) {
            return true;
        }
    }
    return false;
}

static bool era_split_communication_core_storage_snapshot_image_current(const era_split_communication_core_storage_responder_snapshot_t *snapshot) {
    if (snapshot == NULL || !snapshot->valid || snapshot->image_publication_seq_address == 0 || snapshot->image_publication_seq == 0 ||
        (snapshot->image_publication_seq & 1U) != 0) {
        return false;
    }
    volatile const uint32_t *publication_seq = (volatile const uint32_t *)(uintptr_t)snapshot->image_publication_seq_address;
    __DMB();
    return *publication_seq == snapshot->image_publication_seq;
}

static bool era_split_communication_core_storage_replay_source_current(const era_split_communication_core_storage_responder_snapshot_t *snapshot, const era_split_communication_core_storage_responder_result_t *previous) {
    bool source_required = previous->operation == ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_RSP ||
                           (previous->operation == ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP &&
                            (previous->status == ERA_SPLIT_EEPROM_SYNC_STATUS_MATCH ||
                             previous->status == ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER)) ||
                           (previous->operation == ERA_SPLIT_EEPROM_SYNC_OP_APPLY_RSP &&
                            previous->status == ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY);
    return !source_required ||
           (snapshot->valid && previous->source_revision == snapshot->source_revision &&
            previous->image_crc32 == snapshot->image_crc32 && previous->image_size == snapshot->image_size &&
            era_split_communication_core_storage_snapshot_image_current(snapshot));
}

bool era_split_communication_core_storage_plan_responder(const era_split_communication_core_storage_responder_snapshot_t *snapshot, const era_split_communication_core_storage_initiator_request_t *request, const era_split_communication_core_storage_responder_result_t *previous, era_split_communication_core_storage_responder_result_t *result) {
    if (!era_split_communication_core_storage_responder_snapshot_valid(snapshot) || request == NULL || result == NULL ||
        !era_split_communication_core_storage_request_operation_valid(request->operation) || request->transaction_generation == 0) {
        return false;
    }

    uint32_t fingerprint = era_split_communication_core_storage_request_fingerprint(request);
    if (fingerprint == 0) {
        return false;
    }

    if (previous != NULL && previous->request_fingerprint == fingerprint &&
        previous->owner_epoch == snapshot->owner_epoch && previous->relation_generation == snapshot->relation_generation &&
        previous->policy_generation == snapshot->policy_generation &&
        previous->transaction_generation == request->transaction_generation && previous->domain == request->domain &&
        previous->schema == request->schema &&
        era_split_communication_core_storage_response_matches_request(request->operation, previous->operation, previous->status) &&
        previous->request_source_revision == request->source_revision && previous->request_image_crc32 == request->image_crc32 &&
        previous->request_policy_generation == request->policy_generation && previous->request_image_size == request->image_size &&
        previous->chunk_id == request->chunk_id &&
        (request->operation != ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ || previous->data_length == request->detail ||
         previous->data_length == 0) &&
        !(previous->operation == ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP && previous->status == ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY &&
          snapshot->pinned && request->transaction_generation == snapshot->transaction_generation && request->domain == snapshot->domain) &&
        !(previous->operation == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_RSP && previous->status == ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY &&
          snapshot->pinned && request->transaction_generation == snapshot->transaction_generation && request->domain == snapshot->domain) &&
        /* The push complete phase is a poll, not an idempotent request: its
         * whole purpose is to observe core0 crossing into DURABLE, and two
         * consecutive polls are byte-identical, so they share a fingerprint.
         * Replaying a provisional APPLY_READY therefore latches it forever
         * and the answer can never flip - measured 2026-07-28 as ~195
         * duplicate replays over the full 5 s episode deadline, a red
         * indicator held for that whole window, and an abort on a domain
         * whose content had in fact already landed. A previous COMPLETE
         * still replays, because that one is the lost-response repair the
         * contract requires. */
        !(previous->operation == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_RSP &&
          previous->status == ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY &&
          request->operation == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ &&
          request->detail == (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_COMPLETE) &&
        era_split_communication_core_storage_replay_source_current(snapshot, previous)) {
        *result                  = *previous;
        result->snapshot_generation = snapshot->snapshot_generation;
        result->response_sent   = 0;
        result->result          = ERA_SPLIT_TRANSACTION_RESULT_NONE;
        result->failure         = ERA_SPLIT_TRANSACTION_FAILURE_NONE;
        result->replayed        = 1;
        return true;
    }

    memset(result, 0, sizeof(*result));
    result->source_revision       = snapshot->source_revision;
    result->image_crc32           = snapshot->image_crc32;
    result->request_fingerprint   = fingerprint;
    result->owner_epoch           = snapshot->owner_epoch;
    result->relation_generation   = snapshot->relation_generation;
    result->snapshot_generation   = snapshot->snapshot_generation;
    result->policy_generation     = snapshot->policy_generation;
    result->transaction_generation = request->transaction_generation;
    result->image_size            = snapshot->image_size;
    result->domain                = request->domain;
    result->schema                = request->schema;
    result->operation             = era_split_eeprom_sync_response_operation(request->operation);
    result->request_source_revision = request->source_revision;
    result->request_image_crc32     = request->image_crc32;
    result->request_policy_generation = request->policy_generation;
    result->request_image_size      = request->image_size;

    if (request->operation == ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ) {
        /* Stateless arbitration input, answered from the snapshot's recency
         * seat: no pin, no episode, no expected-operation gate. A
         * policy-closed responder answers all-zero — quiet rather than
         * refused — and the initiator's later episodes surface
         * POLICY_CLOSED where data would actually move. */
        result->source_revision = 0;
        result->image_crc32     = 0;
        if (request->domain == ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE) {
            result->chunk_id = snapshot->allowed ? snapshot->recency_changed_mask : 0;
            result->status   = snapshot->allowed ? snapshot->recency_baseline_valid : 0;
        } else {
            uint8_t bit      = (uint8_t)(1U << request->domain);
            result->chunk_id = snapshot->allowed && (snapshot->recency_changed_mask & bit) != 0 ? 1 : 0;
            result->status   = snapshot->allowed ? snapshot->recency_baseline_valid : 0;
            result->image_crc32 = snapshot->allowed ?
                                      era_split_wire_get16(&snapshot->recency_counter[(uint16_t)request->domain * 2U]) :
                                      0;
        }
        return true;
    }

    if (request->operation == ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ) {
        result->image_size = request->image_size;
        if (request->domain >= ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) {
            result->source_revision = 0;
            result->image_crc32     = 0;
            result->status          = ERA_SPLIT_EEPROM_SYNC_STATUS_UNSUPPORTED_DOMAIN;
            return true;
        }
        if (request->schema != ERA_HOST_PEER_STORAGE_SCHEMA_V1) {
            result->source_revision = 0;
            result->image_crc32     = 0;
            result->status          = ERA_SPLIT_EEPROM_SYNC_STATUS_UNSUPPORTED_SCHEMA;
            return true;
        }
        if (request->image_size != era_split_communication_core_storage_domain_size(request->domain)) {
            result->source_revision = 0;
            result->image_crc32     = 0;
            result->status          = ERA_SPLIT_EEPROM_SYNC_STATUS_SIZE_MISMATCH;
            return true;
        }
        if (!snapshot->allowed) {
            result->source_revision = 0;
            result->image_crc32     = 0;
            result->status          = ERA_SPLIT_EEPROM_SYNC_STATUS_POLICY_CLOSED;
            return true;
        }
        if (!snapshot->pinned || request->transaction_generation != snapshot->transaction_generation) {
            result->source_revision = 0;
            result->image_crc32     = 0;
            result->status          = ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY;
            return true;
        }
        if (request->domain != snapshot->domain) {
            result->source_revision = 0;
            result->image_crc32     = 0;
            result->status          = ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY;
            return true;
        }
        if (!snapshot->valid || !era_split_communication_core_storage_snapshot_image_current(snapshot)) {
            result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED;
            return true;
        }
        result->status = request->image_crc32 == snapshot->image_crc32 ? ERA_SPLIT_EEPROM_SYNC_STATUS_MATCH : ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER;
        return true;
    }

    if (request->operation == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ &&
        request->detail == (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_OPEN) {
        /* Push open mirrors the probe's core0 pin handoff: an unpinned
         * responder answers BUSY once so core0 can select the domain and
         * publish a push staging pin; the retried open is then answered
         * from the pinned snapshot — MATCH when the initiator's full CRC
         * equals this half's own captured content CRC (nothing to move,
         * the pull-symmetric short circuit), TRANSFER otherwise. */
        result->source_revision = request->source_revision;
        result->image_crc32     = 0;
        if (request->schema != ERA_HOST_PEER_STORAGE_SCHEMA_V1) {
            result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_UNSUPPORTED_SCHEMA;
            return true;
        }
        if (!snapshot->allowed) {
            result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_POLICY_CLOSED;
            return true;
        }
        if (!snapshot->pinned || request->transaction_generation != snapshot->transaction_generation ||
            request->domain != snapshot->domain ||
            snapshot->push_state == ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUSH_STATE_NONE) {
            result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY;
            return true;
        }
        if (request->source_revision != snapshot->source_revision ||
            snapshot->push_state != ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUSH_STATE_STAGING ||
            snapshot->expected_operation != ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ) {
            result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_STALE;
            return true;
        }
        if (!snapshot->valid || !era_split_communication_core_storage_snapshot_image_current(snapshot)) {
            result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED;
            return true;
        }
        result->status = request->image_crc32 == snapshot->image_crc32 ?
                             ERA_SPLIT_EEPROM_SYNC_STATUS_MATCH :
                             ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER;
        return true;
    }

    if (request->transaction_generation != snapshot->transaction_generation || request->domain != snapshot->domain ||
        request->schema != snapshot->schema) {
        return false;
    }
    if (request->operation == ERA_SPLIT_EEPROM_SYNC_OP_ABORT_REQ ||
        (request->operation == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ &&
         request->detail == (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_ABORT)) {
        result->source_revision = request->source_revision;
        result->image_crc32     = request->image_crc32;
        result->status          = ERA_SPLIT_EEPROM_SYNC_STATUS_ABORTED;
        return true;
    }
    if (!snapshot->allowed || request->operation != snapshot->expected_operation) {
        return false;
    }

    switch ((era_split_eeprom_sync_op_t)request->operation) {
        case ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ: {
            uint8_t expected_length = era_split_communication_core_storage_chunk_length(snapshot->image_size, request->chunk_id);
            if (request->chunk_id != snapshot->expected_chunk_id || request->detail == 0 || request->detail != expected_length) {
                return false;
            }
            if (!snapshot->valid || request->source_revision != snapshot->source_revision ||
                !era_split_communication_core_storage_snapshot_image_current(snapshot)) {
                result->operation       = ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP;
                result->status          = ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED;
                result->source_revision = request->source_revision;
                result->chunk_id        = request->chunk_id;
                result->data_length     = request->detail;
                return true;
            }
            result->chunk_id    = request->chunk_id;
            result->data_length = request->detail;
            return true;
        }
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ:
            if (!snapshot->valid || !era_split_communication_core_storage_snapshot_image_current(snapshot) ||
                request->source_revision != snapshot->source_revision || request->image_crc32 != snapshot->image_crc32) {
                result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED;
            } else {
                result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY;
            }
            return true;
        case ERA_SPLIT_EEPROM_SYNC_OP_COMPLETE_REQ:
            if (!snapshot->valid || !era_split_communication_core_storage_snapshot_image_current(snapshot) ||
                request->source_revision != snapshot->source_revision || request->image_crc32 != snapshot->image_crc32) {
                result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_STALE;
            } else {
                result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_COMPLETE;
            }
            return true;
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ: {
            /* Staging admission; the service performs the image write after
             * this plan accepts, under the same publication-stability
             * check. */
            uint8_t expected_length = era_split_communication_core_storage_chunk_length(snapshot->image_size, request->chunk_id);
            if (snapshot->push_state != ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUSH_STATE_STAGING ||
                request->chunk_id != snapshot->expected_chunk_id || request->detail == 0 ||
                request->detail != expected_length) {
                return false;
            }
            result->source_revision = snapshot->source_revision;
            result->image_crc32     = 0;
            if (request->source_revision != snapshot->source_revision || !snapshot->valid ||
                !era_split_communication_core_storage_snapshot_image_current(snapshot)) {
                result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED;
                return true;
            }
            result->status      = ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER;
            result->chunk_id    = request->chunk_id;
            result->data_length = request->detail;
            return true;
        }
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ:
            /* Only the APPLY and COMPLETE phases reach here: OPEN and ABORT
             * are handled before the expected-operation gate. */
            result->source_revision = snapshot->source_revision;
            result->image_crc32     = snapshot->image_crc32;
            if (request->source_revision != snapshot->source_revision) {
                result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_STALE;
                return true;
            }
            if (request->detail == (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_APPLY) {
                /* Core0 swaps the snapshot CRC seat to the episode's full
                 * image CRC when it advances past the last staged chunk, so
                 * this equality is the transfer-consistency check. */
                if (snapshot->push_state != ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUSH_STATE_STAGING ||
                    request->image_crc32 != snapshot->image_crc32) {
                    result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_STALE;
                } else {
                    result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY;
                }
                return true;
            }
            if (request->detail == (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_COMPLETE) {
                if (request->image_crc32 != snapshot->image_crc32) {
                    result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_STALE;
                } else if (snapshot->push_state == ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUSH_STATE_DURABLE) {
                    result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_COMPLETE;
                } else {
                    result->status = ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY;
                }
                return true;
            }
            return false;
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_REQ:
            return false;
        default:
            return false;
    }
}

bool era_split_communication_core_storage_stage_push_chunk(const era_split_communication_core_storage_responder_snapshot_t *snapshot, uint8_t chunk_id, const uint8_t *data, uint8_t length) {
    /* The push staging write: the one place core1 writes the pinned image,
     * as the episode's single writer, under the same publication-stability
     * discipline the pull-side chunk copy reads with. Core0 keeps the
     * publication seq stable and even for the whole staging episode, so
     * instability here means the target changed under the episode and the
     * chunk is refused rather than half-trusted — the caller converts that
     * into a SOURCE_CHANGED response. */
    if (snapshot == NULL || data == NULL || length == 0 || snapshot->image_address == 0 ||
        chunk_id >= ERA_HOST_PEER_STORAGE_MAX_CHUNKS) {
        return false;
    }
    uint32_t offset = (uint32_t)chunk_id * ERA_HOST_PEER_STORAGE_CHUNK_BYTES;
    if (offset >= snapshot->image_size || length > snapshot->image_size - offset) {
        return false;
    }
    if (!era_split_communication_core_storage_snapshot_image_current(snapshot)) {
        return false;
    }
    uint8_t *image = (uint8_t *)(uintptr_t)snapshot->image_address;
    memcpy(&image[offset], data, length);
    __DMB();
    return era_split_communication_core_storage_snapshot_image_current(snapshot);
}

bool era_split_communication_core_storage_prepare_responder_payload(uint8_t control, const era_split_communication_core_storage_responder_snapshot_t *snapshot, era_split_communication_core_storage_responder_result_t *result, uint8_t *payload, uint16_t *payload_len, era_split_wire_frame_lane_t *lane) {
    if (snapshot == NULL || result == NULL || payload == NULL || payload_len == NULL || lane == NULL) {
        return false;
    }

    const uint8_t *data = NULL;
    if (result->operation == ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_RSP && result->data_length > 0) {
        if (snapshot->image_address == 0 || result->chunk_id >= ERA_HOST_PEER_STORAGE_MAX_CHUNKS) {
            return false;
        }
        if (!era_split_communication_core_storage_snapshot_image_current(snapshot)) {
            result->operation       = ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP;
            result->status          = ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED;
            result->source_revision = result->request_source_revision;
            return era_split_communication_core_storage_encode_response_payload(control, result, NULL, payload, payload_len, lane);
        }
        uint32_t offset = (uint32_t)result->chunk_id * ERA_HOST_PEER_STORAGE_CHUNK_BYTES;
        if (offset >= snapshot->image_size || result->data_length > snapshot->image_size - offset) {
            return false;
        }
        const uint8_t *image = (const uint8_t *)(uintptr_t)snapshot->image_address;
        memcpy(g_era_split_communication_core_storage_chunk_scratch, &image[offset], result->data_length);
        __DMB();
        if (!era_split_communication_core_storage_snapshot_image_current(snapshot)) {
            memset(g_era_split_communication_core_storage_chunk_scratch, 0, sizeof(g_era_split_communication_core_storage_chunk_scratch));
            result->operation       = ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP;
            result->status          = ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED;
            result->source_revision = result->request_source_revision;
            return era_split_communication_core_storage_encode_response_payload(control, result, NULL, payload, payload_len, lane);
        }
        data = g_era_split_communication_core_storage_chunk_scratch;
        uint32_t hint = result->request_image_crc32 & 0xFFFFFFUL;
        if (hint != 0 &&
            (era_split_wire_crc32(data, result->data_length) & 0xFFFFFFUL) == hint) {
            /* Request-embedded chunk-CRC delta: the PEER already holds these
             * bytes; acknowledge with the zero-length content-match form. */
            result->data_length = 0;
            data                = NULL;
        }
    }

    return era_split_communication_core_storage_encode_response_payload(control, result, data, payload, payload_len, lane);
}
