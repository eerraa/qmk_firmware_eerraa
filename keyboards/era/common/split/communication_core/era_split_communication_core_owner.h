// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../era_split_transaction_backend.h"

typedef enum {
    ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE = 0,
    ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1 = 2,
} era_split_communication_core_backend_owner_t;

typedef enum {
    ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK = 0,
    ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OWNER,
    ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_EPOCH,
    ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_CANCEL,
    ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_RESET,
} era_split_communication_core_backend_access_t;

typedef struct {
    uint8_t  owner;
    uint8_t  backend_role;
    uint8_t  revoke_pending;
    uint8_t  cancel_pending;
    uint8_t  reset_pending;
    uint16_t owner_epoch;
    uint16_t released_epoch;
    uint16_t ready_epoch;
    uint32_t transfer_count;
    uint32_t revoke_count;
    uint32_t release_count;
    uint32_t ready_count;
    uint32_t transfer_timeout_count;
    uint32_t ready_timeout_count;
    uint32_t init_fail_count;
    uint32_t reclaim_count;
} era_split_communication_core_owner_diagnostics_t;

bool era_split_communication_core_owner_transfer_role(era_split_communication_core_backend_owner_t next_owner, era_split_transaction_backend_role_t next_role);
bool era_split_communication_core_owner_ensure_core1(void);

/* True when the live owner lease is already a coherent CORE1 lease for `role`
   (owner CORE1, lease/role epoch-coherent, Core1 ready for the current lease,
   no pending revoke). Lets the serial-reset path skip a redundant
   teardown/rebuild that would only churn the owner epoch and relation
   generation on a bare authority edge that does not change the wire role. */
bool era_split_communication_core_owner_core1_role_is_live(era_split_transaction_backend_role_t role);

/* Core1 actor service. True means the current Core1 lease is initialized. */
bool era_split_communication_core_owner_core1_service(void);

era_split_communication_core_backend_access_t era_split_communication_core_owner_backend_access(uint16_t expected_epoch);
uint16_t                                      era_split_communication_core_owner_epoch(void);
era_split_communication_core_backend_owner_t era_split_communication_core_owner_current(void);
era_split_transaction_backend_role_t          era_split_communication_core_owner_current_role(void);
void                                          era_split_communication_core_owner_get_diagnostics_snapshot(era_split_communication_core_owner_diagnostics_t *snapshot);
