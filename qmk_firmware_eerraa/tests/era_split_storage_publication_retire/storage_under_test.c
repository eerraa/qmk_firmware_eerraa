// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hardware/structs/timer.h"

#include <stdbool.h>

static timer_hw_t g_era_test_timer_hw;
timer_hw_t       *timer_hw = &g_era_test_timer_hw;

typedef enum {
    ERA_TEST_STORAGE_WATCH_NONE = 0,
    ERA_TEST_STORAGE_WATCH_INITIATOR,
    ERA_TEST_STORAGE_WATCH_RESPONDER,
} era_test_storage_watch_t;

typedef enum {
    ERA_TEST_STORAGE_LATE_CLAIM_NONE = 0,
    ERA_TEST_STORAGE_LATE_CLAIM_INITIATOR,
    ERA_TEST_STORAGE_LATE_CLAIM_RESPONDER,
} era_test_storage_late_claim_t;

static era_test_storage_watch_t g_era_test_storage_watch;
static bool                     g_era_test_storage_ready_observed;
static bool                     g_era_test_storage_ready_seq_even;
static bool                     g_era_test_storage_source_claim_held;
static era_test_storage_late_claim_t g_era_test_storage_late_claim;
static bool                          g_era_test_storage_late_claim_injected;

static void era_test_storage_barrier(void);

/* The QMK TEST runner is a 64-bit host, so this test translation unit removes
 * its compile-time assertions. The supported TOMAK firmware build owns those
 * assertions; publication code, records and barriers remain the exact
 * production implementation here. */
#define _Static_assert(...)
#define __DMB() era_test_storage_barrier()
#define __SEV() ((void)0)
#include "keyboards/era/common/split/communication_core/era_split_communication_core_storage.c"
#undef __SEV
#undef __DMB
#undef _Static_assert

static void era_test_storage_barrier(void) {
    __sync_synchronize();
    /* Pause a modelled Core1 reader after it stores a late source claim but
     * before its final source-sequence validation. This is the exact window
     * retirement's post-sentinel recheck and restoration must close. */
    if (!g_era_test_storage_late_claim_injected &&
        g_era_test_storage_late_claim == ERA_TEST_STORAGE_LATE_CLAIM_INITIATOR &&
        g_era_split_communication_core_storage_initiator_request.publication_seq ==
            ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED) {
        g_era_split_communication_core_storage_initiator_request.claim_generation =
            g_era_split_communication_core_storage_initiator_request.record.request_generation;
        g_era_test_storage_late_claim_injected = true;
    } else if (!g_era_test_storage_late_claim_injected &&
               g_era_test_storage_late_claim == ERA_TEST_STORAGE_LATE_CLAIM_RESPONDER &&
               g_era_split_communication_core_storage_responder_snapshot.publication_seq ==
                   ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUBLICATION_RETIRED) {
        g_era_split_communication_core_storage_responder_snapshot.claim_generation =
            g_era_split_communication_core_storage_responder_snapshot.record.snapshot_generation;
        g_era_test_storage_late_claim_injected = true;
    }
    if (g_era_test_storage_ready_observed) {
        return;
    }

    if (g_era_test_storage_watch == ERA_TEST_STORAGE_WATCH_INITIATOR &&
        g_era_split_communication_core_storage_initiator_result.ready_generation != 0) {
        g_era_test_storage_ready_observed = true;
        uint32_t publication_seq = g_era_split_communication_core_storage_initiator_result.publication_seq;
        g_era_test_storage_ready_seq_even = publication_seq != 0 && (publication_seq & 1U) == 0;
        g_era_test_storage_source_claim_held =
            g_era_split_communication_core_storage_initiator_request.claim_generation ==
            g_era_split_communication_core_storage_initiator_result.ready_generation;
    } else if (g_era_test_storage_watch == ERA_TEST_STORAGE_WATCH_RESPONDER &&
               g_era_split_communication_core_storage_responder_result.ready_generation != 0) {
        g_era_test_storage_ready_observed = true;
        uint32_t publication_seq = g_era_split_communication_core_storage_responder_result.publication_seq;
        g_era_test_storage_ready_seq_even = publication_seq != 0 && (publication_seq & 1U) == 0;
        g_era_test_storage_source_claim_held =
            g_era_split_communication_core_storage_responder_snapshot.claim_generation ==
            g_era_split_communication_core_storage_responder_result.ready_generation;
    }
}

uint32_t era_split_wire_crc32(const uint8_t *data, uint16_t length) {
    (void)data;
    (void)length;
    return 0;
}

void era_test_storage_boot_reset(void) {
    /* A host-test process runs every case in one boot. Clear only the two
       boot-terminal sentinels first, then let the production initializer
       reset the complete capacity exactly as firmware BSS startup does. */
    g_era_split_communication_core_storage_initiator_request.publication_seq = 0;
    g_era_split_communication_core_storage_responder_snapshot.publication_seq = 0;
    era_split_communication_core_storage_capacity_init();
    g_era_test_storage_watch             = ERA_TEST_STORAGE_WATCH_NONE;
    g_era_test_storage_ready_observed    = false;
    g_era_test_storage_ready_seq_even    = false;
    g_era_test_storage_source_claim_held = false;
    g_era_test_storage_late_claim        = ERA_TEST_STORAGE_LATE_CLAIM_NONE;
    g_era_test_storage_late_claim_injected = false;
}

void era_test_storage_set_time_us(uint32_t now_us) {
    g_era_test_timer_hw.timerawl = now_us;
}

void era_test_storage_watch_initiator_publish(void) {
    g_era_test_storage_watch             = ERA_TEST_STORAGE_WATCH_INITIATOR;
    g_era_test_storage_ready_observed    = false;
    g_era_test_storage_ready_seq_even    = false;
    g_era_test_storage_source_claim_held = false;
}

void era_test_storage_watch_responder_publish(void) {
    g_era_test_storage_watch             = ERA_TEST_STORAGE_WATCH_RESPONDER;
    g_era_test_storage_ready_observed    = false;
    g_era_test_storage_ready_seq_even    = false;
    g_era_test_storage_source_claim_held = false;
}

bool era_test_storage_ready_observed(void) {
    return g_era_test_storage_ready_observed;
}

bool era_test_storage_ready_seq_was_even(void) {
    return g_era_test_storage_ready_seq_even;
}

bool era_test_storage_source_claim_was_held_at_ready(void) {
    return g_era_test_storage_source_claim_held;
}

void era_test_storage_inject_late_initiator_claim(void) {
    g_era_test_storage_late_claim          = ERA_TEST_STORAGE_LATE_CLAIM_INITIATOR;
    g_era_test_storage_late_claim_injected = false;
}

void era_test_storage_inject_late_responder_claim(void) {
    g_era_test_storage_late_claim          = ERA_TEST_STORAGE_LATE_CLAIM_RESPONDER;
    g_era_test_storage_late_claim_injected = false;
}

bool era_test_storage_late_claim_was_injected(void) {
    return g_era_test_storage_late_claim_injected;
}

uint32_t era_test_storage_initiator_publication_seq(void) {
    return g_era_split_communication_core_storage_initiator_request.publication_seq;
}

uint32_t era_test_storage_responder_publication_seq(void) {
    return g_era_split_communication_core_storage_responder_snapshot.publication_seq;
}

void era_test_storage_release_late_claim(void) {
    g_era_split_communication_core_storage_initiator_request.claim_generation = 0;
    g_era_split_communication_core_storage_responder_snapshot.claim_generation = 0;
    g_era_test_storage_late_claim = ERA_TEST_STORAGE_LATE_CLAIM_NONE;
}
