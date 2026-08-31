// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Two controls on the SYSTEM channel, beside the three SYNC toggles. The
 * dropdown sets a RAM-only pending level and changes nothing; a changing
 * toggle starts the agreement. USB re-enumerates only after that request
 * commits, so VIA re-reads the consumed 0. An inert or uncommitted Apply
 * does not bounce, so Enable staying on is visible. Toggle-as-action on
 * channel 9 is the existing pattern -- DFU and the three-step EEPROM clean
 * confirm both take it. */
#define ERA_SPLIT_VIA_LINK_LEVEL_VALUE_ID 8
#define ERA_SPLIT_VIA_LINK_APPLY_VALUE_ID 9

bool era_split_via_link_handle_via_command(uint8_t *data, uint8_t length);
/* Arm the USB re-enumeration. Called from the owner Apply commit, not from
 * the VIA SET, so a request that never commits leaves Enable on. */
void era_split_via_link_schedule_reattach(void);
/* Runs from `era_split_keyboard_task()` after the scheduler so the bounce
 * cannot occupy T_commit. */
void era_split_via_link_task(void);
