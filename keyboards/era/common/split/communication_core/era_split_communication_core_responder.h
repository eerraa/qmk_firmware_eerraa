// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../era_host_peer_transaction.h"
#include "../era_split_transaction_types.h"
#include "../era_split_wire_protocol.h"

typedef enum {
    ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_INVALID = 0,
    ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_SESSION,
    ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_HEARTBEAT,
    ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_SOURCE_PUSH,
    /* The same 0x20 envelope as SOURCE_PUSH carrying runtime sections instead
       of the matrix. It is a distinct result kind because it takes the general
       result slot: the dedicated source-push slot's reservation is bound to
       the matrix section, not to the op id, which is what preserves the
       invariant under a frame shape where the op alone no longer identifies a
       matrix. */
    ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_RUNTIME_PUSH,
} era_split_communication_core_responder_result_kind_t;

typedef struct {
    uint16_t                  owner_epoch;
    uint16_t                  relation_generation;
    uint16_t                  snapshot_generation;
    uint8_t                   owner_gate_ready;
    uint8_t                   session_allowed;
    era_split_wire_session_status_t local_session_status;
    uint8_t                   host_matrix_admitted;
    uint8_t                   heartbeat_allowed;
    uint8_t                   source_push_slot_available;
    /* This relation's section eligibility, resolved on core0 from the mode and
       published so core1 can clip an inbound section mask without knowing the
       relation. Core1 never decides eligibility; it enforces the one core0
       already decided. */
    uint8_t                   eligible_push_sections;
    uint8_t                   eligible_rsp_sections;
    /* Confirmed DUAL-HOST Right with a valid session: this half may answer an
       initiator's runtime slot. It is separate from host_matrix_admitted
       because that gate is a HOST-PEER matrix question and this relation
       forwards no matrix at all.

       It contributes to `heartbeat_allowed`, and it used to be the *reason*
       that flag stayed narrow: widening it would have relaxed HOST-PEER
       heartbeat admission as a side effect. R2 removed that coupling by
       deriving the HOST-PEER term from the relation fact instead of from the
       matrix admission, so the reason to keep this one narrow is now the
       response-plan guard it also gates, not the heartbeat.

       Neither this nor `runtime_push_allowed` carries a storage-exclusivity
       term (Slice 11.7). A busy responder that answers nothing is
       indistinguishable from a dead wire, and exclusivity suppresses owner
       routes rather than responder answers; what it does suppress is the
       response *plan*, so an exclusive half answers with the section-less
       ACK. */
    uint8_t                   runtime_allowed;
    /* This half may accept a non-matrix push, which since Slice 11.6 means
       both relations: the HOST-PEER HOST receives its PEER's AUTHORITY section
       on the same envelope the DUAL-HOST Right receives INPUT_LAYER on.
       Separate from `runtime_allowed` because they answer different questions
       and sharing one flag is what made the HOST refuse that push outright --
       no ACK, so the PEER's transaction failed and every authority edge reset
       the relation. One flag per question. */
    uint8_t                   runtime_push_allowed;
    uint8_t                   lock_state_bits;
    uint8_t                   visual_baseline_valid;
    uint8_t                   visual_reason;
    uint8_t                   visual_baseline[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES];
    uint8_t                   rgb_state_valid;
    era_host_peer_rgb_state_t rgb_state;
    uint8_t                   storage_news;
    uint32_t                  time_anchor_ms;
    /* The R6 send-side stamp travelling with the anchor it dates; core1 adds
       the elapsed at encode. */
    uint32_t                  time_anchor_stamp_us;
    /* This half's own layer state, captured cold on the core0 publish. Core1
       serializes the copied byte and never reads layer_state itself. */
    uint8_t                   input_layer;
    /* Likewise for this half's session facts: core0 resolves them and core1
       serializes the copied record. It is in the snapshot rather than derived
       from `local_session_status` beside it so the sent-state shadow governs
       one value, the one that actually crosses. */
    era_split_wire_authority_section_t authority;
    /* This half's tap-hold activity (FA-2 S2), the same copied-value rule. */
    era_split_wire_activity_section_t activity;
    uint8_t                   response_section_mask;
} era_split_communication_core_responder_snapshot_t;

