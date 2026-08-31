// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "quantum.h"

void era_split_keyboard_pre_init(void);
void era_split_keyboard_post_init(void);
void era_split_keyboard_reload_features_from_eeprom(void);
void era_split_keyboard_task(void);
void era_split_keyboard_notify_usb_device_state_change(uint8_t configure_state);
void era_split_keyboard_note_input_edge(void);
void era_split_keyboard_suspend_wakeup_init(void);

/* The lighting render gate's one owner (era_authority_contract.md). The
   HOST-PEER response apply publishes the HOST's sleep fact with the first;
   the HOST's own response snapshot reads the resolved decision with the
   second, so what a PEER renders is a session fact and never a transient
   render override on the HOST. */
bool era_split_keyboard_note_wire_lighting_sleep(bool sleep);
bool era_split_keyboard_lighting_sleep_state(void);
/* Diagnostics only: rising edges of the resolved sleep decision since boot,
   and the local ms of the last rise (breaker-hunt pairing with `brk`). */
uint16_t era_split_keyboard_lighting_sleep_true_diag(uint32_t *last_ms);

bool era_split_keyboard_process_record(uint16_t keycode, keyrecord_t *record);
bool era_split_keyboard_handle_via_command(uint8_t *data, uint8_t length);
