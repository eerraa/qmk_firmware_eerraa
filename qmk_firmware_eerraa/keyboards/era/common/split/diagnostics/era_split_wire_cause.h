// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
#include <stdbool.h>
#include <stdint.h>

/* Capture schema 1. Slots are causes, not a chronology or a capacity pool. */
enum {
    ERA_CAUSE_UNAVAILABLE,
    ERA_CAUSE_ENCODE_ARGS, ERA_CAUSE_ENCODE_CONTROL,
    ERA_CAUSE_COMPACT_CRC, ERA_CAUSE_COMPACT_PAYLOAD,
    ERA_CAUSE_BULK_CRC, ERA_CAUSE_BULK_PAYLOAD,
    ERA_CAUSE_RX_MARKER, ERA_CAUSE_RX_LENGTH, ERA_CAUSE_RX_CAPACITY,
    ERA_CAUSE_DIRECTION, ERA_CAUSE_KIND, ERA_CAUSE_ACK, ERA_CAUSE_KIND_ACK,
    ERA_CAUSE_SEND_TIMEOUT, ERA_CAUSE_INITIAL_TIMEOUT, ERA_CAUSE_PARTIAL,
    ERA_CAUSE_OWNER, ERA_CAUSE_EPOCH, ERA_CAUSE_CANCEL, ERA_CAUSE_RESET,
    ERA_CAUSE_PIO, ERA_CAUSE_IO, ERA_CAUSE_CLEANUP_TIMEOUT,
    ERA_CAUSE_COUNT
};
enum {
    ERA_CAUSE_SEND = 1, ERA_CAUSE_RESPONSE_BEGIN, ERA_CAUSE_RESPONSE_FIRST,
    ERA_CAUSE_RESPONSE_BODY, ERA_CAUSE_IDLE_BEGIN, ERA_CAUSE_IDLE_FIRST,
    ERA_CAUSE_FRAME_BEGIN, ERA_CAUSE_BULK_HEADER, ERA_CAUSE_BULK_BODY,
    ERA_CAUSE_COMPACT_BODY, ERA_CAUSE_CONTRACT, ERA_CAUSE_SEND_CLEANUP,
    ERA_CAUSE_SEND_ADMISSION, ERA_CAUSE_SEND_FINAL
};
enum {
    ERA_CAUSE_CONTEXT_VALID = 1, ERA_CAUSE_WAIT_VALID = 2,
    ERA_CAUSE_SEND_VALID = 4, ERA_CAUSE_CLEANUP_VALID = 8,
    ERA_CAUSE_PC_VALID = 16
};
typedef struct {
    uint32_t us, baud;
    uint16_t attempt_epoch, observed_epoch, relation, a, b;
    uint8_t mode, reason, valid, phase, result, failure, role;
    uint8_t wait, send_wait, cleanup_wait, pc, program;
    uint8_t expected, alternate, kind, tx, ack, flags;
} era_split_wire_cause_record_t;
typedef struct {
    volatile uint32_t count;
    era_split_wire_cause_record_t first;
} era_split_wire_cause_slot_t;
typedef struct {
    era_split_wire_cause_slot_t slots[ERA_CAUSE_COUNT];
} era_split_wire_cause_capture_t;

void era_split_wire_cause_begin(uint16_t epoch, uint8_t phase);
void era_split_wire_cause_end(void);
void era_split_wire_cause_phase(uint8_t phase);
void era_split_wire_cause_reject(uint8_t cause, uint16_t a, uint16_t b, uint8_t flags);
void era_split_wire_cause_wait(uint8_t phase, uint8_t wait);
void era_split_wire_cause_send(uint8_t send_wait, uint8_t cleanup_wait, uint8_t pc, uint8_t program, bool pc_valid);
void era_split_wire_cause_contract(bool kind_bad, bool ack_bad, uint8_t expected, uint8_t alternate, uint8_t kind, uint8_t tx, uint8_t ack);
void era_split_wire_cause_finish(uint8_t result, uint8_t failure);
void era_split_wire_cause_capture(era_split_wire_cause_capture_t *capture);
void era_split_wire_cause_context(uint16_t epoch, uint16_t relation, uint8_t mode, uint8_t reason);
void era_split_wire_cause_relation(uint16_t epoch, uint16_t relation, uint8_t mode);
void era_split_wire_cause_owner(uint16_t epoch);
/* Error-only backend facts: no live Core0-owned semantic reads. */
void era_split_transaction_backend_cause_facts(era_split_wire_cause_record_t *record);
#endif
