// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_transaction_io.h"

#include "era_split_wire_frame.h"

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
#    include "timer.h"
#    if defined(MCU_RP)
#        include "hardware/structs/timer.h"
#    endif
#endif

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
static uint32_t era_split_transaction_io_timing_read_us(void) {
#    if defined(MCU_RP)
    return timer_hw->timerawl;
#    else
    return timer_read32() * 1000U;
#    endif
}

static uint32_t era_split_transaction_io_timing_elapsed_us(uint32_t start_us) {
    return (uint32_t)(era_split_transaction_io_timing_read_us() - start_us);
}
#endif

era_split_transaction_engine_result_t era_split_transaction_io_send_compact_owned(era_split_wire_direction_t direction,
                                                                                  const uint8_t *payload,
                                                                                  uint8_t payload_len,
                                                                                  uint16_t owner_epoch,
                                                                                  era_split_transaction_failure_t *failure
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
                                                                                  ,
                                                                                  era_split_transaction_backend_send_timing_t *timing
#endif
) {
    if (failure == NULL) {
        return ERA_SPLIT_TRANSACTION_RESULT_BAD;
    }
    *failure = ERA_SPLIT_TRANSACTION_FAILURE_NONE;

    uint8_t frame[ERA_SPLIT_WIRE_MAX_FRAME_LEN];
    uint8_t frame_len = 0;
    if (!era_split_wire_encode_compact_frame(direction, payload, payload_len, frame, &frame_len)) {
        *failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
        return ERA_SPLIT_TRANSACTION_RESULT_BAD;
    }

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    uint32_t start_us = era_split_transaction_io_timing_read_us();
    if (timing != NULL) {
        *timing = (era_split_transaction_backend_send_timing_t){0};
    }
#endif
    era_split_transaction_backend_wait_result_t wait_result;
    bool sent = era_split_transaction_backend_send_owned(frame, frame_len, owner_epoch, &wait_result);
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    if (timing != NULL) {
        timing->tx_us = era_split_transaction_io_timing_elapsed_us(start_us);
    }
#endif
    if (!sent) {
        *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_SEND_TIMEOUT);
        if (*failure == ERA_SPLIT_TRANSACTION_FAILURE_NONE) {
            *failure = ERA_SPLIT_TRANSACTION_FAILURE_IO;
        }
        return ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    }
    return ERA_SPLIT_TRANSACTION_RESULT_OK;
}

era_split_transaction_engine_result_t era_split_transaction_io_receive_compact_owned(era_split_wire_direction_t expected_direction,
                                                                                     uint16_t response_window_ms,
                                                                                     uint16_t owner_epoch,
                                                                                     era_split_wire_frame_t *frame,
                                                                                     era_split_transaction_failure_t *failure
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
                                                                                     ,
                                                                                     era_split_transaction_io_receive_timing_t *timing
#endif
) {
    if (frame == NULL || failure == NULL) {
        return ERA_SPLIT_TRANSACTION_RESULT_BAD;
    }
    *failure = ERA_SPLIT_TRANSACTION_FAILURE_NONE;
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    if (timing != NULL) {
        *timing = (era_split_transaction_io_receive_timing_t){0};
    }
    uint32_t wait_start_us = era_split_transaction_io_timing_read_us();
#endif

    era_split_transaction_backend_wait_result_t     wait_result;
    era_split_transaction_backend_response_window_t window;
    if (!era_split_transaction_backend_response_window_begin(owner_epoch, response_window_ms, &window, &wait_result)) {
        *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT);
        if (*failure == ERA_SPLIT_TRANSACTION_FAILURE_NONE) {
            *failure = ERA_SPLIT_TRANSACTION_FAILURE_IO;
        }
        return ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    }

    uint8_t wire[ERA_SPLIT_WIRE_MAX_FRAME_LEN];
    if (!era_split_transaction_backend_receive_response_window_until(&window, &wire[0], 1, &wait_result)) {
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
        if (timing != NULL) {
            timing->wait_rx_us = era_split_transaction_io_timing_elapsed_us(wait_start_us);
        }
#endif
        *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT);
        return *failure == ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT ? ERA_SPLIT_TRANSACTION_RESULT_MISS : ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    }

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    if (timing != NULL) {
        timing->wait_rx_us = era_split_transaction_io_timing_elapsed_us(wait_start_us);
    }
    uint32_t decode_start_us = era_split_transaction_io_timing_read_us();
#endif
    uint8_t payload_len = wire[0] & ERA_SPLIT_WIRE_FRAME_LENGTH_MASK;
    if ((wire[0] & ERA_SPLIT_WIRE_FRAME_MARKER_MASK) != ERA_SPLIT_WIRE_FRAME_MARKER ||
        payload_len == ERA_SPLIT_WIRE_BULK_LENGTH_ESCAPE || payload_len > ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN) {
        *failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
        if (timing != NULL) {
            timing->rx_decode_us = era_split_transaction_io_timing_elapsed_us(decode_start_us);
        }
#endif
        return ERA_SPLIT_TRANSACTION_RESULT_BAD;
    }

    uint8_t remaining = (uint8_t)(payload_len + 1U);
    if (!era_split_transaction_backend_receive_response_window_until(&window, &wire[1], remaining, &wait_result)) {
        *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME);
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
        if (timing != NULL) {
            timing->rx_decode_us = era_split_transaction_io_timing_elapsed_us(decode_start_us);
        }
#endif
        return ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    }

    uint8_t frame_len = (uint8_t)(payload_len + 2U);
    if (!era_split_wire_decode_compact_frame(wire, frame_len, frame) || frame->direction != expected_direction) {
        *failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
        if (timing != NULL) {
            timing->rx_decode_us = era_split_transaction_io_timing_elapsed_us(decode_start_us);
        }
#endif
        return ERA_SPLIT_TRANSACTION_RESULT_BAD;
    }
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    if (timing != NULL) {
        timing->rx_decode_us = era_split_transaction_io_timing_elapsed_us(decode_start_us);
    }
