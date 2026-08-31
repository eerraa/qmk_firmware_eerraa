// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_communication_core_owner.h"

#include <stddef.h>
#include <string.h>

#include "hal.h"
#include "hardware/structs/timer.h"
#include "pico/platform.h"

#include "era_split_communication_core_lifecycle.h"
#include "../era_split_transaction_backend.h"
#include "../era_split_transaction_engine.h"

#if !defined(MCU_RP)
#    error "ERA split communication-core ownership requires RP2040."
#endif

#ifndef ERA_SPLIT_COMMUNICATION_CORE_OWNER_TRANSFER_TIMEOUT_US
#    define ERA_SPLIT_COMMUNICATION_CORE_OWNER_TRANSFER_TIMEOUT_US 30000U
#endif

/* R7: consecutive revoke-wait timeouts before core1 is judged dead. A live
 * core1 checks the revoke word at the top of every loop pass and publishes
 * release in microseconds; its whole design contract (bounded waits, no
 * flash, no ChibiOS suspends) leaves it nothing legal to do for one 30 ms
 * window, let alone two with an SEV in between. Two is therefore one window
 * of benefit-of-the-doubt over the contract, not a tuning knob. Before this
 * bound existed, a post-boot core1 death wedged the half mid-teardown; the
 * device later showed the wedge was harder than the traced "30 ms wait per
 * housekeeping pass forever" — the wait itself never returned, because its
 * deadline check sat behind a WFE wake a dead core1 never sends (the fix is
 * at owner_wait_word() below, with the breadcrumb evidence). */
#ifndef ERA_SPLIT_COMMUNICATION_CORE_OWNER_REVOKE_DEATH_STREAK
#    define ERA_SPLIT_COMMUNICATION_CORE_OWNER_REVOKE_DEATH_STREAK 2U
#endif

enum {
    ERA_SPLIT_COMMUNICATION_CORE_OWNER_WORD_OWNER_MASK = 0xFFU,
    ERA_SPLIT_COMMUNICATION_CORE_OWNER_WORD_EPOCH_LSB  = 8U,
    ERA_SPLIT_COMMUNICATION_CORE_OWNER_REVOKE           = 1U << 0,
    ERA_SPLIT_COMMUNICATION_CORE_OWNER_CANCEL           = 1U << 1,
    ERA_SPLIT_COMMUNICATION_CORE_OWNER_RESET            = 1U << 2,
};

typedef struct {
    volatile bool     initialized;
    volatile uint32_t lease_word __attribute__((aligned(4)));
    volatile uint32_t role_word __attribute__((aligned(4)));
    volatile uint32_t revoke_word __attribute__((aligned(4)));
    volatile uint32_t release_word __attribute__((aligned(4)));
    volatile uint32_t ready_word __attribute__((aligned(4)));
    volatile uint32_t transfer_count;
    volatile uint32_t revoke_count;
    volatile uint32_t release_count;
    volatile uint32_t ready_count;
    volatile uint32_t transfer_timeout_count;
    volatile uint32_t ready_timeout_count;
    volatile uint32_t init_fail_count;
    volatile uint32_t revoke_timeout_streak;
    volatile uint32_t reclaim_count;
} era_split_communication_core_owner_state_t;

static era_split_communication_core_owner_state_t g_era_split_communication_core_owner;

static uint32_t era_split_communication_core_owner_word(era_split_communication_core_backend_owner_t owner, uint16_t epoch) {
    return ((uint32_t)epoch << ERA_SPLIT_COMMUNICATION_CORE_OWNER_WORD_EPOCH_LSB) | (uint32_t)owner;
}

static uint32_t era_split_communication_core_owner_role_word(era_split_transaction_backend_role_t role, uint16_t epoch) {
    return ((uint32_t)epoch << ERA_SPLIT_COMMUNICATION_CORE_OWNER_WORD_EPOCH_LSB) | (uint32_t)role;
}

static era_split_transaction_backend_role_t era_split_communication_core_owner_word_role(uint32_t word) {
    return (era_split_transaction_backend_role_t)(word & ERA_SPLIT_COMMUNICATION_CORE_OWNER_WORD_OWNER_MASK);
}

