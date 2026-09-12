// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_via_link.h"

#include <string.h>

#include "../system/era_via_system.h"
#include "era_split_link.h"
#include "via.h"

/* The dropdown labels name the baud, so a build that moved the rate would ship
 * a page that lies about what it does. Medium and Low are derived from this
 * value, so all three labels move together and one refusal covers the set.
 * No apostrophe in the message: the preprocessor lexes the text of a skipped
 * group, so one reads as an unterminated character constant and -Werror fails
 * every build this refusal is not for. */
#if SERIAL_USART_SPEED != 460800
#    error "The SYSTEM page states the link speeds as 460800 / 230400 / 115200. A board that changes ERA_SPLIT_SERIAL_USART_SPEED must restate the three labels in its VIA definition before this check is relaxed."
#endif

static bool era_split_via_link_value_id(uint8_t value_id) {
    return value_id == ERA_SPLIT_VIA_LINK_LEVEL_VALUE_ID || value_id == ERA_SPLIT_VIA_LINK_APPLY_VALUE_ID ||
           value_id == ERA_SPLIT_VIA_LINK_RUNTIME_VALUE_ID || value_id == ERA_SPLIT_VIA_LINK_STORED_VALUE_ID ||
           value_id == ERA_SPLIT_VIA_LINK_RESULT_VALUE_ID;
}

static const char *era_split_via_link_level_name(uint8_t level) {
    static const char *const names[] = {"High", "Medium", "Low"};
    return level < ERA_SPLIT_LINK_LEVEL_COUNT ? names[level] : "Unknown";
}

static const char *era_split_via_link_result_name(void) {
    static const char *const pending[] = {"Pending High", "Pending Medium", "Pending Low"};
    static const char *const applied[] = {"Applied High", "Applied Medium", "Applied Low"};
    uint8_t target = era_split_link_apply_target();
    switch (era_split_link_apply_status()) {
        case ERA_SPLIT_LINK_APPLY_PENDING:
            return target < ERA_SPLIT_LINK_LEVEL_COUNT ? pending[target] : "Pending";
        case ERA_SPLIT_LINK_APPLY_APPLIED:
            return target < ERA_SPLIT_LINK_LEVEL_COUNT ? applied[target] : "Applied";
        case ERA_SPLIT_LINK_APPLY_UNCHANGED:
            return "Already set";
        case ERA_SPLIT_LINK_APPLY_BUSY:
            return "Busy - retry";
        case ERA_SPLIT_LINK_APPLY_FAILED:
            return "Failed - check levels";
        case ERA_SPLIT_LINK_APPLY_CANCELLED:
            return "Cancelled - retry";
        default:
            return "No Apply this boot";
    }
}

static bool era_split_via_link_get_label(uint8_t value_id, uint8_t *value_data, uint8_t length) {
    const char *text;
    uint8_t stored;
    switch (value_id) {
        case ERA_SPLIT_VIA_LINK_RUNTIME_VALUE_ID:
            text = era_split_via_link_level_name(era_split_link_active_level());
            break;
        case ERA_SPLIT_VIA_LINK_STORED_VALUE_ID:
            text = era_split_link_get_stored_level(&stored) ? era_split_via_link_level_name(stored) : "Unknown";
            break;
        case ERA_SPLIT_VIA_LINK_RESULT_VALUE_ID:
            text = era_split_via_link_result_name();
            break;
        default:
            return false;
    }
    size_t bytes = strlen(text) + 1U;
    if (length < 3U + bytes) {
        return false;
    }
    memcpy(value_data, text, bytes);
    return true;
}

static bool era_split_via_link_set_value(uint8_t value_id, uint8_t *value_data, uint8_t length) {
    if (length < 4) {
        return false;
    }
    switch (value_id) {
        case ERA_SPLIT_VIA_LINK_LEVEL_VALUE_ID:
            /* The dropdown alone changes nothing on the wire and nothing in
               EEPROM. Refusing an out-of-range index rather than clamping is
               what keeps the read-back exact: a clamped write would answer the
               next get with a level the owner did not pick. */
            return era_split_link_set_pending_level(value_data[0]);
        case ERA_SPLIT_VIA_LINK_APPLY_VALUE_ID:
            /* Toggle-as-action: GET is the consumed 0, not a success status.
             * UI refresh must not create a firmware USB/session transition. */
            if (value_data[0]) {
                (void)era_split_link_request_apply();
            }
            return true;
        default:
            return false;
    }
}

static bool era_split_via_link_get_value(uint8_t value_id, uint8_t *value_data, uint8_t length) {
    if (length < 4) {
        return false;
    }
    switch (value_id) {
        case ERA_SPLIT_VIA_LINK_LEVEL_VALUE_ID:
            value_data[0] = era_split_link_pending_level();
            return true;
        case ERA_SPLIT_VIA_LINK_APPLY_VALUE_ID:
            /* Keep the legacy action control consumed. The separate local
               result label, not this toggle, carries the operation receipt. */
            value_data[0] = 0;
            return true;
        default:
            return era_split_via_link_get_label(value_id, value_data, length);
    }
}

bool era_split_via_link_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || length < 3 || data[1] != ERA_VIA_SYSTEM_CHANNEL || !era_split_via_link_value_id(data[2])) {
        return false;
    }

    uint8_t *command_id = &data[0];
    uint8_t *value_id   = &data[2];
    uint8_t *value_data = &data[3];

    switch (*command_id) {
        case id_custom_set_value:
            return era_split_via_link_set_value(*value_id, value_data, length);
        case id_custom_get_value:
            return era_split_via_link_get_value(*value_id, value_data, length);
        /* Preserve legacy control SAVE handling: selection is RAM-only and
           has nothing to flush. Read-only labels explicitly refuse SAVE. */
        case id_custom_save:
            return *value_id == ERA_SPLIT_VIA_LINK_LEVEL_VALUE_ID || *value_id == ERA_SPLIT_VIA_LINK_APPLY_VALUE_ID;
        default:
            return false;
    }
}
