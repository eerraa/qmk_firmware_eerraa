// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_communication_core_internal.h"

#include <stddef.h>
#include <string.h>

#include "hal.h"

void era_split_communication_core_get_diagnostics_snapshot(era_split_communication_core_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    __DMB();
    snapshot->running       = g_era_split_communication_core.running;
    snapshot->launch_capped = g_era_split_communication_core.launch_capped;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    snapshot->dead_declared_count           = g_era_split_communication_core.dead_declared_count;
    snapshot->initiator_queue_expired_count = g_era_split_communication_core.initiator_queue_expired_count;
#endif
}
