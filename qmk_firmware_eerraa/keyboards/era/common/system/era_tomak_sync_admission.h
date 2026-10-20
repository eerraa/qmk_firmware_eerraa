// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ERA_TOMAK_ADMISSION_EMPTY,
    ERA_TOMAK_ADMISSION_WAITING,
    ERA_TOMAK_ADMISSION_EXECUTE,
    ERA_TOMAK_ADMISSION_EXPIRED,
    ERA_TOMAK_ADMISSION_CANCELLED,
} era_tomak_admission_result_t;

/* One request, no queue. The caller owns whether a presentation edge requires
 * waiting, session sampling and replies. This unit never acknowledges a write. */
bool era_tomak_sync_admission_hold(const uint8_t request[32], uint16_t session, uint32_t now_ms, uint16_t wait_ms);
bool era_tomak_sync_admission_pending(void);
void era_tomak_sync_admission_cancel(void);
/* Cancel only the captured request prefix in its originating USB session. */
bool era_tomak_sync_admission_cancel_matching(const uint8_t *prefix, uint8_t length, uint16_t session);
era_tomak_admission_result_t era_tomak_sync_admission_take(uint8_t request[32], uint16_t session, uint32_t now_ms, bool output_ready);