static era_split_communication_core_backend_owner_t era_split_communication_core_owner_word_owner(uint32_t word) {
    return (era_split_communication_core_backend_owner_t)(word & ERA_SPLIT_COMMUNICATION_CORE_OWNER_WORD_OWNER_MASK);
}

static uint16_t era_split_communication_core_owner_word_epoch(uint32_t word) {
    return (uint16_t)(word >> ERA_SPLIT_COMMUNICATION_CORE_OWNER_WORD_EPOCH_LSB);
}

static uint16_t era_split_communication_core_owner_next_epoch(uint16_t epoch) {
    epoch++;
    return epoch == 0 ? 1 : epoch;
}

/* R7 fix, device-observed 2026-08-06 on the kill image's breadcrumb: this
 * wait used to park on __WFE() between deadline checks, which put the 30 ms
 * bound behind a wake that a dead core1 never sends. The main loop stalls
 * right here, so within milliseconds core0's own interrupt sources go quiet
 * too — no HID re-arm, no RGB DMA, no scheduled tick — and the wait never
 * returned at all: the wedged half read pos=RESET_SERIAL>REVOKE_WAIT frozen
 * with rto=0 after minutes, so the judgment below it was never reached. A
 * bounded wait must be bounded by construction, not by the peer's
 * cooperation: this is a cold-path wait whose healthy exit is microseconds
 * away, so it polls the deadline instead of sleeping toward it. */
static bool era_split_communication_core_owner_wait_word(volatile uint32_t *word, uint32_t expected) {
    uint32_t start_us = timer_hw->timerawl;
    while (*word != expected) {
        if ((uint32_t)(timer_hw->timerawl - start_us) >= ERA_SPLIT_COMMUNICATION_CORE_OWNER_TRANSFER_TIMEOUT_US) {
            return false;
        }
    }
    return true;
}

static void era_split_communication_core_owner_publish_release(era_split_communication_core_backend_owner_t owner, uint16_t epoch) {
    __DMB();
    g_era_split_communication_core_owner.release_word = era_split_communication_core_owner_word(owner, epoch);
    g_era_split_communication_core_owner.release_count++;
    __DMB();
    __SEV();
}

static void era_split_communication_core_owner_publish_ready(era_split_communication_core_backend_owner_t owner, uint16_t epoch) {
    __DMB();
    g_era_split_communication_core_owner.ready_word = era_split_communication_core_owner_word(owner, epoch);
    g_era_split_communication_core_owner.ready_count++;
    __DMB();
    __SEV();
}

void era_split_communication_core_owner_init(void) {
    if (g_era_split_communication_core_owner.initialized) {
        return;
    }

    memset(&g_era_split_communication_core_owner, 0, sizeof(g_era_split_communication_core_owner));
    g_era_split_communication_core_owner.initialized = true;
    __DMB();
}

era_split_communication_core_backend_owner_t era_split_communication_core_owner_current(void) {
    era_split_communication_core_owner_init();
    __DMB();
    return era_split_communication_core_owner_word_owner(g_era_split_communication_core_owner.lease_word);
}

uint16_t era_split_communication_core_owner_epoch(void) {
    era_split_communication_core_owner_init();
    __DMB();
    return era_split_communication_core_owner_word_epoch(g_era_split_communication_core_owner.lease_word);
}

era_split_transaction_backend_role_t era_split_communication_core_owner_current_role(void) {
    era_split_communication_core_owner_init();
    __DMB();
    uint32_t lease_word = g_era_split_communication_core_owner.lease_word;
    uint32_t role_word  = g_era_split_communication_core_owner.role_word;
    if (era_split_communication_core_owner_word_epoch(lease_word) != era_split_communication_core_owner_word_epoch(role_word)) {
        return ERA_SPLIT_TRANSACTION_BACKEND_ROLE_DISABLED;
    }
    return era_split_communication_core_owner_word_role(role_word);
}

