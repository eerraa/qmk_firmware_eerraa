// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_split_communication_core_diagnostics.h"
#include "era_split_communication_core_lifecycle.h"
#include "era_split_communication_core_initiator.h"
#include "era_split_communication_core_responder.h"

#ifndef ERA_SPLIT_COMMUNICATION_CORE_QUEUE_SLOTS
#    define ERA_SPLIT_COMMUNICATION_CORE_QUEUE_SLOTS 4
#endif

_Static_assert(ERA_SPLIT_COMMUNICATION_CORE_QUEUE_SLOTS >= 2, "ERA communication core queue needs at least two slots.");
_Static_assert(ERA_SPLIT_COMMUNICATION_CORE_QUEUE_SLOTS <= 255, "ERA communication core queue level must fit uint8_t diagnostics.");

typedef enum {
    ERA_SPLIT_COMMUNICATION_CORE_QUEUE_RECORD_INVALID = 0,
    ERA_SPLIT_COMMUNICATION_CORE_QUEUE_RECORD_INITIATOR_REQUEST = 2,
    ERA_SPLIT_COMMUNICATION_CORE_QUEUE_RECORD_INITIATOR_RESULT  = 3,
} era_split_communication_core_queue_record_kind_t;

typedef struct {
    uint8_t  kind;
    union {
        era_split_communication_core_initiator_request_t initiator_request;
        era_split_communication_core_initiator_result_t  initiator_result;
    } data;
} era_split_communication_core_queue_record_t;

/* What core1 records about one initiator lane. Every lane has all of it and
   nothing here is lane-specific -- that is the whole point, and it is why the
   state below holds an array of these indexed by lane rather than one copy of
   these names per lane.

   `pending`, `result_ready`, `generation` and `last_result_generation` are the
   lane's live handshake with core0; the rest are counters and last-value
   records that only the diagnostics mirror reads. */
typedef struct {
    volatile uint8_t  pending;
    volatile uint8_t  result_ready;
    volatile uint16_t generation;
    volatile uint16_t last_result_generation;
    volatile uint32_t submit_count;
    volatile uint32_t accept_count;
    volatile uint32_t full_count;
    volatile uint32_t start_fail_count;
    volatile uint32_t transaction_count;
    volatile uint32_t result_count;
    volatile uint32_t result_stale_count;
    volatile uint32_t result_full_count;
    volatile uint32_t ok_count;
    volatile uint32_t miss_count;
    volatile uint32_t bad_count;
    volatile uint32_t fail_count;
    volatile uint8_t  last_result;
    volatile uint8_t  last_request_sent;
    volatile uint8_t  last_route_kind;
    volatile uint8_t  last_route_reason;
    volatile uint8_t  last_response_kind;
} era_split_communication_core_lane_state_t;

