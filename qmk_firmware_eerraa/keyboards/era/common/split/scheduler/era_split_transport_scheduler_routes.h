// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

void era_split_transport_scheduler_begin_revalidation(uint32_t now_ms);
bool era_split_transport_scheduler_check_revalidation(uint32_t now_ms);

bool era_split_transport_scheduler_core1_initiator_pending(void);
bool era_split_transport_scheduler_cancel_core1_initiator(void);
bool era_split_transport_scheduler_stop_communication_core_for_flash_write(void);
bool era_split_transport_scheduler_initiator_route_available(void);
bool era_split_transport_scheduler_poll_core1_initiator(void);
void era_split_transport_scheduler_execute_owner_route(void);
#ifdef ERA_TOMAK_SYNC_ENABLE
#    include "../era_tomak_sync_wire.h"
bool era_split_transport_scheduler_submit_sync(const era_tomak_sync_wire_t *message);
#endif
