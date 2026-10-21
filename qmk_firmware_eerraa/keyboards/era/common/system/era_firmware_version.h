// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ERA_FIRMWARE_VERSION "261021R1"
#define ERA_FIRMWARE_VERSION_LENGTH 8U
#define ERA_FIRMWARE_VERSION_PAYLOAD_SIZE (ERA_FIRMWARE_VERSION_LENGTH + 1U)

#include "era_wire.h"
#define ERA_VIA_FIRMWARE_VERSION_CHANNEL ERA_WIRE_CHANNELS_EERRAA_VERSION
#define ERA_VIA_FIRMWARE_VERSION_VALUE_ID ERA_WIRE_VERSION_EERRAA_ASCII

extern const char era_firmware_version[];

bool era_firmware_version_handle_via_command(uint8_t *data, uint8_t length);