era_split_communication_core_backend_access_t era_split_communication_core_owner_backend_access(uint16_t expected_epoch) {
    era_split_communication_core_owner_init();
    __DMB();
    uint32_t lease_word = g_era_split_communication_core_owner.lease_word;
    uint16_t epoch      = era_split_communication_core_owner_word_epoch(lease_word);
    era_split_communication_core_backend_owner_t owner = era_split_communication_core_owner_word_owner(lease_word);

    if (get_core_num() == 0 || owner != ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1) {
        return ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OWNER;
    }
    if (expected_epoch != 0 && expected_epoch != epoch) {
        return ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_EPOCH;
    }

    uint32_t revoke_word = g_era_split_communication_core_owner.revoke_word;
    if (era_split_communication_core_owner_word_epoch(revoke_word) != epoch) {
        return ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK;
    }
    uint8_t flags = (uint8_t)(revoke_word & ERA_SPLIT_COMMUNICATION_CORE_OWNER_WORD_OWNER_MASK);
    if ((flags & ERA_SPLIT_COMMUNICATION_CORE_OWNER_RESET) != 0) {
        return ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_RESET;
    }
    if ((flags & (ERA_SPLIT_COMMUNICATION_CORE_OWNER_CANCEL | ERA_SPLIT_COMMUNICATION_CORE_OWNER_REVOKE)) != 0) {
        return ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_CANCEL;
    }
    return ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK;
}


bool era_split_communication_core_owner_core1_service(void) {
    era_split_communication_core_owner_init();
    if (get_core_num() == 0) {
        return false;
    }

    __DMB();
    uint32_t lease_word = g_era_split_communication_core_owner.lease_word;
    uint16_t epoch      = era_split_communication_core_owner_word_epoch(lease_word);
    if (era_split_communication_core_owner_word_owner(lease_word) != ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1) {
        return false;
    }

    uint32_t expected_word = era_split_communication_core_owner_word(ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1, epoch);
    uint32_t role_word      = g_era_split_communication_core_owner.role_word;
    era_split_transaction_backend_role_t role = era_split_communication_core_owner_word_epoch(role_word) == epoch ?
                                                    era_split_communication_core_owner_word_role(role_word) :
                                                    ERA_SPLIT_TRANSACTION_BACKEND_ROLE_DISABLED;
    uint32_t revoke_word   = g_era_split_communication_core_owner.revoke_word;
    if (era_split_communication_core_owner_word_epoch(revoke_word) == epoch &&
        (revoke_word & ERA_SPLIT_COMMUNICATION_CORE_OWNER_REVOKE) != 0) {
        if (g_era_split_communication_core_owner.release_word != expected_word) {
            era_split_transaction_engine_release_driver();
            era_split_communication_core_owner_publish_release(ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1, epoch);
        }
        return false;
    }

    if (g_era_split_communication_core_owner.ready_word != expected_word) {
        if (g_era_split_communication_core_owner.release_word == expected_word) {
            return false;
        }
        if (role == ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR) {
            era_split_transaction_engine_init_initiator_driver();
        } else if (role == ERA_SPLIT_TRANSACTION_BACKEND_ROLE_RESPONDER) {
            era_split_transaction_engine_init_responder_driver();
        } else {
            g_era_split_communication_core_owner.init_fail_count++;
            era_split_transaction_engine_release_driver();
            era_split_communication_core_owner_publish_release(ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1, epoch);
            return false;
        }
        if (!era_split_transaction_backend_role_ready(role) ||
            era_split_communication_core_owner_backend_access(epoch) != ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK) {
            g_era_split_communication_core_owner.init_fail_count++;
            era_split_transaction_engine_release_driver();
            era_split_communication_core_owner_publish_release(ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1, epoch);
            return false;
        }
        era_split_communication_core_owner_publish_ready(ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1, epoch);
    }

    return era_split_communication_core_owner_backend_access(epoch) == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK;
}

