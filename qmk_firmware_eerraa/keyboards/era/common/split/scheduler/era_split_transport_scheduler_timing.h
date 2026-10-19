// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

void era_split_transport_scheduler_reset_responder_silence_watch(void);
bool era_split_transport_scheduler_responder_silence_stale(bool peer_known);
void era_split_transport_scheduler_reset_session_probe_backoff(void);
bool era_split_transport_scheduler_local_status_revalidation_due(void);
bool era_split_transport_scheduler_sample_authority(uint32_t now_ms, bool mark_dirty_on_change);
bool era_split_transport_scheduler_refresh_time_due_tokens(uint32_t now_ms);
void era_split_transport_scheduler_update_next_deadline(void);
bool era_split_transport_scheduler_refresh_route_due_flags(void);
void era_split_transport_scheduler_clear_owner_route(void);
void era_split_transport_scheduler_select_owner_route(void);
