// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>

bool era_split_transport_scheduler_core1_initiator_pending(void);
bool era_split_transport_scheduler_cancel_core1_initiator(void);
bool era_split_transport_scheduler_stop_communication_core_for_flash_write(void);
bool era_split_transport_scheduler_initiator_route_available(void);
bool era_split_transport_scheduler_poll_core1_initiator(void);
void era_split_transport_scheduler_execute_owner_route(void);
