// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "era_split_wire_protocol.h"
#include "matrix.h"

/* Exact discovery excludes images without receiver-initiated REBIND support.
 * Input ops have no runtime sections; FLOW substitutes the standing envelope. */
enum {
    ERA_INPUT_SESSION = 0x17,
    ERA_INPUT_FENCE = 0x12, ERA_INPUT_FENCE_RSP = 0x13,
    ERA_INPUT_BIND = 0x14, ERA_INPUT_BIND_RSP = 0x15,
    ERA_INPUT_REBIND = 0x16, /* standing response: receiver incarnation closed */
    ERA_INPUT_DELTA = 0x22, ERA_INPUT_FULL = 0x23, ERA_INPUT_RESET = 0x24,
    ERA_INPUT_ACCEPTED = 0x25, ERA_INPUT_ACCEPTED_STOP = 0x26,
    ERA_INPUT_WAIT = 0x27, ERA_INPUT_RESET_REQUIRED = 0x28, ERA_INPUT_MISMATCH = 0x29,
    ERA_INPUT_FLOW_QUERY = 0x2a,
    ERA_INPUT_READY = 0x2b, ERA_INPUT_FLOW_WAIT = 0x2c,
    ERA_INPUT_FLOW_RESET = 0x2d, ERA_INPUT_BASELINE_READY = 0x2e,
    ERA_INPUT_FLOW_END = 0x2f,
    ERA_INPUT_SENDER_AGE_US = 24000, ERA_INPUT_RECEIVER_AGE_US = 4000,
    ERA_INPUT_HISTORY_CAPACITY = 18, ERA_INPUT_PACKET_SLOTS = 3,
    ERA_INPUT_TOKENS = 9,
};

typedef struct {
    uint8_t op, length, groups;
    uint16_t epoch, record;
    uint32_t born_deadline_us;
    uint32_t generation; /* local producer lifetime, never encoded */
    uint8_t body[ERA_INPUT_TOKENS];
} era_split_input_request_t;

bool era_split_input_validate(const uint8_t *payload, uint16_t length);
bool era_split_input_encode(uint8_t control, const era_split_input_request_t *request, uint8_t *payload, uint8_t *length);

/* Core0 source, with an immutable pending batch retained across ACK misses. */
bool era_split_input_source_gate(bool enabled, uint16_t owner, uint16_t relation);
void era_split_input_source_snapshot(const matrix_row_t *rows, uint32_t now);
void era_split_input_source_facts(const era_split_wire_session_status_t *status);
bool era_split_input_source_due(uint32_t now);
bool era_split_input_source_service_needed(void);
bool era_split_input_source_prepare(era_split_input_request_t *request, uint32_t now);
void era_split_input_source_complete(const era_split_input_request_t *request, bool ok, uint8_t status);
uint32_t era_split_input_source_generation(void);
void era_split_input_source_flow(uint8_t status, uint32_t generation);
bool era_split_input_source_pressure(void);
bool era_split_input_source_control_active(void);
bool era_split_input_source_terminal(void);
bool era_split_input_source_tx_allowed(const era_split_input_request_t *request, uint16_t owner, uint16_t relation, uint32_t now);

/* Core0 gate is independent of the runtime response-plan holder. Core1 owns
 * accepted shadow/receipt/version/write; Core0 owns pin/cursor/read/retired. */
void era_split_input_receiver_gate(bool enabled, uint16_t owner, uint16_t relation);
uint8_t era_split_input_receive(const uint8_t *payload, uint8_t length, uint16_t owner, uint16_t relation, uint32_t now);
bool era_split_input_control(const uint8_t *payload, uint8_t length, uint16_t owner, uint16_t relation);
uint8_t era_split_input_receiver_flow(uint32_t now);
bool era_split_input_receiver_rebind(uint16_t owner, uint16_t relation);
bool era_split_input_receiver_pending(void);
bool era_split_input_claim(matrix_row_t *candidate, const matrix_row_t *previous, uint8_t remote_room, bool hid_room, uint32_t now);
void era_split_input_finish(void);
void era_split_input_receiver_pressure(bool pressured);
void era_split_input_receiver_pressure_event(void);
void era_split_input_receiver_capacity(bool room);
bool era_split_input_receiver_enabled(void);
void era_split_input_receiver_end(void);

/* The QMK ingress seam claims at the start of this synchronous action pass. */
void era_split_input_action_begin(bool action_open);
void era_split_input_action_end(void);
bool era_split_input_event_remote(uint8_t row);
uint32_t era_split_input_clock(void);
