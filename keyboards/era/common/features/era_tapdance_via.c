// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "era_tapdance_via.h"

#include "era_tapdance.h"
#include "../system/era_common_via.h"
#include "via.h"

#if defined(ERA_TAP_DANCE_ENABLE)

#define ERA_TAP_DANCE_TERM_UNIT_MS 10U

enum {
    ERA_TAP_DANCE_VIA_VALUE_ID_BASE   = 32,
    ERA_TAP_DANCE_VIA_VALUE_ID_STRIDE = 5,
    ERA_TAP_DANCE_FIELD_TERM          = 4,
};

#define ERA_TAP_DANCE_VIA_VALUE_ID_MAX (ERA_TAP_DANCE_VIA_VALUE_ID_BASE + (ERA_TAP_DANCE_SLOT_COUNT * ERA_TAP_DANCE_VIA_VALUE_ID_STRIDE) - 1)

bool era_tapdance_is_value_id(uint8_t value_id) {
    return value_id >= ERA_TAP_DANCE_VIA_VALUE_ID_BASE && value_id <= ERA_TAP_DANCE_VIA_VALUE_ID_MAX;
}

static uint8_t era_tapdance_via_slot_index(uint8_t value_id) {
    if (!era_tapdance_is_value_id(value_id)) {
        return ERA_TAP_DANCE_SLOT_COUNT;
    }
    return (value_id - ERA_TAP_DANCE_VIA_VALUE_ID_BASE) / ERA_TAP_DANCE_VIA_VALUE_ID_STRIDE;
}

static uint8_t era_tapdance_via_field_index(uint8_t value_id) {
    return (value_id - ERA_TAP_DANCE_VIA_VALUE_ID_BASE) % ERA_TAP_DANCE_VIA_VALUE_ID_STRIDE;
}

static void era_tapdance_via_save(uint8_t channel_id) {
    (void)channel_id;
    era_tapdance_save_config();
}

static bool era_tapdance_via_set_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    uint8_t slot_index  = era_tapdance_via_slot_index(value_id);
    uint8_t field_index = era_tapdance_via_field_index(value_id);

    if (slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return false;
    }

    if (field_index < ERA_TAP_DANCE_ACTION_COUNT) {
        return era_tapdance_set_action(slot_index, field_index, ((uint16_t)value_data[0] << 8) | value_data[1]);
    }
    if (field_index == ERA_TAP_DANCE_FIELD_TERM) {
        return era_tapdance_set_slot_term_ms(slot_index, (uint16_t)value_data[0] * ERA_TAP_DANCE_TERM_UNIT_MS);
    }
    return false;
}

static bool era_tapdance_via_get_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    uint8_t slot_index  = era_tapdance_via_slot_index(value_id);
    uint8_t field_index = era_tapdance_via_field_index(value_id);

    value_data[0] = 0;
    value_data[1] = 0;

    if (slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return false;
    }

    if (field_index < ERA_TAP_DANCE_ACTION_COUNT) {
        uint16_t keycode = era_tapdance_get_action(slot_index, field_index);
        value_data[0] = keycode >> 8;
        value_data[1] = keycode & 0xFF;
        return true;
    }
    if (field_index == ERA_TAP_DANCE_FIELD_TERM) {
        value_data[0] = era_tapdance_get_slot_term_ms(slot_index) / ERA_TAP_DANCE_TERM_UNIT_MS;
        return true;
    }
    return false;
}

/* No channel test of its own: this one shares id_custom_channel with NKRO and
   with whatever a board hangs off era_board_hooks.h, so the addressing that
   reaches it is the router's channel test plus its own value-id range
   (system/era_common_via.c). What is left is the shared protocol. */
bool era_tapdance_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data) {
        return false;
    }
    return era_common_via_value_command(data, length, era_tapdance_via_save, era_tapdance_via_set_value, era_tapdance_via_get_value);
}

#endif