#endif
    return ERA_SPLIT_TRANSACTION_RESULT_OK;
}

era_split_transaction_engine_result_t era_split_transaction_io_receive_idle_owned(era_split_wire_direction_t expected_direction,
                                                                                  uint16_t first_byte_timeout_ms,
                                                                                  uint16_t frame_timeout_ms,
                                                                                  uint16_t owner_epoch,
                                                                                  uint8_t *wire,
                                                                                  uint16_t wire_capacity,
                                                                                  era_split_wire_frame_t *frame,
                                                                                  era_split_transaction_failure_t *failure) {
    if (frame == NULL || failure == NULL || wire == NULL || wire_capacity < ERA_SPLIT_WIRE_COMPACT_MAX_FRAME_LEN) {
        return ERA_SPLIT_TRANSACTION_RESULT_BAD;
    }
    *failure = ERA_SPLIT_TRANSACTION_FAILURE_NONE;

    era_split_transaction_backend_wait_result_t     wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
    era_split_transaction_backend_response_window_t window;
    if (!era_split_transaction_backend_responder_idle_window_begin(owner_epoch, first_byte_timeout_ms, &window, &wait_result)) {
        *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT);
        return ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    }

    if (!era_split_transaction_backend_receive_responder_until(&window, &wire[0], 1, &wait_result)) {
        if (wait_result == ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT) {
            return ERA_SPLIT_TRANSACTION_RESULT_NONE;
        }
        *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT);
        return ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    }

    uint8_t length_field = wire[0] & ERA_SPLIT_WIRE_FRAME_LENGTH_MASK;
    if ((wire[0] & ERA_SPLIT_WIRE_FRAME_MARKER_MASK) != ERA_SPLIT_WIRE_FRAME_MARKER) {
        *failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
        return ERA_SPLIT_TRANSACTION_RESULT_BAD;
    }

    if (length_field == ERA_SPLIT_WIRE_BULK_LENGTH_ESCAPE) {
        /* Bulk-page framing in the request direction. A compact-capacity
         * caller rejects the escape before any body byte is read, which is
         * the former compact-only contract unchanged. The frame window bounds
         * the whole body, and no separate body deadline is needed on this path
         * because that window already clears the frame: 271 bytes is ~5.9 ms
         * at 460800 against ERA_SPLIT_PEER_RESPONSE_WINDOW_MS (20).
         *
         * **The baud is the premise and it is not a constant.** At the Low
         * link level the same frame is ~23.5 ms, which the unscaled window
         * would refuse -- so the backend multiplies every window it converts
         * by the level scale (era_split_transaction_backend_rp2040.c), and
         * this margin is what that multiplication preserves rather than
         * something the constant holds on its own. This comment said 25 ms
         * against a caller that passes 20; the figure was wrong before the
         * level made the premise visible. */
        if (wire_capacity < (uint16_t)(ERA_SPLIT_WIRE_BULK_PAGE_MAX_PAYLOAD_LEN + 7U)) {
            *failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
            return ERA_SPLIT_TRANSACTION_RESULT_BAD;
        }
        if (!era_split_transaction_backend_responder_frame_window_begin(&window, frame_timeout_ms, &wait_result)) {
            *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME);
            return ERA_SPLIT_TRANSACTION_RESULT_FAIL;
        }
        if (!era_split_transaction_backend_receive_responder_until(&window, &wire[1], 2, &wait_result)) {
            *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME);
            return ERA_SPLIT_TRANSACTION_RESULT_FAIL;
        }
        uint16_t bulk_payload_len = era_split_wire_get16(&wire[1]);
        if (bulk_payload_len == 0 || bulk_payload_len > ERA_SPLIT_WIRE_BULK_PAGE_MAX_PAYLOAD_LEN) {
            *failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
            return ERA_SPLIT_TRANSACTION_RESULT_BAD;
        }
        if (!era_split_transaction_backend_receive_responder_until(&window, &wire[3], (size_t)bulk_payload_len + 4U, &wait_result)) {
            *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME);
            return ERA_SPLIT_TRANSACTION_RESULT_FAIL;
        }
        uint16_t bulk_frame_len = (uint16_t)(bulk_payload_len + 7U);
        if (!era_split_wire_decode_bulk_page_frame(wire, bulk_frame_len, frame) || frame->direction != expected_direction) {
            *failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
            return ERA_SPLIT_TRANSACTION_RESULT_BAD;
        }
        return ERA_SPLIT_TRANSACTION_RESULT_OK;
    }

    uint8_t payload_len = length_field;
    if (payload_len > ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN) {
        *failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
        return ERA_SPLIT_TRANSACTION_RESULT_BAD;
    }

    if (!era_split_transaction_backend_responder_frame_window_begin(&window, frame_timeout_ms, &wait_result)) {
        *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME);
        return ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    }

    uint8_t remaining = (uint8_t)(payload_len + 1U);
    if (!era_split_transaction_backend_receive_responder_until(&window, &wire[1], remaining, &wait_result)) {
        *failure = era_split_transaction_backend_failure_from_wait(wait_result, ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME);
        return ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    }

    uint8_t frame_len = (uint8_t)(payload_len + 2U);
    if (!era_split_wire_decode_compact_frame(wire, frame_len, frame) || frame->direction != expected_direction) {
        *failure = ERA_SPLIT_TRANSACTION_FAILURE_DECODE;
        return ERA_SPLIT_TRANSACTION_RESULT_BAD;
    }
    return ERA_SPLIT_TRANSACTION_RESULT_OK;
}
