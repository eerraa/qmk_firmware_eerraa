// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_wire_frame.h"

#include <string.h>

#include "era_split_wire_payload.h"

uint8_t era_split_wire_crc8(const uint8_t *data, uint8_t length) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

uint32_t era_split_wire_crc32_update(uint32_t crc, const uint8_t *data, uint16_t length) {
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }
    return crc;
}

uint32_t era_split_wire_crc32(const uint8_t *data, uint16_t length) {
    uint32_t crc = era_split_wire_crc32_update(0xFFFFFFFFUL, data, length);
    return crc ^ 0xFFFFFFFFUL;
}

uint8_t era_split_wire_next_seq(uint8_t seq) {
    seq++;
    return seq > 7 ? 1 : seq;
}

uint8_t era_split_wire_control_byte(uint8_t tx_seq, uint8_t ack_seq, bool ext) {
    return (uint8_t)((tx_seq & 0x07) | ((ack_seq & 0x07) << 3) | (ext ? ERA_SPLIT_WIRE_CONTROL_EXT : 0));
}

bool era_split_wire_encode_compact_frame(era_split_wire_direction_t direction, const uint8_t *payload, uint8_t payload_len, uint8_t *frame, uint8_t *frame_len) {
    if (payload == NULL || frame == NULL || frame_len == NULL || payload_len == 0 || payload_len > ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN) {
        return false;
    }
    if ((payload[0] & ERA_SPLIT_WIRE_CONTROL_RESERVED) != 0 || (payload[0] & ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK) == 0) {
        return false;
    }

    volatile uint8_t *vframe = frame;
    vframe[0] = (uint8_t)(ERA_SPLIT_WIRE_FRAME_MARKER |
                          (direction == ERA_SPLIT_WIRE_DIRECTION_SECONDARY_TO_PRIMARY ? ERA_SPLIT_WIRE_FRAME_DIRECTION_BIT : 0) |
                          payload_len);
    for (uint8_t index = 0; index < payload_len; index++) {
        vframe[index + 1] = payload[index];
    }
    frame[payload_len + 1] = era_split_wire_crc8(frame, (uint8_t)(payload_len + 1));
    *frame_len = payload_len + 2;
    return true;
}

bool era_split_wire_decode_compact_frame(const uint8_t *frame, uint8_t frame_len, era_split_wire_frame_t *decoded) {
    if (frame == NULL || decoded == NULL || frame_len < 3 || frame_len > ERA_SPLIT_WIRE_MAX_FRAME_LEN) {
        return false;
    }
    if ((frame[0] & ERA_SPLIT_WIRE_FRAME_MARKER_MASK) != ERA_SPLIT_WIRE_FRAME_MARKER) {
        return false;
    }

    uint8_t payload_len = frame[0] & ERA_SPLIT_WIRE_FRAME_LENGTH_MASK;
    if (payload_len == ERA_SPLIT_WIRE_BULK_LENGTH_ESCAPE || payload_len > ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN || frame_len != payload_len + 2) {
        return false;
    }
    if (era_split_wire_crc8(frame, (uint8_t)(payload_len + 1)) != frame[payload_len + 1]) {
        return false;
    }

    decoded->direction   = (frame[0] & ERA_SPLIT_WIRE_FRAME_DIRECTION_BIT) != 0 ? ERA_SPLIT_WIRE_DIRECTION_SECONDARY_TO_PRIMARY : ERA_SPLIT_WIRE_DIRECTION_PRIMARY_TO_SECONDARY;
    decoded->lane        = ERA_SPLIT_WIRE_FRAME_LANE_COMPACT;
    decoded->kind        = ERA_SPLIT_WIRE_PAYLOAD_INVALID;
    decoded->payload_len = payload_len;

    volatile uint8_t *vpayload = decoded->payload;
    for (uint8_t index = 0; index < payload_len; index++) {
        vpayload[index] = frame[index + 1];
    }
    for (uint8_t index = payload_len; index < ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN; index++) {
        vpayload[index] = 0;
    }
    decoded->tx_seq  = decoded->payload[0] & ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK;
    decoded->ack_seq = (decoded->payload[0] & ERA_SPLIT_WIRE_CONTROL_ACK_SEQ_MASK) >> 3;

    return era_split_wire_classify_payload(decoded->payload, decoded->payload_len, decoded->lane, &decoded->kind);
}

