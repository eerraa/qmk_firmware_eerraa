// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_mousekey_via.h"

#include "era_mousekey.h"
#include "../system/era_common_via.h"
#include "via.h"

/* Channel 13, which was skipped when 10, 11, 12, 14 and 15 were assigned
   together and has never been defined on any branch. The channels are not
   renumbered to close the gap: VIA caches a definition per (vendorId,
   productId), so an old definition against new firmware would write to the
   wrong channels, and a 256-wide namespace with seven claimants has no scarcity
   to relieve. */
enum {
    ERA_MOUSEKEY_VIA_VALUE_CURSOR_MIN_SPEED = 1,
    ERA_MOUSEKEY_VIA_VALUE_CURSOR_MAX_SPEED,
    ERA_MOUSEKEY_VIA_VALUE_CURSOR_ACCELERATION,
    ERA_MOUSEKEY_VIA_VALUE_CURSOR_INTERVAL,
    ERA_MOUSEKEY_VIA_VALUE_WHEEL_INTERVAL,
    ERA_MOUSEKEY_VIA_VALUE_WHEEL_ACCELERATION,
};

static void era_mousekey_via_save(uint8_t channel_id) {
    (void)channel_id;
    era_mousekey_save_config();
}

static bool era_mousekey_via_set_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    switch (value_id) {
        case ERA_MOUSEKEY_VIA_VALUE_CURSOR_MIN_SPEED:
            era_mousekey_set_cursor_min_speed(value_data[0]);
            return true;
        case ERA_MOUSEKEY_VIA_VALUE_CURSOR_MAX_SPEED:
            era_mousekey_set_cursor_max_speed(value_data[0]);
            return true;
        case ERA_MOUSEKEY_VIA_VALUE_CURSOR_ACCELERATION:
            era_mousekey_set_cursor_acceleration(value_data[0]);
            return true;
        case ERA_MOUSEKEY_VIA_VALUE_CURSOR_INTERVAL:
            era_mousekey_set_cursor_interval_ms(value_data[0]);
            return true;
        case ERA_MOUSEKEY_VIA_VALUE_WHEEL_INTERVAL:
            era_mousekey_set_wheel_interval_ms(value_data[0]);
            return true;
        case ERA_MOUSEKEY_VIA_VALUE_WHEEL_ACCELERATION:
            era_mousekey_set_wheel_acceleration(value_data[0]);
            return true;
        default:
            return false;
    }
}

static bool era_mousekey_via_get_value(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    value_data[0] = 0;

    switch (value_id) {
        case ERA_MOUSEKEY_VIA_VALUE_CURSOR_MIN_SPEED:
            value_data[0] = era_mousekey_get_cursor_min_speed();
            return true;
        case ERA_MOUSEKEY_VIA_VALUE_CURSOR_MAX_SPEED:
            value_data[0] = era_mousekey_get_cursor_max_speed();
            return true;
        case ERA_MOUSEKEY_VIA_VALUE_CURSOR_ACCELERATION:
            value_data[0] = era_mousekey_get_cursor_acceleration();
            return true;
        case ERA_MOUSEKEY_VIA_VALUE_CURSOR_INTERVAL:
            value_data[0] = era_mousekey_get_cursor_interval_ms();
            return true;
        case ERA_MOUSEKEY_VIA_VALUE_WHEEL_INTERVAL:
            value_data[0] = era_mousekey_get_wheel_interval_ms();
            return true;
        case ERA_MOUSEKEY_VIA_VALUE_WHEEL_ACCELERATION:
            value_data[0] = era_mousekey_get_wheel_acceleration();
            return true;
        default:
            return false;
    }
}

bool era_mousekey_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || data[1] != ERA_VIA_MOUSEKEY_CHANNEL) {
        return false;
    }
    return era_common_via_value_command(data, length, era_mousekey_via_save, era_mousekey_via_set_value, era_mousekey_via_get_value);
}
