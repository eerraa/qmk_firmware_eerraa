// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define ERA_MACRO_QUEUE_CAPACITY 4
bool era_macro_request(uint8_t id);
void era_macro_task(void);
void era_macro_cancel(void);
void era_macro_session(uint32_t generation, bool valid, bool suspended);
/* Platform updates lifecycle before execution and before transport reopen. */
void era_macro_platform_sync(void);
bool era_macro_platform_pressured(void);
bool era_macro_session_current(uint32_t generation);
#ifdef ERA_MACRO_ENABLE
#    include "action_output.h"
/* Producer policy stays here; inline it so the repeated-key path needs only
 * the shared scope read, without an extra feature-to-core call frame. */
static inline bool era_macro_emitting(void) {
    action_output_owner_t owner = action_output_get_owner();
    return owner == ACTION_OUTPUT_OWNER_MACRO_HOLD || owner == ACTION_OUTPUT_OWNER_MACRO_TAP;
}
#endif
