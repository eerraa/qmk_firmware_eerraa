// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

typedef struct {
    uint8_t  running;
    uint8_t  launch_capped;
    uint32_t dead_declared_count;
    uint32_t initiator_queue_expired_count;
} era_split_communication_core_diagnostics_t;

void era_split_communication_core_get_diagnostics_snapshot(era_split_communication_core_diagnostics_t *snapshot);
