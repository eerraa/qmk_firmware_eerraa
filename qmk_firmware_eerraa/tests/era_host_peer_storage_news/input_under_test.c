// SPDX-License-Identifier: GPL-2.0-or-later
#define SPLIT_KEYBOARD
#include <assert.h>
#include <string.h>
#include "timer.h"
#include "keyboards/era/common/split/era_split_input.c"
#include "keyboards/era/common/split/era_split_matrix_frame.c"
#include "keyboards/era/common/split/era_split_wire_payload.c"

static matrix_row_t applied[MATRIX_ROWS_PER_HAND];
static uint16_t input_owner = 1, input_relation = 2;
uint32_t era_split_input_clock(void) { return timer_read32() * 1000U; }

static void input_roundtrip(void) {
    era_split_input_request_t request;
    assert(era_split_input_source_prepare(&request, era_split_input_clock()));
    uint8_t payload[15], length, status = 0;
    if (request.op == ERA_INPUT_FENCE || request.op == ERA_INPUT_BIND) {
        payload[0] = 0x41; payload[1] = request.op;
        memcpy(payload + 2, request.body, 7);
        length = 9;
        if (request.op == ERA_INPUT_BIND) {
            era_split_wire_put16(payload + 9, request.epoch); length = 11;
        }
        assert(era_split_input_control(payload, length, input_owner, input_relation));
    } else {
        assert(era_split_input_encode(1, &request, payload, &length));
        status = era_split_input_receive(payload, length, input_owner, input_relation, era_split_input_clock());
        assert(status == ERA_INPUT_ACCEPTED || status == ERA_INPUT_ACCEPTED_STOP);
    }
    era_split_input_source_complete(&request, true, status);
}

static void input_apply(void) {
    matrix_row_t candidate[MATRIX_ROWS_PER_HAND];
    assert(era_split_input_claim(candidate, applied, ERA_SPLIT_WIRE_HALF_MATRIX_BITS, true, era_split_input_clock()));
    memcpy(applied, candidate, sizeof(applied));
    era_split_input_finish();
}

void era_test_input_start_held(void) {
    memset(applied, 0, sizeof(applied));
    era_split_input_receiver_gate(false, 0, 0);
    era_split_input_source_gate(false, 0, 0);
    input_owner = 1; input_relation = 2;
    era_split_input_receiver_gate(true, input_owner, input_relation);
    era_split_input_receiver_capacity(true);
    era_split_input_source_gate(true, input_owner, input_relation);
    matrix_row_t rows[MATRIX_ROWS_PER_HAND] = {1};
    era_split_input_source_snapshot(rows, era_split_input_clock());
    era_split_wire_session_status_t facts = {.accepted_no_host = true, .matrix_ready = true, .usb_epoch = 1};
    era_split_input_source_facts(&facts);
    input_roundtrip(); input_roundtrip(); input_roundtrip(); input_apply();
    assert(applied[0] == 1 && !era_split_input_receiver_recovery_pending());
}

void era_test_input_close_for_transfer(void) {
    era_split_input_source_gate(false, input_owner, input_relation);
    era_split_input_receiver_gate(false, input_owner, input_relation);
    assert(applied[0] == 1);
}

bool era_test_input_recover_release(void) {
    ++input_owner; ++input_relation;
    era_split_input_source_gate(true, input_owner, input_relation);
    era_split_input_receiver_gate(true, input_owner, input_relation);
    matrix_row_t rows[MATRIX_ROWS_PER_HAND] = {0};
    era_split_input_source_snapshot(rows, era_split_input_clock());
    if (!era_split_input_receiver_recovery_pending() || applied[0] != 1) return false;
    input_roundtrip(); input_roundtrip(); input_roundtrip();
    if (!era_split_input_receiver_recovery_pending()) return false;
    input_apply();
    return applied[0] == 0 && !era_split_input_receiver_recovery_pending() &&
           !era_split_input_source_recovery_pending();
}
