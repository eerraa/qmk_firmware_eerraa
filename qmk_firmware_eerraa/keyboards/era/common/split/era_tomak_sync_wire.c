// SPDX-License-Identifier: GPL-2.0-or-later
#include "era_tomak_sync_wire.h"
#include "era_split_wire_protocol.h"

static bool valid(const era_tomak_sync_wire_t *message) {
    return message && message->owner_epoch && message->relation_generation && message->sequence &&
           message->operation >= ERA_TOMAK_SYNC_WIRE_OFFER && message->operation <= ERA_TOMAK_SYNC_WIRE_PAIR_READY;
}

bool era_tomak_sync_wire_encode(const era_tomak_sync_wire_t *message, uint8_t control, uint8_t payload[ERA_TOMAK_SYNC_WIRE_BYTES]) {
    if (!valid(message) || !payload || (control & ERA_SPLIT_WIRE_CONTROL_RESERVED) ||
        !(control & ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK)) {
        return false;
    }
    payload[0] = control | ERA_SPLIT_WIRE_CONTROL_EXT;
    payload[1] = message->operation;
    era_split_wire_put16(payload + 2, message->owner_epoch);
    era_split_wire_put16(payload + 4, message->relation_generation);
    era_split_wire_put16(payload + 6, message->sequence);
    era_split_wire_put32(payload + 8, message->deadline_ms);
    payload[12] = message->target ? 1U : 0U;
    return true;
}

bool era_tomak_sync_wire_decode(const uint8_t *payload, uint16_t length, era_tomak_sync_wire_t *message) {
    if (!payload || !message || length != ERA_TOMAK_SYNC_WIRE_BYTES ||
        !(payload[0] & ERA_SPLIT_WIRE_CONTROL_EXT) || (payload[0] & ERA_SPLIT_WIRE_CONTROL_RESERVED) ||
        !(payload[0] & ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK) || payload[12] > 1U) {
        return false;
    }
    era_tomak_sync_wire_t decoded = {
        .owner_epoch = era_split_wire_get16(payload + 2),
        .relation_generation = era_split_wire_get16(payload + 4),
        .sequence = era_split_wire_get16(payload + 6),
        .deadline_ms = era_split_wire_get32(payload + 8),
        .operation = payload[1],
        .target = payload[12] != 0,
    };
    if (!valid(&decoded)) {
        return false;
    }
    *message = decoded;
    return true;
}

bool era_tomak_sync_wire_matches(const era_tomak_sync_wire_t *request, const era_tomak_sync_wire_t *reply, uint8_t operation) {
    return valid(request) && valid(reply) && reply->operation == operation &&
           request->owner_epoch == reply->owner_epoch &&
           request->relation_generation == reply->relation_generation &&
           request->sequence == reply->sequence && request->deadline_ms == reply->deadline_ms &&
           request->target == reply->target;
}