bool era_split_communication_core_owner_transfer_role(era_split_communication_core_backend_owner_t next_owner, era_split_transaction_backend_role_t next_role) {
    era_split_communication_core_owner_init();
    bool role_valid = (next_owner == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE && next_role == ERA_SPLIT_TRANSACTION_BACKEND_ROLE_DISABLED) ||
                      (next_owner == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1 &&
                       (next_role == ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR || next_role == ERA_SPLIT_TRANSACTION_BACKEND_ROLE_RESPONDER));
    if (get_core_num() != 0 || next_owner > ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1 || !role_valid) {
        return false;
    }

    __DMB();
    uint32_t current_word = g_era_split_communication_core_owner.lease_word;
    uint16_t current_epoch = era_split_communication_core_owner_word_epoch(current_word);
    era_split_communication_core_backend_owner_t current_owner = era_split_communication_core_owner_word_owner(current_word);

    if (current_owner != ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE &&
        current_owner != ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1) {
        return false;
    }
    if (current_owner == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE &&
        next_owner == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE) {
        return true;
    }

    if (current_owner != ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE) {
        uint32_t release_word = era_split_communication_core_owner_word(current_owner, current_epoch);
        g_era_split_communication_core_owner.revoke_word =
            ((uint32_t)current_epoch << ERA_SPLIT_COMMUNICATION_CORE_OWNER_WORD_EPOCH_LSB) |
            ERA_SPLIT_COMMUNICATION_CORE_OWNER_REVOKE |
            ERA_SPLIT_COMMUNICATION_CORE_OWNER_CANCEL |
            ERA_SPLIT_COMMUNICATION_CORE_OWNER_RESET;
        g_era_split_communication_core_owner.revoke_count++;
        __DMB();
        __SEV();

        era_split_communication_core_wake();

        if (!era_split_communication_core_owner_wait_word(&g_era_split_communication_core_owner.release_word, release_word)) {
            g_era_split_communication_core_owner.transfer_timeout_count++;
            g_era_split_communication_core_owner.revoke_timeout_streak++;
            if (g_era_split_communication_core_owner.revoke_timeout_streak < ERA_SPLIT_COMMUNICATION_CORE_OWNER_REVOKE_DEATH_STREAK) {
                return false;
            }
            /* R7: core1 is judged dead — it has ignored consecutive revokes
               with SEV wakes across two 30 ms windows. Reset it now (which
               also clears `launched`, so the next CORE1 transfer runs the
               full handshake instead of a SEV wake against a corpse),
               reclaim the lease core0-side by falling through to the epoch
               bump below, and let the teardown CONCLUDE: the mode commits,
               the session forgets, the peer matrix flushes. The zombie's old
               epoch is dead the moment the lease word moves, so anything a
               wrongly-judged core did after this line is epoch-refused —
               and it was reset one line ago, so it does nothing at all. */
            era_split_communication_core_declare_dead();
            g_era_split_communication_core_owner.reclaim_count++;
            g_era_split_communication_core_owner.revoke_timeout_streak = 0;
        } else {
            g_era_split_communication_core_owner.revoke_timeout_streak = 0;
        }
    }

    uint16_t next_epoch = era_split_communication_core_owner_next_epoch(current_epoch);
    if (next_owner == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1 && !era_split_communication_core_start()) {
        g_era_split_communication_core_owner.init_fail_count++;
        return false;
    }

    g_era_split_communication_core_owner.ready_word = 0;
    __DMB();
    g_era_split_communication_core_owner.role_word = era_split_communication_core_owner_role_word(next_role, next_epoch);
    __DMB();
    g_era_split_communication_core_owner.lease_word = era_split_communication_core_owner_word(next_owner, next_epoch);
    g_era_split_communication_core_owner.transfer_count++;
    __DMB();
    g_era_split_communication_core_owner.revoke_word = 0;
    __DMB();
    __SEV();

    if (next_owner == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE) {
        return true;
    }

    uint32_t ready_word = era_split_communication_core_owner_word(next_owner, next_epoch);
    era_split_communication_core_wake();

    if (!era_split_communication_core_owner_wait_word(&g_era_split_communication_core_owner.ready_word, ready_word)) {
        g_era_split_communication_core_owner.ready_timeout_count++;
        /* R7 fix (2026-08-06): a launch whose core never publishes ready is a
           failure to bring core1 into service, whatever start() returned.
           Counting it here is what makes the per-boot cap reachable when a
           relaunched core1 keeps dying after a successful handshake, and the
           cap is what lets the scheduler stop wanting a wire role and commit
           LOCAL_NO_LINK. */
        era_split_communication_core_note_core1_service_timeout();
        return false;
    }
    era_split_communication_core_note_core1_serviced();
    return true;
}

/* True when the live owner lease is already a coherent CORE1 lease for `role`:
   owner is CORE1, the lease and role words agree on the current epoch, the
   role matches, Core1 has published ready for that exact lease, and no revoke
   is pending. This is the idempotence predicate shared by the ensure fast-path
   and the scheduler serial-reset guard; a false result means a genuine
   transfer/reinit is still required (owner mismatch, epoch/role skew, an
   unpublished ready after a rebuild timeout, or a pending revoke/teardown).
   Callers run owner_init() plus a DMB before reading the lease words. */
