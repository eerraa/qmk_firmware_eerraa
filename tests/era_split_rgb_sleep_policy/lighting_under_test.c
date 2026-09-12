// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

// Execute the production owner/receiver/resolver. Only its input facts and
// notification sink are substituted; unrelated hardware hooks are discarded.
#include "quantum.h"
#include "atomic_util.h"
// Deterministic single-Core0 callback ordering, not an interrupt timing proof.
#undef ATOMIC_BLOCK_RESTORESTATE
#define ATOMIC_BLOCK_RESTORESTATE for (bool once = true; once; once = false)
#define SPLIT_KEYBOARD
#include "keyboards/era/common/split/era_split_keyboard.c"
#include "lighting_under_test.h"

static bool test_wire_owned;
static bool test_local_loss;

bool era_split_transport_scheduler_lighting_sleep_owner_is_wire(void) { return test_wire_owned; }
void era_split_transport_scheduler_mark_host_peer_rgb_state_due(void) {}
bool era_usb_session_frames_lost(void) { return test_local_loss; }
bool era_rgb_sleep_enabled(void) { return true; }
uint16_t era_split_board_rgb_sleep_timeout_seconds(void) { return 0; }

void era_test_lighting_reset(void) {
    era_split_keyboard_lighting_sleep_valid = false;
    era_split_keyboard_lighting_sleep = false;
    era_split_keyboard_lighting_sleep_owner_valid = false;
    era_split_keyboard_lighting_sleep_owner_is_wire = false;
    era_split_keyboard_wire_lighting_sleep_valid = false;
    era_split_keyboard_wire_lighting_sleep = false;
    era_split_keyboard_lighting_sleep_true_count = 0;
    era_split_keyboard_lighting_sleep_true_ms = 0;
    test_wire_owned = false;
    test_local_loss = false;
    usb_device_state_init();
}

void era_test_lighting_set_owner(bool wire_owned) { test_wire_owned = wire_owned; }
void era_test_lighting_set_local_loss(bool lost) { test_local_loss = lost; }
void era_test_lighting_rotate_relation(void) { era_split_keyboard_forget_wire_lighting_sleep(); }
bool era_test_lighting_publish(bool sleep) { return era_split_keyboard_note_wire_lighting_sleep(sleep); }
bool era_test_lighting_resolve(void) {
    (void)era_split_keyboard_resolve_lighting_sleep();
    return era_split_keyboard_lighting_sleep_state();
}
