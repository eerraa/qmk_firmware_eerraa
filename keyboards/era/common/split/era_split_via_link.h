// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* SYSTEM channel selection and action. SET selection is RAM-only; Apply
 * requests the agreement. GET Apply is always the consumed 0, not a completion
 * acknowledgement. No MCU reset or USB re-enumeration belongs to this API. */
#include "../system/era_wire.h"
#define ERA_SPLIT_VIA_LINK_LEVEL_VALUE_ID ERA_WIRE_SYSTEM_EERRAA_LINK_LEVEL
#define ERA_SPLIT_VIA_LINK_APPLY_VALUE_ID ERA_WIRE_SYSTEM_EERRAA_LINK_APPLY
/* Read-only NUL-terminated ASCII labels, outside legacy control ids. */
#define ERA_SPLIT_VIA_LINK_RUNTIME_VALUE_ID ERA_WIRE_SYSTEM_EERRAA_LINK_RUNTIME
#define ERA_SPLIT_VIA_LINK_STORED_VALUE_ID ERA_WIRE_SYSTEM_EERRAA_LINK_STORED
#define ERA_SPLIT_VIA_LINK_RESULT_VALUE_ID ERA_WIRE_SYSTEM_EERRAA_LINK_RESULT

bool era_split_via_link_handle_via_command(uint8_t *data, uint8_t length);
