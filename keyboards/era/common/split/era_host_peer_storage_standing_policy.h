// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>

/* Small, host-testable composition for the standing-cadence storage gate.
 * Transfer exclusivity already suppresses the standing cadence. A push has one
 * additional window after transfer verification: the initiator is waiting for
 * the responder's synchronous Core0 Apply to finish. The storage control lane
 * remains live there, but reopening the 1/10 ms runtime cadence would feed
 * per-arrival runtime-push results into a responder whose Core0 cannot drain
 * them. Keep the two reasons separate in storage state and compose them here. */
static inline bool era_host_peer_storage_standing_policy_suppressed(bool route_exclusive,
                                                                   bool initiator_role,
                                                                   bool peer_push_apply,
                                                                   bool peer_push_complete) {
    return route_exclusive ||
           (initiator_role && (peer_push_apply || peer_push_complete));
}
