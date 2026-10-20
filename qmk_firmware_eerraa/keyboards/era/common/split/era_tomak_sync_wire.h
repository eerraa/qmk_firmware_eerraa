// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* A dedicated compact class, not an alias of RESTART_ARM or an extension
 * hidden in a reserved byte. Every reply repeats the exact transition tuple. */
#define ERA_TOMAK_SYNC_WIRE_BYTES 13U
typedef enum {
    ERA_TOMAK_SYNC_WIRE_OFFER = 0x30,
    ERA_TOMAK_SYNC_WIRE_INSTALLED = 0x31,
    ERA_TOMAK_SYNC_WIRE_ARM = 0x32,
    ERA_TOMAK_SYNC_WIRE_APPLIED = 0x33,
    ERA_TOMAK_SYNC_WIRE_REJECT = 0x34,
    /* PENDING is delivery progress only, never consumer installation proof. */
    ERA_TOMAK_SYNC_WIRE_PENDING = 0x35,
    ERA_TOMAK_SYNC_WIRE_ARMED = 0x36,
    /* Core0 consumed the coordinator's APPLIED declaration after its own
     * output completed. Only an ON-target receipt can release a writer. */
    ERA_TOMAK_SYNC_WIRE_PAIR_READY = 0x37,
} era_tomak_sync_wire_op_t;

typedef struct {
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint16_t sequence;
    uint32_t deadline_ms;
    uint8_t operation;
    bool target;
} era_tomak_sync_wire_t;

bool era_tomak_sync_wire_encode(const era_tomak_sync_wire_t *message, uint8_t control, uint8_t payload[ERA_TOMAK_SYNC_WIRE_BYTES]);
bool era_tomak_sync_wire_decode(const uint8_t *payload, uint16_t length, era_tomak_sync_wire_t *message);
bool era_tomak_sync_wire_matches(const era_tomak_sync_wire_t *request, const era_tomak_sync_wire_t *reply, uint8_t operation);