static bool era_split_communication_core_owner_lease_is_live_core1_role(era_split_transaction_backend_role_t role) {
    uint32_t lease_word  = g_era_split_communication_core_owner.lease_word;
    uint32_t role_word   = g_era_split_communication_core_owner.role_word;
    uint32_t ready_word  = g_era_split_communication_core_owner.ready_word;
    uint32_t revoke_word = g_era_split_communication_core_owner.revoke_word;
    return era_split_communication_core_owner_word_owner(lease_word) == ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1 &&
           era_split_communication_core_owner_word_epoch(lease_word) == era_split_communication_core_owner_word_epoch(role_word) &&
           era_split_communication_core_owner_word_role(role_word) == role &&
           ready_word == lease_word &&
           revoke_word == 0;
}

static bool era_split_communication_core_owner_ensure_core1_role(era_split_transaction_backend_role_t role) {
    era_split_communication_core_owner_init();
    __DMB();
    if (era_split_communication_core_owner_lease_is_live_core1_role(role)) {
        /* R7 fix review finding (2026-08-06): a live serviced lease observed
           here is the same "core1 in service" fact the full-transfer path
           reports, and it must clear the give-up streak too — a ready that
           arrived after its 30 ms wait already counted a service timeout is
           recovered through this fast path, and without the clear those
           marginal counts would accumulate across the boot toward a false
           cap. The call is one byte read when the streak is already zero. */
        era_split_communication_core_note_core1_serviced();
        return true;
    }

    return era_split_communication_core_owner_transfer_role(ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1, role);
}

bool era_split_communication_core_owner_ensure_core1(void) {
    return era_split_communication_core_owner_ensure_core1_role(ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR);
}


bool era_split_communication_core_owner_core1_role_is_live(era_split_transaction_backend_role_t role) {
    era_split_communication_core_owner_init();
    __DMB();
    return era_split_communication_core_owner_lease_is_live_core1_role(role);
}

void era_split_communication_core_owner_get_diagnostics_snapshot(era_split_communication_core_owner_diagnostics_t *snapshot) {
    era_split_communication_core_owner_init();
    if (snapshot == NULL) {
        return;
    }

    __DMB();
    uint32_t lease_word   = g_era_split_communication_core_owner.lease_word;
    uint32_t revoke_word  = g_era_split_communication_core_owner.revoke_word;
    uint16_t owner_epoch  = era_split_communication_core_owner_word_epoch(lease_word);
    uint8_t  revoke_flags = era_split_communication_core_owner_word_epoch(revoke_word) == owner_epoch ? (uint8_t)revoke_word : 0;
    *snapshot = (era_split_communication_core_owner_diagnostics_t){
        .owner                  = (uint8_t)era_split_communication_core_owner_word_owner(lease_word),
        .backend_role           = (uint8_t)era_split_communication_core_owner_current_role(),
        .revoke_pending         = (revoke_flags & ERA_SPLIT_COMMUNICATION_CORE_OWNER_REVOKE) != 0,
        .cancel_pending         = (revoke_flags & ERA_SPLIT_COMMUNICATION_CORE_OWNER_CANCEL) != 0,
        .reset_pending          = (revoke_flags & ERA_SPLIT_COMMUNICATION_CORE_OWNER_RESET) != 0,
        .owner_epoch            = owner_epoch,
        .released_epoch         = era_split_communication_core_owner_word_epoch(g_era_split_communication_core_owner.release_word),
        .ready_epoch            = era_split_communication_core_owner_word_epoch(g_era_split_communication_core_owner.ready_word),
        .transfer_count         = g_era_split_communication_core_owner.transfer_count,
        .revoke_count           = g_era_split_communication_core_owner.revoke_count,
        .release_count          = g_era_split_communication_core_owner.release_count,
        .ready_count            = g_era_split_communication_core_owner.ready_count,
        .transfer_timeout_count = g_era_split_communication_core_owner.transfer_timeout_count,
        .ready_timeout_count    = g_era_split_communication_core_owner.ready_timeout_count,
        .init_fail_count        = g_era_split_communication_core_owner.init_fail_count,
        .reclaim_count          = g_era_split_communication_core_owner.reclaim_count,
    };
    __DMB();
}
