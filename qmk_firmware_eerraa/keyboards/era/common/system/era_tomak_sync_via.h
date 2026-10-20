// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool era_tomak_sync_via_admit(uint8_t *request, uint8_t length);
void era_tomak_sync_via_task(void);
