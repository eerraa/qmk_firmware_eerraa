// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Renumbered and unified at the DUAL-HOST parent removal (Slice 9.5,
 * owner decisions 2026-07-28): the parent ids, the three *_EFFECTIVE ids,
 * and the separate HOST-PEER/DUAL-HOST EEPROM ids all retired. One EEPROM
 * SYNC value now carries the relation-independent owner intent; which
 * relation runtime consumes it is each slice's business. */
#define ERA_SPLIT_VIA_SYNC_EEPROM_SYNC_REQUESTED_VALUE_ID 5
#define ERA_SPLIT_VIA_SYNC_INPUT_SYNC_REQUESTED_VALUE_ID 6
#define ERA_SPLIT_VIA_SYNC_RGB_SYNC_REQUESTED_VALUE_ID 7

bool era_split_via_sync_handle_via_command(uint8_t *data, uint8_t length);
