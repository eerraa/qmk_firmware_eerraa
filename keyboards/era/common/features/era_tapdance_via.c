// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "era_tapdance_via.h"

#include "era_tapdance.h"
#include "../system/era_common_via.h"
#include "via.h"

#if defined(ERA_TAP_DANCE_ENABLE)

enum {
    ERA_TAP_DANCE_VIA_VALUE_ID_BASE       = 32,
    ERA_TAP_DANCE_VIA_VALUE_ID_STRIDE     = 5,
    ERA_TAP_DANCE_FIELD_TERM              = 4,
    ERA_TAP_DANCE_VIA_VALUE_ID_EXACT_BASE = 72,
    ERA_TAP_DANCE_VIA_VALUE_ID_MODE_BASE = 80,
    ERA_TAP_DANCE_VIA_VALUE_ID_HOLD_TERM_BASE = 88,
    ERA_TAP_DANCE_VIA_VALUE_ID_HOLD_OTHER_BASE = 96,
};

#define ERA_TAP_DANCE_VIA_VALUE_ID_MAX (ERA_TAP_DANCE_VIA_VALUE_ID_BASE + (ERA_TAP_DANCE_SLOT_COUNT * ERA_TAP_DANCE_VIA_VALUE_ID_STRIDE) - 1)
#define ERA_TAP_DANCE_VIA_VALUE_ID_EXACT_MAX (ERA_TAP_DANCE_VIA_VALUE_ID_EXACT_BASE + ERA_TAP_DANCE_SLOT_COUNT - 1)
static bool era_tapdance_is_mode_id(uint8_t id) {
    return id >= ERA_TAP_DANCE_VIA_VALUE_ID_MODE_BASE && id < ERA_TAP_DANCE_VIA_VALUE_ID_MODE_BASE + ERA_TAP_DANCE_SLOT_COUNT;
}

static bool era_tapdance_is_timing_id(uint8_t id) {
    return id >= ERA_TAP_DANCE_VIA_VALUE_ID_HOLD_TERM_BASE && id < ERA_TAP_DANCE_VIA_VALUE_ID_HOLD_OTHER_BASE + ERA_TAP_DANCE_SLOT_COUNT;
}

bool era_tapdance_is_value_id(uint8_t value_id) {
    return (value_id >= ERA_TAP_DANCE_VIA_VALUE_ID_BASE && value_id <= ERA_TAP_DANCE_VIA_VALUE_ID_MAX) ||
           (value_id >= ERA_TAP_DANCE_VIA_VALUE_ID_EXACT_BASE && value_id <= ERA_TAP_DANCE_VIA_VALUE_ID_EXACT_MAX) || era_tapdance_is_mode_id(value_id) || era_tapdance_is_timing_id(value_id);
}

static bool era_tapdance_is_exact_term_id(uint8_t value_id) {
    return value_id >= ERA_TAP_DANCE_VIA_VALUE_ID_EXACT_BASE && value_id <= ERA_TAP_DANCE_VIA_VALUE_ID_EXACT_MAX;
}

static uint8_t era_tapdance_via_slot_index(uint8_t value_id) {
    if (era_tapdance_is_timing_id(value_id)) return (value_id - ERA_TAP_DANCE_VIA_VALUE_ID_HOLD_TERM_BASE) % ERA_TAP_DANCE_SLOT_COUNT;
    if (era_tapdance_is_mode_id(value_id)) return value_id - ERA_TAP_DANCE_VIA_VALUE_ID_MODE_BASE;
    if (era_tapdance_is_exact_term_id(value_id)) {
        return (uint8_t)(value_id - ERA_TAP_DANCE_VIA_VALUE_ID_EXACT_BASE);
    }
    if (!era_tapdance_is_value_id(value_id)) {
        return ERA_TAP_DANCE_SLOT_COUNT;
    }
    return (value_id - ERA_TAP_DANCE_VIA_VALUE_ID_BASE) / ERA_TAP_DANCE_VIA_VALUE_ID_STRIDE;
}

