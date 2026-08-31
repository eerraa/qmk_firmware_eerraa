// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_firmware_version.h"

#include <string.h>

#include "usb_descriptor.h"
#include "via.h"

const char era_firmware_version[] = ERA_FIRMWARE_VERSION;

/* Three VIA command bytes precede the complete NUL-terminated release
   identity. This is the enforceable report-capacity boundary; `length` is
   RAW_EPSIZE rather than the received byte count (system/era_common_via.h). */
_Static_assert(sizeof(ERA_FIRMWARE_VERSION) == ERA_FIRMWARE_VERSION_PAYLOAD_SIZE, "The ERA firmware version must remain eight ASCII bytes plus NUL.");
_Static_assert(sizeof(era_firmware_version) == ERA_FIRMWARE_VERSION_PAYLOAD_SIZE, "The exported ERA firmware version symbol must include the NUL byte.");
_Static_assert(RAW_EPSIZE >= 3U + sizeof(era_firmware_version), "An ERA VIA report must carry the complete NUL-terminated firmware version.");

bool era_firmware_version_handle_via_command(uint8_t *data, uint8_t length) {
    (void)length;

    if (!data || data[0] != id_custom_get_value || data[1] != ERA_VIA_FIRMWARE_VERSION_CHANNEL || data[2] != ERA_VIA_FIRMWARE_VERSION_VALUE_ID) {
        return false;
    }

    memcpy(&data[3], era_firmware_version, sizeof(era_firmware_version));
    return true;
}
