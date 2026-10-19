// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef IGNORE_ATOMIC_BLOCK
#    define IGNORE_ATOMIC_BLOCK
#endif
#include "quantum.h"
#include "test_matrix.h"
#include "usb_device_state.h"
#include "hardware/structs/timer.h"

/* Hardware facts, not a replacement should_process_keypress predicate. The
 * actual RP2040 reducer below decides the action gate from these facts. */
#define MCU_RP
#define PROTOCOL_CHIBIOS
#define SPLIT_USB_DETECT
bool split_hand_pin_is_left(void);
#include "../../keyboards/era/common/split/era_split_authority_reducer.c"
#include "../../platforms/chibios/suspend.c"

/* The TEST matrix is composed. Give the real HP ingress its physical halves
 * without enabling QMK's hardware split transport on other test objects. */
#define SPLIT_KEYBOARD
#undef MATRIX_ROWS_PER_HAND
#define MATRIX_ROWS_PER_HAND (MATRIX_ROWS / 2)
#include "../../keyboards/era/common/split/era_split_input_qmk.c"

era_test_timer_registers_t era_test_timer_registers;
static bool physical_left=true, peer_enabled, peer_closure;
static uint8_t configure=USB_DEVICE_STATE_CONFIGURED;
static uint32_t frame_age;
static unsigned peer_syncs;

bool split_hand_pin_is_left(void) {return physical_left;}
bool is_keyboard_left(void) {return physical_left;}
uint8_t era_usb_session_configure_state(void) {return configure;}
bool era_usb_session_sample_frame_age(uint32_t *age) {*age=frame_age;return true;}
bool era_split_input_receiver_enabled(void) {return peer_enabled;}
bool era_split_input_receiver_pending(void) {return false;}
void era_split_input_receiver_capacity(bool room) {(void)room;}
void era_split_input_receiver_pressure(bool pressured) {(void)pressured;}
void era_split_input_receiver_pressure_event(void) {}
void era_split_input_finish(void) {}
void era_split_keyboard_note_input_edge(void) {}
bool era_split_input_claim(matrix_row_t *candidate,const matrix_row_t *previous,uint8_t room,bool hid,uint32_t now) {
    (void)candidate;(void)previous;(void)room;(void)hid;(void)now;return false;
}
bool era_matrix_engine_accept_peer_snapshot(const matrix_row_t rows[MATRIX_ROWS_PER_HAND]) {(void)rows;return true;}
bool era_matrix_engine_peer_closure_pending(void) {return peer_closure;}
era_matrix_engine_peer_projection_result_t era_matrix_engine_sync_peer_projection(bool host) {
    if(host || !peer_closure)return ERA_MATRIX_ENGINE_PEER_PROJECTION_UNCHANGED;
    ++peer_syncs;
    uint8_t start=physical_left?MATRIX_ROWS_PER_HAND:0;
    for(uint8_t row=start;row<start+MATRIX_ROWS_PER_HAND;++row)
        for(uint8_t col=0;col<MATRIX_COLS;++col)release_key(col,row);
    peer_closure=false;
    return ERA_MATRIX_ENGINE_PEER_PROJECTION_CLEARED;
}
void gate_reset(bool left) {
    physical_left=left;configure=USB_DEVICE_STATE_CONFIGURED;frame_age=0;
    peer_enabled=false;peer_closure=false;peer_syncs=0;
    memset(&era_split_authority_state,0,sizeof(era_split_authority_state));
    era_split_authority_state.usb_epoch=1;
    input_remote_pass=input_previous_host=false;input_hid_pressure_seen=0;
    era_split_authority_reducer_init();era_split_authority_reducer_task();
}
void gate_start_suspend(void) {configure=USB_DEVICE_STATE_SUSPEND;era_split_authority_reducer_task();}
void gate_stale_frames(void) {frame_age=10;era_split_authority_reducer_task();}
void gate_reopen(bool wake) {
    if(wake)suspend_wakeup_init();
    configure=USB_DEVICE_STATE_CONFIGURED;frame_age=0;era_split_authority_reducer_task();
}
void gate_peer(bool enabled,bool closure) {peer_enabled=enabled;peer_closure=closure;}
unsigned gate_peer_syncs(void) {return peer_syncs;}
bool gate_bus_active(void) {return configure==USB_DEVICE_STATE_CONFIGURED;}
void housekeeping_task_user(void) {
    era_test_timer_registers.timerawl=timer_read32()*1000U;
    era_split_authority_reducer_task();
    if(gate_bus_active())era_hid_report_interval_task(timer_read32());
}

/* Hardware split setup is outside the composed TEST matrix. gate_reset()
 * initializes the actual authority reducer; the actual sync timer uses the
 * host clock with its zero initial offset, without wire anchor updates. */
void split_pre_init(void) {}
void split_post_init(void) {}
