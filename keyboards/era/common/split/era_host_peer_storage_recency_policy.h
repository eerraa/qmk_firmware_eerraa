// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>

/* Small, host-testable state boundary for recency persistence. The storage
 * owner may expose a settled capture only after every counter mutation that
 * capture requires has durably succeeded; convergence may retire changed state
 * only after its combined baseline+counter metadata publication succeeds. */
typedef enum {
    ERA_HOST_PEER_STORAGE_RECENCY_SETTLE_NOT_ATTEMPTED = 0,
    ERA_HOST_PEER_STORAGE_RECENCY_SETTLE_PERSIST_FAILED,
    ERA_HOST_PEER_STORAGE_RECENCY_SETTLE_AT_BASELINE,
    ERA_HOST_PEER_STORAGE_RECENCY_SETTLE_DEPARTED,
} era_host_peer_storage_recency_settle_result_t;

static inline era_host_peer_storage_recency_settle_result_t era_host_peer_storage_recency_settle_result(bool at_baseline,
                                                                                                         bool persistence_required,
                                                                                                         bool persistence_succeeded) {
    if (persistence_required && !persistence_succeeded) {
        return ERA_HOST_PEER_STORAGE_RECENCY_SETTLE_PERSIST_FAILED;
    }
    return at_baseline ? ERA_HOST_PEER_STORAGE_RECENCY_SETTLE_AT_BASELINE : ERA_HOST_PEER_STORAGE_RECENCY_SETTLE_DEPARTED;
}

static inline bool era_host_peer_storage_recency_settle_can_publish(era_host_peer_storage_recency_settle_result_t result) {
    return result == ERA_HOST_PEER_STORAGE_RECENCY_SETTLE_AT_BASELINE ||
           result == ERA_HOST_PEER_STORAGE_RECENCY_SETTLE_DEPARTED;
}

static inline bool era_host_peer_storage_recency_settle_departed(era_host_peer_storage_recency_settle_result_t result) {
    return result == ERA_HOST_PEER_STORAGE_RECENCY_SETTLE_DEPARTED;
}

static inline bool era_host_peer_storage_recency_convergence_can_retire(bool persistence_succeeded) {
    return persistence_succeeded;
}
