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

typedef struct {
    volatile uint8_t  initialized;
    volatile uint8_t  launched;
    volatile uint8_t  running;
    volatile uint8_t  stop_requested;
    volatile uint8_t  wake_pending;
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
    volatile uint32_t loop_count;
    volatile uint32_t launch_error_count;
    volatile uint32_t entry_timeout_count;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    /* R7: times core0 declared core1 dead and hardware-reset it for relaunch. */
    volatile uint32_t dead_declared_count;
    volatile uint32_t initiator_queue_expired_count;
#endif
    volatile uint8_t  initiator_pending;
    volatile uint8_t  initiator_result_ready;
    volatile uint8_t  responder_snapshot_valid;
    volatile uint16_t responder_owner_epoch;
    volatile uint16_t responder_relation_generation;
    volatile uint16_t responder_snapshot_generation;
    /* Requests answered with a bare ACK that carried nothing core0 has to act
       on, so no result record was published and core0 was not woken. Under a
       constant DUAL-HOST poll this is almost every request, and publishing them
       was measured costing core0 one full housekeeping pass each. Core0 folds
       this count into its responder projection in bulk, at whatever cadence it
       happens to run -- which is the whole point. */
    volatile uint32_t responder_quiet_count;
    /* Successful/failed duplicate HEARTBEAT replies that intentionally publish
       no second Core0 result. `quiet_count` remains strictly section-less ACKs. */
    volatile uint32_t responder_coalesced_heartbeat_count;
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