bool era_split_wire_encode_bulk_page_frame(era_split_wire_direction_t direction, const uint8_t *payload, uint16_t payload_len, uint8_t *frame, uint16_t *frame_len) {
    if (payload == NULL || frame == NULL || frame_len == NULL || payload_len == 0 || payload_len > ERA_SPLIT_WIRE_BULK_PAGE_MAX_PAYLOAD_LEN) {
        return false;
    }
    if ((payload[0] & ERA_SPLIT_WIRE_CONTROL_RESERVED) != 0 ||
        (payload[0] & ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK) == 0 ||
        (payload[0] & ERA_SPLIT_WIRE_CONTROL_EXT) == 0 ||
        !era_split_wire_validate_bulk_page_payload(payload, payload_len)) {
        return false;
    }

    frame[0] = (uint8_t)(ERA_SPLIT_WIRE_FRAME_MARKER |
                         (direction == ERA_SPLIT_WIRE_DIRECTION_SECONDARY_TO_PRIMARY ? ERA_SPLIT_WIRE_FRAME_DIRECTION_BIT : 0) |
                         ERA_SPLIT_WIRE_BULK_LENGTH_ESCAPE);
    era_split_wire_put16(&frame[1], payload_len);
    memcpy(&frame[3], payload, payload_len);
    era_split_wire_put32(&frame[3 + payload_len], era_split_wire_crc32(frame, (uint16_t)(3 + payload_len)));
    *frame_len = (uint16_t)(payload_len + 7);
    return true;
}

bool era_split_wire_decode_bulk_page_frame(const uint8_t *frame, uint16_t frame_len, era_split_wire_bulk_page_frame_t *decoded) {
    if (frame == NULL || decoded == NULL || frame_len < 8 || frame_len > ERA_SPLIT_WIRE_BULK_PAGE_MAX_FRAME_LEN) {
        return false;
    }
    if ((frame[0] & ERA_SPLIT_WIRE_FRAME_MARKER_MASK) != ERA_SPLIT_WIRE_FRAME_MARKER ||
        (frame[0] & ERA_SPLIT_WIRE_FRAME_LENGTH_MASK) != ERA_SPLIT_WIRE_BULK_LENGTH_ESCAPE) {
        return false;
    }

    uint16_t payload_len = era_split_wire_get16(&frame[1]);
    if (payload_len == 0 || payload_len > ERA_SPLIT_WIRE_BULK_PAGE_MAX_PAYLOAD_LEN || frame_len != payload_len + 7) {
        return false;
    }
    if (era_split_wire_crc32(frame, (uint16_t)(payload_len + 3)) != era_split_wire_get32(&frame[payload_len + 3])) {
        return false;
    }

    memset(decoded, 0, sizeof(*decoded));
    decoded->direction   = (frame[0] & ERA_SPLIT_WIRE_FRAME_DIRECTION_BIT) != 0 ? ERA_SPLIT_WIRE_DIRECTION_SECONDARY_TO_PRIMARY : ERA_SPLIT_WIRE_DIRECTION_PRIMARY_TO_SECONDARY;
    decoded->lane        = ERA_SPLIT_WIRE_FRAME_LANE_BULK_PAGE;
    decoded->payload_len = payload_len;
    memcpy(decoded->payload, &frame[3], payload_len);
    decoded->tx_seq  = decoded->payload[0] & ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK;
    decoded->ack_seq = (decoded->payload[0] & ERA_SPLIT_WIRE_CONTROL_ACK_SEQ_MASK) >> 3;

    return era_split_wire_classify_payload(decoded->payload, decoded->payload_len, decoded->lane, &decoded->kind);
}