static uint8_t era_tapdance_via_field_index(uint8_t value_id) {
    return (value_id - ERA_TAP_DANCE_VIA_VALUE_ID_BASE) % ERA_TAP_DANCE_VIA_VALUE_ID_STRIDE;
}

static bool era_tapdance_via_save(uint8_t channel_id) {
    (void)channel_id;
    return era_tapdance_save_config();
}

static bool era_tapdance_via_set_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    uint8_t slot_index = era_tapdance_via_slot_index(value_id);

    if (slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return false;
    }

    if (era_tapdance_is_timing_id(value_id)) {
        if (value_id < ERA_TAP_DANCE_VIA_VALUE_ID_HOLD_OTHER_BASE) {
            if (length < 5U || !era_tapdance_set_hold_term(slot_index, era_common_via_get_u16_be(value_data))) return false;
            if (length >= 6U) value_data[2] = 0xD3;
        } else {
            if (length < 4U || !era_tapdance_set_hold_other(slot_index, value_data[0])) return false;
            if (length >= 5U) value_data[1] = 0xD3;
        }
        return true;
    }
    if (era_tapdance_is_mode_id(value_id)) {
        if (length < 4U || !era_tapdance_set_mode(slot_index, value_data[0])) return false;
        if (length >= 5U) value_data[1] = 0xD2;
        if (length >= 6U) value_data[2] = 0xD3;
        return true;
    }
    if (length < 5U) return false;
    if (era_tapdance_is_exact_term_id(value_id)) {
        return era_tapdance_set_slot_term_ms_exact(slot_index, era_common_via_get_u16_be(value_data));
    }
    uint8_t field_index = era_tapdance_via_field_index(value_id);
    if (field_index < ERA_TAP_DANCE_ACTION_COUNT) {
        return era_tapdance_set_action(slot_index, field_index, era_common_via_get_u16_be(value_data));
    }
    if (field_index == ERA_TAP_DANCE_FIELD_TERM) {
        return era_tapdance_set_slot_term_ms(slot_index, era_common_via_legacy_term_ms(value_data[0]));
    }
    return false;
}

static bool era_tapdance_via_get_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    uint8_t slot_index = era_tapdance_via_slot_index(value_id);

    value_data[0] = 0;
    value_data[1] = 0;

    if (slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return false;
    }

    if (era_tapdance_is_timing_id(value_id)) {
        if (value_id < ERA_TAP_DANCE_VIA_VALUE_ID_HOLD_OTHER_BASE) {
            if (length < 5U) return false;
            era_common_via_put_u16_be(value_data, era_tapdance_get_hold_term(slot_index));
            if (length >= 6U) value_data[2] = 0xD3;
        } else {
            value_data[0] = era_tapdance_get_hold_other(slot_index);
            if (length >= 5U) value_data[1] = 0xD3;
        }
        return true;
    }
    if (era_tapdance_is_mode_id(value_id)) {
        value_data[0] = era_tapdance_get_mode(slot_index);
        if (length >= 5U) value_data[1] = 0xD2;
        if (length >= 6U) value_data[2] = 0xD3;
        return true;
    }
    if (era_tapdance_is_exact_term_id(value_id)) {
        era_common_via_put_u16_be(value_data, era_tapdance_get_slot_term_ms(slot_index));
        return true;
    }
    uint8_t field_index = era_tapdance_via_field_index(value_id);
    if (field_index < ERA_TAP_DANCE_ACTION_COUNT) {
        era_common_via_put_u16_be(value_data, era_tapdance_get_action(slot_index, field_index));
        return true;
    }
    if (field_index == ERA_TAP_DANCE_FIELD_TERM) {
        value_data[0] = era_common_via_legacy_term_units(era_tapdance_get_slot_term_ms(slot_index));
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
