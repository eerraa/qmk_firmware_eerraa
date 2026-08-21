// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "quantum.h"

#if defined(ERA_TAP_DANCE_ENABLE)

#    if !defined(TAP_DANCE_ENABLE)
#        error "ERA_TAP_DANCE_ENABLE requires TAP_DANCE_ENABLE."
#    endif

#    if !defined(ERA_TAP_DANCE_KEYCODE_BASE)
#        error "ERA_TAP_DANCE_KEYCODE_BASE must be defined by the keyboard."
#    endif

#    define ERA_TAP_DANCE_SLOT_COUNT 8
#    define ERA_TAP_DANCE_ACTION_COUNT 4

typedef struct {
    uint8_t slot_index;
} era_tapdance_user_data_t;

#    define ERA_TAP_DANCE_ACTION(slot) \
        {.fn = {era_tapdance_on_each_tap, era_tapdance_on_dance_finished, era_tapdance_on_reset, NULL}, .user_data = &era_tapdance_user_data[(slot)]}

extern era_tapdance_user_data_t era_tapdance_user_data[ERA_TAP_DANCE_SLOT_COUNT];

void     era_tapdance_init(void);
void     era_tapdance_reload_from_eeprom(void);
void     era_tapdance_save_config(void);
bool     era_tapdance_set_action(uint8_t slot_index, uint8_t action_index, uint16_t keycode);
bool     era_tapdance_set_slot_term_ms(uint8_t slot_index, uint16_t term_ms);
uint16_t era_tapdance_get_action(uint8_t slot_index, uint8_t action_index);
uint16_t era_tapdance_get_slot_term_ms(uint8_t slot_index);
void     era_tapdance_on_each_tap(tap_dance_state_t *state, void *user_data);
void     era_tapdance_on_dance_finished(tap_dance_state_t *state, void *user_data);
void     era_tapdance_on_reset(tap_dance_state_t *state, void *user_data);

#endif
