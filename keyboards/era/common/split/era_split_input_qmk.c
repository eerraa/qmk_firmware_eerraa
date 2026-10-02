// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "era_split_input.h"
#include "../system/era_matrix_engine.h"
#include "../system/era_hid_report_interval.h"
#include "action.h"
#include "era_split_keyboard.h"
#include "action_tapping.h"
#include "split_common/split_util.h"
#include "hardware/structs/timer.h"

extern matrix_row_t matrix_previous[MATRIX_ROWS];
static bool input_remote_pass, input_previous_host;
static uint32_t input_hid_pressure_seen;
uint32_t era_split_input_clock(void) { return timer_hw->timerawl; }

void era_split_input_action_begin(bool action_open) {
    bool host = era_split_input_receiver_enabled();
    bool closure = era_matrix_engine_peer_closure_pending();
    input_remote_pass = host || input_previous_host || closure;
    input_previous_host = host;
    uint8_t peer_hand = is_keyboard_left() ? MATRIX_ROWS_PER_HAND : 0;
    uint8_t room = ERA_SPLIT_WIRE_HALF_MATRIX_BITS;
#ifndef NO_ACTION_TAPPING
    room = action_tapping_remote_room();
#endif
    uint32_t pressure = era_hid_report_interval_pressure_serial();
    if (pressure != input_hid_pressure_seen) {
        input_hid_pressure_seen = pressure;
        era_split_input_receiver_pressure_event();
    }
    bool hid_room = !era_hid_report_interval_pressured();
    if (!action_open) {
        era_split_input_receiver_capacity(false);
        era_split_input_receiver_pressure(!hid_room);
        if (era_split_input_receiver_pending()) era_split_keyboard_note_input_edge();
        return;
    }
    if (closure) {
        uint8_t changes = 0;
        for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; ++row) {
            matrix_row_t bits = matrix_previous[peer_hand + row];
            while (bits) { bits &= bits - 1U; ++changes; }
        }
        era_split_input_receiver_capacity(false);
        era_split_input_receiver_pressure(!hid_room);
        if (hid_room && changes <= room) (void)era_matrix_engine_sync_peer_projection(false);
        return;
    }
    /* The scalar capacity publication must run even with an empty ring, so
     * the initial baseline can be admitted. No clock on that idle path. */
    if (!era_split_input_receiver_pending()) {
        era_split_input_receiver_capacity(room == ERA_SPLIT_WIRE_HALF_MATRIX_BITS && hid_room);
        era_split_input_receiver_pressure(!hid_room);
        return;
    }
    matrix_row_t candidate[MATRIX_ROWS_PER_HAND];
    if (era_split_input_claim(candidate, matrix_previous + peer_hand, room, hid_room, era_split_input_clock())) {
        era_matrix_engine_accept_peer_snapshot(candidate);
        era_matrix_engine_sync_peer_projection(true);
    }
}
void era_split_input_action_end(void) { era_split_input_finish(); }
bool era_split_input_event_remote(uint8_t row) {
    /* Capture before swap-hands. Once enqueued the origin bitmap owns it,
     * even when the relation closes while a tapping record is still held. */
    return input_remote_pass && row < MATRIX_ROWS &&
           ((row < MATRIX_ROWS_PER_HAND) != is_keyboard_left());
}
