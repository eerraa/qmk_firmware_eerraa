// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_communication_core_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal.h"

static era_split_communication_core_queue_record_t g_era_split_communication_core_request_queue[ERA_SPLIT_COMMUNICATION_CORE_QUEUE_SLOTS];
static era_split_communication_core_queue_record_t g_era_split_communication_core_result_queue[ERA_SPLIT_COMMUNICATION_CORE_QUEUE_SLOTS];
static volatile uint32_t                           g_era_split_communication_core_request_read __attribute__((aligned(4)));
static volatile uint32_t                           g_era_split_communication_core_request_write __attribute__((aligned(4)));
static volatile uint32_t                           g_era_split_communication_core_result_read __attribute__((aligned(4)));
static volatile uint32_t                           g_era_split_communication_core_result_write __attribute__((aligned(4)));

static uint32_t era_split_communication_core_ring_next(uint32_t index) {
    index++;
    return index >= ERA_SPLIT_COMMUNICATION_CORE_QUEUE_SLOTS ? 0 : index;
}

static uint8_t era_split_communication_core_ring_level(uint32_t read, uint32_t write) {
    if (write >= read) {
        return (uint8_t)(write - read);
    }
    return (uint8_t)(ERA_SPLIT_COMMUNICATION_CORE_QUEUE_SLOTS - (uint8_t)(read - write));
}

uint8_t era_split_communication_core_request_level(void) {
    return era_split_communication_core_ring_level(g_era_split_communication_core_request_read, g_era_split_communication_core_request_write);
}

uint8_t era_split_communication_core_result_level(void) {
    return era_split_communication_core_ring_level(g_era_split_communication_core_result_read, g_era_split_communication_core_result_write);
}

static bool era_split_communication_core_request_pop(era_split_communication_core_queue_record_t *record) {
    if (record == NULL) {
        return false;
    }

    uint32_t read  = g_era_split_communication_core_request_read;
    uint32_t write = g_era_split_communication_core_request_write;
    if (read == write) {
        return false;
    }

    __DMB();
    *record = g_era_split_communication_core_request_queue[read];
    __DMB();
    g_era_split_communication_core_request_read = era_split_communication_core_ring_next(read);
    __DMB();
    return true;
}

bool era_split_communication_core_request_push(const era_split_communication_core_queue_record_t *record, volatile uint8_t *pending_flag) {
    if (record == NULL) {
        return false;
    }

    uint32_t read  = g_era_split_communication_core_request_read;
    uint32_t write = g_era_split_communication_core_request_write;
    uint32_t next  = era_split_communication_core_ring_next(write);
    if (next == read) {
        return false;
    }
    uint8_t level = era_split_communication_core_ring_level(read, next);

    __DMB();
    g_era_split_communication_core_request_queue[write] = *record;
    if (pending_flag != NULL) {
        *pending_flag = 1;
    }
    __DMB();
    g_era_split_communication_core_request_write = next;
    __DMB();

    if (level > g_era_split_communication_core.queue_high_water) {
        g_era_split_communication_core.queue_high_water = level;
    }

    return true;
}

bool era_split_communication_core_result_push(const era_split_communication_core_queue_record_t *record) {
    if (record == NULL) {
        return false;
    }

    uint32_t read  = g_era_split_communication_core_result_read;
    uint32_t write = g_era_split_communication_core_result_write;
    uint32_t next  = era_split_communication_core_ring_next(write);
    if (next == read) {
        return false;
    }
    uint8_t level = era_split_communication_core_ring_level(read, next);

    __DMB();
    g_era_split_communication_core_result_queue[write] = *record;
    __DMB();
    g_era_split_communication_core_result_write = next;
    __DMB();

    if (level > g_era_split_communication_core.queue_result_high_water) {
        g_era_split_communication_core.queue_result_high_water = level;
    }
    return true;
}

bool era_split_communication_core_result_pop(era_split_communication_core_queue_record_t *record) {
    if (record == NULL) {
        return false;
    }

    uint32_t read  = g_era_split_communication_core_result_read;
    uint32_t write = g_era_split_communication_core_result_write;
    if (read == write) {
        return false;
    }

    __DMB();
    *record = g_era_split_communication_core_result_queue[read];
    __DMB();
    g_era_split_communication_core_result_read = era_split_communication_core_ring_next(read);
    __DMB();
    return true;
}

bool era_split_communication_core_process_queue_once(void) {
    era_split_communication_core_queue_record_t request;
    if (!era_split_communication_core_request_pop(&request)) {
        return false;
    }

    switch (request.kind) {
        case ERA_SPLIT_COMMUNICATION_CORE_QUEUE_RECORD_INITIATOR_REQUEST:
            era_split_communication_core_process_initiator(&request);
            return true;
        default:
            return true;
    }
}

bool era_split_communication_core_queue_reset(void) {
    if (!era_split_communication_core_request_quiesce()) {
        return false;
    }
    __DMB();
    g_era_split_communication_core_request_read  = 0;
    g_era_split_communication_core_request_write = 0;
    g_era_split_communication_core_result_read   = 0;
    g_era_split_communication_core_result_write  = 0;
    g_era_split_communication_core.queue_high_water        = 0;
    g_era_split_communication_core.queue_result_high_water = 0;
    g_era_split_communication_core.queue_generation++;
    era_split_communication_core_initiator_queue_flushed();
    g_era_split_communication_core.queue_flush_count++;
    __DMB();
    return true;
}
