// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "era_split_wire_cause.h"
#include <string.h>
#include "hal.h"
#include "pico/platform.h"
#include "hardware/structs/timer.h"
#include "../era_split_transaction_backend.h"
#include "../communication_core/era_split_communication_core_owner.h"

static era_split_wire_cause_slot_t ledger[ERA_CAUSE_COUNT];
static struct {
    volatile uint32_t seq;
    uint16_t epoch, relation;
    uint8_t mode, reason;
} context;
static struct {
    era_split_wire_cause_record_t record;
    uint16_t epoch;
    uint8_t phase, cause;
    bool active, detailed;
} pending;

_Static_assert(ERA_CAUSE_COUNT <= 48, "wire cause slot ceiling");
_Static_assert(sizeof(ledger) + sizeof(pending) + sizeof(context) + sizeof(era_split_wire_cause_capture_t) <= 4608,
               "wire cause static storage ceiling including frozen copy");

void era_split_wire_cause_context(uint16_t epoch, uint16_t relation, uint8_t mode, uint8_t reason) {
    uint32_t odd = context.seq + 1U;
    context.seq = odd;
    __DMB();
    context.epoch = epoch;
    context.relation = relation;
    context.mode = mode;
    context.reason = reason;
    __DMB();
    context.seq = odd + 1U;
}

void era_split_wire_cause_owner(uint16_t epoch) {
    /* Core0 is the sole context writer, including before the first lease. */
    era_split_wire_cause_context(epoch, context.relation, context.mode, context.reason);
}

void era_split_wire_cause_relation(uint16_t epoch, uint16_t relation, uint8_t mode) {
    era_split_wire_cause_context(epoch, relation, mode, context.reason);
}

void era_split_wire_cause_begin(uint16_t epoch, uint8_t phase) {
    pending.epoch = epoch;
    pending.phase = phase;
    pending.detailed = false;
    pending.active = true;
}

void era_split_wire_cause_end(void) {
    pending.active = false;
}

void era_split_wire_cause_phase(uint8_t phase) {
    pending.phase = phase;
    if (pending.detailed) {
        pending.record.phase = phase;
    }
}

void era_split_wire_cause_reject(uint8_t cause, uint16_t a, uint16_t b, uint8_t flags) {
    if (get_core_num() != 1 || !pending.active || pending.detailed) {
        return;
    }
    pending.record = (era_split_wire_cause_record_t){0};
    pending.record.us = timer_hw->timerawl;
    pending.record.attempt_epoch = pending.epoch;
    pending.record.observed_epoch = era_split_communication_core_owner_epoch();
    pending.record.phase = pending.phase;
    pending.record.a = a;
    pending.record.b = b;
    pending.record.flags = flags;
    era_split_transaction_backend_cause_facts(&pending.record);
    /* One bounded sample; a concurrent update means unavailable, never a wait. */
    uint32_t seq = context.seq;
    __DMB();
    if (!(seq & 1U)) {
        uint16_t epoch = context.epoch;
        pending.record.relation = context.relation;
        pending.record.mode = context.mode;
        pending.record.reason = context.reason;
        __DMB();
        if (seq == context.seq && seq != 0 && epoch == pending.epoch) {
            pending.record.valid |= ERA_CAUSE_CONTEXT_VALID;
        }
    }
    pending.cause = cause;
    pending.detailed = true;
}

void era_split_wire_cause_wait(uint8_t phase, uint8_t wait) {
    if (get_core_num() != 1 || !pending.active) {
        return;
    }
    uint8_t cause;
    switch (wait) {
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT:
            cause = phase == ERA_CAUSE_SEND ? ERA_CAUSE_SEND_TIMEOUT :
                    phase == ERA_CAUSE_SEND_CLEANUP ? ERA_CAUSE_CLEANUP_TIMEOUT :
                    phase == ERA_CAUSE_RESPONSE_FIRST ? ERA_CAUSE_INITIAL_TIMEOUT : ERA_CAUSE_PARTIAL;
            break;
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OWNER: cause = ERA_CAUSE_OWNER; break;
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_EPOCH: cause = ERA_CAUSE_EPOCH; break;
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_CANCEL: cause = ERA_CAUSE_CANCEL; break;
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_RESET: cause = ERA_CAUSE_RESET; break;
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_PIO_ERROR: cause = ERA_CAUSE_PIO; break;
        default: cause = ERA_CAUSE_IO; break;
    }
    if (!pending.detailed) {
        pending.phase = phase;
        era_split_wire_cause_reject(cause, 0, 0, 0);
        pending.record.wait = wait;
        pending.record.valid |= ERA_CAUSE_WAIT_VALID;
    }
}

void era_split_wire_cause_send(uint8_t send_wait, uint8_t cleanup_wait, uint8_t pc, uint8_t program, bool pc_valid) {
    if (get_core_num() != 1 || !pending.active) {
        return;
    }
    era_split_wire_cause_wait(send_wait ? ERA_CAUSE_SEND : ERA_CAUSE_SEND_CLEANUP, send_wait ? send_wait : cleanup_wait);
    pending.record.send_wait = send_wait;
    pending.record.cleanup_wait = cleanup_wait;
    pending.record.valid |= ERA_CAUSE_SEND_VALID | ERA_CAUSE_CLEANUP_VALID;
    if (pc_valid) {
        pending.record.pc = pc;
        pending.record.program = program;
        pending.record.valid |= ERA_CAUSE_PC_VALID;
    }
}

void era_split_wire_cause_contract(bool kind_bad, bool ack_bad, uint8_t expected, uint8_t alternate, uint8_t kind, uint8_t tx, uint8_t ack) {
    pending.phase = ERA_CAUSE_CONTRACT;
    era_split_wire_cause_reject(kind_bad ? (ack_bad ? ERA_CAUSE_KIND_ACK : ERA_CAUSE_KIND) : ERA_CAUSE_ACK, 0, 0, 0);
    pending.record.expected = expected;
    pending.record.alternate = alternate;
    pending.record.kind = kind;
    pending.record.tx = tx;
    pending.record.ack = ack;
}

void era_split_wire_cause_finish(uint8_t result, uint8_t failure) {
    if (!pending.detailed) {
        era_split_wire_cause_reject(ERA_CAUSE_UNAVAILABLE, 0, 0, 0);
    }
    era_split_wire_cause_slot_t *slot = &ledger[pending.cause];
    uint32_t count = slot->count;
    if (!count) {
        pending.record.result = result;
        pending.record.failure = failure;
        slot->first = pending.record;
        /* A halt before this publication leaves an unoccupied slot. */
        __DMB();
        slot->count = 1;
    } else if (count != UINT32_MAX) {
        slot->count = count + 1U;
    }
    era_split_wire_cause_end();
}

void era_split_wire_cause_capture(era_split_wire_cause_capture_t *capture) {
    for (unsigned i = 0; i < ERA_CAUSE_COUNT; ++i) {
        uint32_t count = ledger[i].count;
        __DMB();
        capture->slots[i].count = count;
        if (count) {
            capture->slots[i].first = ledger[i].first;
        }
    }
}
