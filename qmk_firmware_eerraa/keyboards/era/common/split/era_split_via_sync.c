// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_via_sync.h"

#include "../system/era_via_system.h"
#include "era_split_sync_policy.h"
#include "via.h"

static bool era_split_via_sync_requested_field(uint8_t value_id, era_split_sync_policy_field_t *field) {
    switch (value_id) {
        case ERA_SPLIT_VIA_SYNC_EEPROM_SYNC_REQUESTED_VALUE_ID:
            *field = ERA_SPLIT_SYNC_POLICY_FIELD_EEPROM;
            return true;
        case ERA_SPLIT_VIA_SYNC_INPUT_SYNC_REQUESTED_VALUE_ID:
            *field = ERA_SPLIT_SYNC_POLICY_FIELD_INPUT;
            return true;
        case ERA_SPLIT_VIA_SYNC_RGB_SYNC_REQUESTED_VALUE_ID:
            *field = ERA_SPLIT_SYNC_POLICY_FIELD_RGB;
            return true;
        default:
            return false;
    }
}

static bool era_split_via_sync_value_id(uint8_t value_id) {
    era_split_sync_policy_field_t field;
    return era_split_via_sync_requested_field(value_id, &field);
}

static bool era_split_via_sync_set_value(uint8_t value_id, uint8_t *value_data, uint8_t length) {
    era_split_sync_policy_field_t sync_field;
    if (era_split_via_sync_requested_field(value_id, &sync_field)) {
        return era_split_sync_policy_set_requested(sync_field, value_data[0] != 0);
    }

    return false;
}

static bool era_split_via_sync_get_value(uint8_t value_id, uint8_t *value_data, uint8_t length) {
    era_split_sync_policy_field_t sync_field;
    if (era_split_via_sync_requested_field(value_id, &sync_field)) {
        bool requested = false;
        if (!era_split_sync_policy_get_requested(sync_field, &requested)) {
            return false;
        }
        value_data[0] = requested ? 1 : 0;
        return true;
    }

    return false;
}

bool era_split_via_sync_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || data[1] != ERA_VIA_SYSTEM_CHANNEL || !era_split_via_sync_value_id(data[2])) {
        return false;
    }

    uint8_t *command_id = &data[0];
    uint8_t *value_id   = &data[2];
    uint8_t *value_data = &data[3];

    switch (*command_id) {
        case id_custom_set_value:
            return era_split_via_sync_set_value(*value_id, value_data, length);
        case id_custom_get_value:
            return era_split_via_sync_get_value(*value_id, value_data, length);
        /* Claimed with nothing to do, and for a well-formed VIA report it is
           not reachable at all. Two independent reasons, and the second is the
           one that survives every build: era_via_system.c owns id_custom_save
           on this channel wherever it is compiled in, AND a VIA save command is
           `[id_custom_save, channel_id]` with no value id, so byte 2 is host
           padding and the gate above admits only 5, 6 or 7. The arm answers
           only a malformed report, and it stays because the policy fields write
           through on set -- a save has nothing held back to flush, exactly as
           in era_nkro_via.c. */
        case id_custom_save:
            return true;
        default:
            return false;
    }
}
