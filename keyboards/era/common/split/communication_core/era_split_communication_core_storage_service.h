// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../era_split_wire_protocol.h"

bool era_split_communication_core_storage_service_initiator_once(uint16_t owner_epoch);
bool era_split_communication_core_storage_service_responder_frame(uint16_t owner_epoch, const era_split_wire_frame_t *frame);