typedef struct {
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint16_t snapshot_generation;
    uint8_t  kind;
    /* No response_tx_seq and no failure. Both were core1's own working values
       sitting in a record that crosses the core boundary: the sequence never
       left the function that produced it, and the failure code was written on
       three paths and read on none -- core0 reads `result`, which is the
       coarser fact it acts on. Retired 2026-08-11.

       A field earns a place here by being read after publication. The usual
       argument for that -- this record is stored in four plus two ring slots,
       so a byte costs six -- did not collect on these two: `sizeof` is
       unchanged at 140, because both bytes fell into tail padding the record
       already had. The rule stands and the accounting is honest about which
       one it was. What the removal did buy is 48 bytes of code, which is the
       three stores and the field arithmetic around them. */
    uint8_t  response_sent;
    uint8_t  result;
    uint8_t  response_section_mask;
    era_split_wire_session_status_t peer_status;
    era_host_peer_transaction_responder_response_plan_t response_plan;
    uint8_t packed_matrix[ERA_SPLIT_WIRE_HALF_MATRIX_BYTES];
    uint8_t peer_input_layer;
    uint8_t peer_input_layer_valid;
    /* The initiator's storage-pending fact from the push cell (2026-08-14
       indicator redesign). Core1 copies the decoded flag; core0's drain
       latches it into the storage engine's mirror, the EEPROM SYNC lamp's
       peer arm. Idempotent by construction — the apply is a latch write. */
    uint8_t peer_storage_pending;
    uint8_t peer_storage_pending_valid;
    /* The initiator's restart arm -- act, param and the shared-clock deadline.
       Core1 copies the decoded body; core0's drain is what holds the deadline
       and resets on it, because that is the side that owns EEPROM and the
       reset. */
    uint8_t  peer_restart_act;
    uint8_t  peer_restart_param;
    uint32_t peer_restart_commit_ms;
    uint8_t  peer_restart_valid;
    uint8_t peer_authority_valid;
    era_split_wire_authority_section_t peer_authority;
    /* The initiator's RGB config from the DUAL-HOST push cell (Slice 12).
       Core1 copies the decoded body; whether it carries an effect is core0's
       apply-side question -- the receiver's own RGB requested bit gates the
       apply, and the sleep bit is skipped there (era_authority_contract.md). */
    uint8_t peer_rgb_state_valid;
    era_host_peer_rgb_state_t peer_rgb_state;
    /* The initiator's tap-hold activity from the push cell (FA-2 S2). */
    uint8_t peer_activity_valid;
    era_split_wire_activity_section_t peer_activity;
    /* The initiator's visual baseline from the push cell (Slice 14). Core1
       copies the decoded body; the receiver's own RGB requested bit gates
       the apply on core0. Per-exchange results need no sequence -- each
       result is one arrival. */
    uint8_t peer_visual_valid;
    era_host_peer_visual_snapshot_t peer_visual_snapshot;
} era_split_communication_core_responder_result_t;

/* Core1-only aggregation for HEARTBEAT replies whose duplicate Core0 result
   was coalesced behind an already-pending exact successful result. The reply
   still crossed the wire, so cold Core0 diagnostics fold these monotonic
   counts later instead of misclassifying it as a bare ACK. */
typedef struct {
    uint32_t heartbeat_count;
    uint32_t response_count;
    uint32_t visual_count;
    uint32_t rgb_count;
    uint32_t runtime_section_count;
    uint32_t activity_count;
    uint32_t bad_count;
} era_split_communication_core_responder_coalesced_counts_t;

bool era_split_communication_core_publish_responder_snapshot(const era_split_communication_core_responder_snapshot_t *snapshot);
bool era_split_communication_core_responder_result_ready(void);
bool era_split_communication_core_drain_responder_result(era_split_communication_core_responder_result_t *result);
bool era_split_communication_core_responder_result_matches_current(const era_split_communication_core_responder_result_t *result);
bool era_split_communication_core_responder_service_once(uint16_t owner_epoch);
uint32_t era_split_communication_core_responder_accepted_rx_count(void);
/* Free-running count of arrivals that were not a frame (PIO error, partial
   frame, decode failure). The link lane reads it beside the accepted count to
   tell "a peer is talking at another rate" from "nothing is talking"
   (split/era_split_link.c). */
uint32_t era_split_communication_core_responder_undecodable_rx_count(void);
/* Free-running count of requests answered without publishing a result, for the
   bulk diagnostic fold on core0. */
uint32_t era_split_communication_core_responder_quiet_count(void);
void era_split_communication_core_responder_get_coalesced_counts(era_split_communication_core_responder_coalesced_counts_t *counts);
