// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* SYSTEM channel selection and action. SET selection is RAM-only; Apply
 * requests the agreement. GET Apply is always the consumed 0, not a completion
 * acknowledgement. No MCU reset or USB re-enumeration belongs to this API. */
#define ERA_SPLIT_VIA_LINK_LEVEL_VALUE_ID 8
#define ERA_SPLIT_VIA_LINK_APPLY_VALUE_ID 9
/* Read-only NUL-terminated ASCII labels, outside legacy control ids. */
#define ERA_SPLIT_VIA_LINK_RUNTIME_VALUE_ID 64
#define ERA_SPLIT_VIA_LINK_STORED_VALUE_ID 65
#define ERA_SPLIT_VIA_LINK_RESULT_VALUE_ID 66

bool era_split_via_link_handle_via_command(uint8_t *data, uint8_t length);
