// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ERA_FIRMWARE_VERSION "260901R1"
#define ERA_FIRMWARE_VERSION_LENGTH 8U
#define ERA_FIRMWARE_VERSION_PAYLOAD_SIZE (ERA_FIRMWARE_VERSION_LENGTH + 1U)

#define ERA_VIA_FIRMWARE_VERSION_CHANNEL 8U
#define ERA_VIA_FIRMWARE_VERSION_VALUE_ID 1U

extern const char era_firmware_version[];

bool era_firmware_version_handle_via_command(uint8_t *data, uint8_t length);