typedef struct {
    volatile uint8_t  initialized;
    volatile uint8_t  launch_attempted;
    volatile uint8_t  launched;
    volatile uint8_t  running;
    volatile uint8_t  stop_requested;
    volatile uint8_t  wake_pending;
    volatile uint8_t  launch_error;
    volatile uint8_t  entry_timeout;
    volatile uint8_t  stop_timeout;
    volatile uint8_t  launch_error_stage;
    volatile uint8_t  launch_error_phase;
    /* R7: consecutive failures to bring core1 into service, and the per-boot
       cap latch. The streak counts handshake failures, entry timeouts, and
       post-launch service timeouts (the owner layer's ready wait expiring),
       and clears only when the owner observes core1 in service — ready
       published — never at start()'s own success, which the kill leg proved
       compatible with a core already dead (2026-08-06 fix). The cap is the
       give-up: while set, start() refuses in microseconds instead of spending
       a 10 ms handshake timeout on every scheduler retry, and the scheduler
       reads it to stop wanting a wire role at all. Never cleared by time —
       per boot is the bound. */
    volatile uint8_t  launch_capped;
    volatile uint8_t  launch_failure_streak;
    volatile uint32_t start_count;
    volatile uint32_t stop_count;
    volatile uint32_t wake_count;
    volatile uint32_t wake_observed_count;
    volatile uint32_t loop_count;
    volatile uint32_t idle_count;
#ifdef ERA_SPLIT_CORE1_PARK_DIAGNOSTICS_ENABLE
    /* Microseconds spent inside the loop's idle WFE, boot-cumulative. Written
       by core1 only, read by the core0 snapshot. */
    volatile uint32_t idle_us;
#endif
    volatile uint32_t launch_error_count;
    volatile uint32_t entry_timeout_count;
    volatile uint32_t stop_timeout_count;
    /* R7: times core0 declared core1 dead and hardware-reset it for relaunch. */
    volatile uint32_t dead_declared_count;
    volatile uint8_t  queue_high_water;
    volatile uint8_t  queue_result_high_water;
    volatile uint16_t queue_generation;
    volatile uint32_t queue_flush_count;
    volatile uint8_t  initiator_pending;
    volatile uint8_t  initiator_result_ready;
    volatile uint32_t initiator_queue_expired_count;
    volatile uint32_t initiator_owner_count;
    volatile uint32_t initiator_epoch_count;
    volatile uint32_t initiator_cancel_count;
    volatile uint32_t initiator_reset_count;
    volatile uint32_t initiator_pio_error_count;
    volatile uint32_t initiator_send_timeout_count;
    volatile uint32_t initiator_response_timeout_count;
    volatile uint32_t initiator_partial_frame_count;
    volatile uint32_t initiator_io_count;
    volatile uint32_t initiator_decode_count;
    volatile uint32_t initiator_response_contract_count;
    /* One record per initiator lane, indexed by the lane id itself. These
       twenty-one fields were written out once per lane as flat
       `<lane>_<field>` members, and every consumer of them was a switch
       choosing between three identical shapes -- `note_lane_result()` picked
       four counter *pointers* by lane before it could increment one. Slot
       `LANE_INVALID` is unused and costs one record; indexing by the enum
       directly is what keeps every site a plain subscript with no mapping to
       get wrong.

       The lane-specific members stay below under their own names, because a
       field only one lane has is not duplication. */
    era_split_communication_core_lane_state_t lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_COUNT];
    /* SESSION_STATUS only: the frame carries a decoded peer status, and the
       failure class is recorded per attempt because discovery is where a
       failing wire is diagnosed. */
    volatile uint8_t  session_last_failure;
    volatile uint8_t  session_last_peer_status_valid;
    /* SOURCE_PUSH only: the response-section detail of the admitted answer.
       This block is HSRSP's, and the lane that carries it is the only lane
       whose answer may carry a section at all. */
    volatile uint8_t  source_push_last_response_payload_len;
    volatile uint8_t  source_push_last_response_section_byte;
    volatile uint8_t  source_push_last_matrix_seq;
    volatile uint8_t  source_push_last_lock_state_valid;
    volatile uint8_t  source_push_last_lock_state;
    volatile uint8_t  source_push_last_visual_snapshot_valid;
    volatile uint8_t  source_push_last_rgb_state_valid;
    volatile uint8_t  source_push_last_storage_news_valid;
    volatile uint8_t  source_push_last_storage_news;
    volatile uint8_t  source_push_rx_wait_mode;
    volatile uint32_t source_push_hsrsp_count;
    volatile uint32_t source_push_visual_snapshot_count;
    volatile uint32_t source_push_rgb_state_count;
    volatile uint32_t source_push_storage_news_count;
    volatile uint8_t  source_push_response_section_or;
    volatile uint8_t  responder_snapshot_valid;
    volatile uint8_t  responder_snapshot_source;
    volatile uint8_t  responder_source_push_slot_reserved;
    volatile uint8_t  responder_source_push_result_ready;
    volatile uint8_t  responder_owner_gate_ready;
    volatile uint16_t responder_owner_epoch;
    volatile uint16_t responder_relation_generation;
    volatile uint16_t responder_snapshot_generation;
    volatile uint16_t responder_last_result_generation;
    volatile uint32_t responder_snapshot_publish_count;
    volatile uint32_t responder_owner_gate_ready_count;
    volatile uint32_t responder_owner_gate_block_count;
    volatile uint32_t responder_slot_reserve_count;
    volatile uint32_t responder_slot_full_count;
    volatile uint32_t responder_accept_count;
    volatile uint32_t responder_accept_stale_count;
    volatile uint32_t responder_drain_count;
    volatile uint32_t responder_noack_count;
    /* Requests answered with a bare ACK that carried nothing core0 has to act
       on, so no result record was published and core0 was not woken. Under a
       constant DUAL-HOST poll this is almost every request, and publishing them
       was measured costing core0 one full housekeeping pass each. Core0 folds
       this count into its responder projection in bulk, at whatever cadence it
       happens to run -- which is the whole point. */
    volatile uint32_t responder_quiet_count;
    volatile uint32_t responder_response_prepare_count;
    volatile uint32_t responder_response_prepare_fail_count;
    volatile uint32_t responder_accepted_rx_count;
    /* Arrivals that were not a frame: a PIO stop-bit error, a first byte the
       frame window never completed, or a body that failed the marker, length,
       CRC or direction check. It is the wire's own evidence that *something*
       is transmitting at a rate this half is not listening at, and it is
       kept apart from responder_noack_count on purpose -- that one also counts
       admission refusals of frames that decoded, which say nothing about the
       rate. Core0's link lane reads it against responder_accepted_rx_count
       over a dwell window (split/era_split_link.c); a peer booting on cable
       power holds the line low until its firmware claims the pin, and that
       break produces exactly one of these, because the RX program parks on
       `wait 1 pin` after the error until the line idles again. */
    volatile uint32_t responder_undecodable_rx_count;
    volatile uint8_t  responder_last_matrix_seq;
    volatile uint8_t  responder_last_request_kind_flags;
    volatile uint8_t  responder_last_response_section_mask;
    volatile uint8_t  responder_last_response_payload_len;
    volatile uint8_t  responder_last_visual_reason;
    /* The anchor's send-hold instrument (R6): the elapsed between core0's
       capture stamp and core1's encode, per anchor sent and the worst seen.
       This is the previously unmeasured publish-to-send gap the send-side
       correction closes -- the subtraction is the measurement. */
    volatile uint32_t responder_anchor_hold_last_us;
    volatile uint32_t responder_anchor_hold_max_us;
} era_split_communication_core_state_t;

extern era_split_communication_core_state_t g_era_split_communication_core;

bool    era_split_communication_core_request_push(const era_split_communication_core_queue_record_t *record, volatile uint8_t *pending_flag);
bool    era_split_communication_core_result_push(const era_split_communication_core_queue_record_t *record);
bool    era_split_communication_core_result_pop(era_split_communication_core_queue_record_t *record);
bool    era_split_communication_core_process_queue_once(void);
uint8_t era_split_communication_core_request_level(void);
uint8_t era_split_communication_core_result_level(void);

void era_split_communication_core_process_initiator(const era_split_communication_core_queue_record_t *request);
void era_split_communication_core_initiator_queue_flushed(void);
