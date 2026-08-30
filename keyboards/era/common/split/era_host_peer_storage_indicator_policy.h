// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    ERA_HOST_PEER_STORAGE_INDICATOR_PEER_PENDING       = 1U << 0,
    ERA_HOST_PEER_STORAGE_INDICATOR_GATE               = 1U << 1,
    /* The last local pending=1 that this wire role has actually confirmed on
     * the peer-facing carrier: STORAGE_PENDING on an initiator, STORAGE_NEWS
     * bit7 on a responder. Keeping the sent level, rather than a "fall was
     * published" latch, matters when a short local 1 never reached the peer at
     * all: an already-confirmed zero then needs no new zero transaction and
     * must not leave a synthetic hold behind. */
    ERA_HOST_PEER_STORAGE_INDICATOR_LOCAL_SENT_PENDING = 1U << 2,
};

/* Small, host-testable boundary for the EEPROM SYNC indicator's relation
 * continuity. A confirmed serviced relation obviously carries the peer mirror.
 * A relation that is temporarily unclassified while the scheduler is still in
 * its fast recovery window carries the same unfinished pair operation and must
 * keep the mirror too. Once recovery has backed off, LOCAL_NO_LINK is a real
 * presentation departure and the mirror may retire. */
static inline bool era_host_peer_storage_indicator_relation_continuous(bool relation_serviced, bool fast_recovery_active) {
    return relation_serviced || fast_recovery_active;
}

/* Called from core0 only after the active carrier reports the value core1
 * successfully sent: the standing latest-state for an initiator or the
 * responder-result sent-shadow commit for a responder. The bit is a level: one
 * means the peer may still hold our 1 and therefore keeps this panel up even if
 * the local semantic arm has already fallen; zero retires that obligation. */
static inline uint8_t era_host_peer_storage_indicator_note_local_sent(uint8_t indicator_bits, bool pending) {
    if (!pending) {
        indicator_bits &= (uint8_t)~ERA_HOST_PEER_STORAGE_INDICATOR_LOCAL_SENT_PENDING;
    } else if ((indicator_bits & ERA_HOST_PEER_STORAGE_INDICATOR_GATE) != 0) {
        indicator_bits |= ERA_HOST_PEER_STORAGE_INDICATOR_LOCAL_SENT_PENDING;
    }
    return indicator_bits;
}

static inline bool era_host_peer_storage_indicator_pair_pending(uint8_t indicator_bits, bool advertised_pending) {
    return advertised_pending ||
           (indicator_bits & (ERA_HOST_PEER_STORAGE_INDICATOR_PEER_PENDING |
                              ERA_HOST_PEER_STORAGE_INDICATOR_LOCAL_SENT_PENDING)) != 0;
}
