// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

#include "era_split_communication_core_responder.h"

#ifndef ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_GENERAL_RESULT_SLOTS
#    define ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_GENERAL_RESULT_SLOTS 4U
#endif
#ifndef ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_SOURCE_RESULT_SLOTS
#    define ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_SOURCE_RESULT_SLOTS 2U
#endif

enum {
    ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_SNAPSHOT_READ_RETRIES = 4,
};

/* The wrap rule for both result rings, and it lives here because both halves
   of the pair need it: core0's unit drains and core1's unit reserves and
   publishes. It was written out twice, byte-identical, once in each -- and two
   copies of the one rule that decides where an SPSC ring turns over is the
   shape where a later widening of one ring updates one of them. */
static inline uint32_t era_split_communication_core_responder_ring_next(uint32_t index, uint32_t slots) {
    index++;
    return index >= slots ? 0 : index;
}

extern era_split_communication_core_responder_snapshot_t g_era_split_communication_core_responder_snapshot;
extern volatile uint32_t                                 g_era_split_communication_core_responder_snapshot_publish_seq;
extern volatile uint32_t                                 g_era_split_communication_core_responder_snapshot_claim_generation;
extern era_split_communication_core_responder_result_t   g_era_split_communication_core_responder_general_results[ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_GENERAL_RESULT_SLOTS];
extern volatile uint32_t                                 g_era_split_communication_core_responder_general_read;
extern volatile uint32_t                                 g_era_split_communication_core_responder_general_write;
extern era_split_communication_core_responder_result_t   g_era_split_communication_core_responder_source_results[ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_SOURCE_RESULT_SLOTS];
extern volatile uint32_t                                 g_era_split_communication_core_responder_source_read;
extern volatile uint32_t                                 g_era_split_communication_core_responder_source_write;
