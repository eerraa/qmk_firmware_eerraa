// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "../era_split_mode_planner.h"
#include "../era_split_transport_scheduler_diagnostics.h"

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
void era_split_transport_scheduler_role_diagnostics_note_mode_change(era_split_mode_t previous_mode, era_split_mode_t next_mode);
void era_split_transport_scheduler_role_diagnostics_write_snapshot(era_split_transport_scheduler_diagnostics_snapshot_t *snapshot);
void era_split_transport_scheduler_role_diagnostics_reset_baselines(era_split_mode_t current_mode);
#else
static inline void era_split_transport_scheduler_role_diagnostics_note_mode_change(era_split_mode_t previous_mode, era_split_mode_t next_mode) {
    (void)previous_mode;
    (void)next_mode;
}

static inline void era_split_transport_scheduler_role_diagnostics_write_snapshot(era_split_transport_scheduler_diagnostics_snapshot_t *snapshot) {
    (void)snapshot;
}

static inline void era_split_transport_scheduler_role_diagnostics_reset_baselines(era_split_mode_t current_mode) {
    (void)current_mode;
}
#endif
