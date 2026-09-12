// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_link.h"

#include <string.h>

#include "../storage/era_eeprom_driver.h"
#include "era_split_transport_scheduler.h"
#include "communication_core/era_split_communication_core_responder.h"
#include "era_split_restart_agreement.h"
#include "timer.h"

/* The four-byte record keeps its layout. Bit 0 of flags was an informational
 * unagreed mark: accept existing records, but never make it policy or write it
 * again. The agreed rendezvous and the durable local outcome are distinct
 * facts; this record has no bilateral durability vote. */
#define ERA_SPLIT_LINK_STORAGE_FLAG_MASK 0x01

typedef struct {
    uint8_t level;
    uint8_t flags;
    uint8_t reserved[2];
} era_split_link_storage_t;
_Static_assert(sizeof(era_split_link_storage_t) == ERA_EEPROM_LINK_CONFIG_SIZE, "The link block must fill its EEPROM region exactly.");

/* **What is left here is the level itself.** The two-phase agreement that used
 * to live in this file is era_split_restart_agreement.c, which took the state
 * machine, the wire, the quiet gate and the deadline with it. What stays is what
 * a link rate actually is on one half: last confirmed durable storage, the
 * configured wire rate, and the RAM selection not yet handed to an agreement,
 * and Reconciliation -- boot Low, the winner's raise, the fallback, the
 * listener's recovery step. Every durable write belongs to the checked act. */
/* Runtime convergence is not transaction ownership. The agreement alone owns
 * every accepted intent; this phase only gates boot reconciliation/recovery. */
typedef enum {
    ERA_SPLIT_LINK_RECONCILE,
    ERA_SPLIT_LINK_CONFIRMING,
    ERA_SPLIT_LINK_IDLE,
    ERA_SPLIT_LINK_RECOVERING,
} era_split_link_phase_t;

static struct {
    bool    stored_cached;
    uint8_t stored_level;

    bool    running_initialized;
    uint8_t running_level;

    bool    pending_cached;
    uint8_t pending_level;

    bool relation_serviced;
    bool wire_listening;
    bool local_is_rate_winner;

    bool     scan_valid;
    uint32_t scan_since_ms;
    uint32_t scan_accepted_at_open;
    uint32_t scan_undecodable_at_open;

    era_split_link_phase_t phase;
    bool     fallback_latched;
    bool     reconcile_search_stepped;
    bool     recovery_report_pending;
    uint32_t recovery_report_requested_ms;
    bool     recovery_report_active;
    bool     recovery_report_cancelled;
    uint32_t recovery_report_start_ms;
    uint32_t upgrade_confirm_since_ms;
    bool     serviced_since_valid;
    uint32_t serviced_since_ms;
    /* The running level the pair last agreed through a successful checked act
     * on this half. It is what makes reconciliation once per meeting rather
     * than once per relation generation: a same-pair reopen at this level has
     * nothing to agree. A recovery or listener step that moves the running
     * level off it retires it (era_split_link_note_level_selected). */
    bool     agreed_level_valid;
    uint8_t  agreed_level;
    /* Presentation receipt for the requesting half only. The agreement owns
     * execution; this observer never retries, selects or persists a level. */
    era_split_link_apply_status_t apply_status;
    uint8_t  apply_target;
    bool     apply_confirming;
    bool     apply_report_active;
    uint32_t apply_report_since_ms;
} g_era_split_link;

static void era_split_link_apply_feedback_set(era_split_link_apply_status_t status) {
    g_era_split_link.apply_status = status;
    /* Pending is readback state, not an animation or a claimed success. */
    g_era_split_link.apply_report_active = status != ERA_SPLIT_LINK_APPLY_PENDING && status != ERA_SPLIT_LINK_APPLY_NONE;
    g_era_split_link.apply_report_since_ms = timer_read32();
}

static void era_split_link_apply_feedback_task(void) {
    /* The checked local result is already visible. Observation can correct a
     * later failure, but successful observation must not re-arm the flash.
     * A new rate act retires this association before doing any work. */
    if (g_era_split_link.apply_confirming && g_era_split_link.phase != ERA_SPLIT_LINK_CONFIRMING) {
        g_era_split_link.apply_confirming = false;
        if (g_era_split_link.fallback_latched) {
            era_split_link_apply_feedback_set(ERA_SPLIT_LINK_APPLY_FAILED);
        }
    }
    if (g_era_split_link.apply_status == ERA_SPLIT_LINK_APPLY_PENDING &&
        !era_split_restart_agreement_holds_intent(ERA_SPLIT_RESTART_ACT_LINK_SPEED, g_era_split_link.apply_target)) {
        /* An expired, disarmed or superseded intent is not success, even if
         * an unrelated later act happens to have a successful global result. */
        era_split_link_apply_feedback_set(ERA_SPLIT_LINK_APPLY_CANCELLED);
    }
}

static bool era_split_link_load(void) {
    if (g_era_split_link.stored_cached) {
        return true;
    }
    /* Even a cold physical replay may be variable-latency. A received peer
     * arm must reach its divider transition before this lane touches NVM. */
    if (era_split_restart_agreement_timed_window()) {
        return false;
    }
    era_split_link_storage_t block;
    /* A void QMK read can return zero on NOT_READY. Do not cache that as a
     * durable High: this is the same parser a fresh boot uses, not a RAM echo. */
    if (era_eeprom_driver_replay_read(ERA_EEPROM_CONFIG_ADDR + ERA_EEPROM_LINK_CONFIG_OFFSET,
                                    &block, sizeof(block)) != ERA_NVM_RESULT_OK) {
        return false;
    }
    uint8_t level = ERA_SPLIT_LINK_LEVEL_HIGH;
    if (block.level <= ERA_SPLIT_LINK_LEVEL_LOW && (block.flags & (uint8_t)~ERA_SPLIT_LINK_STORAGE_FLAG_MASK) == 0 &&
        block.reserved[0] == 0 && block.reserved[1] == 0) {
        level = block.level;
    }
    g_era_split_link.stored_level  = level;
    g_era_split_link.stored_cached = true;
    return true;
}

static bool era_split_link_store(uint8_t level) {
    const era_split_link_storage_t block = {
        .level = level,
        .flags = 0,
    };
    era_nvm_result_t result = era_eeprom_driver_replace(
        ERA_EEPROM_CONFIG_ADDR + ERA_EEPROM_LINK_CONFIG_OFFSET, &block, sizeof(block), ERA_NVM_ORIGIN_LOCAL_QMK);
    if (result != ERA_NVM_RESULT_OK && result != ERA_NVM_RESULT_NO_CHANGE) {
        return false;
    }
    /* The result-bearing boundary publishes its RAM image only after durable
     * commit. These caches must follow that result, never the requested value. */
    g_era_split_link.stored_level   = level;
    g_era_split_link.stored_cached  = true;
    g_era_split_link.pending_level  = level;
    g_era_split_link.pending_cached = true;
    return true;
}

uint8_t era_split_link_active_level(void) {
    /* Selecting or comparing a divider must never replay NVM before T_commit.
     * Stored configuration has its own cold load; boot wire is always Low. */
    if (!g_era_split_link.running_initialized) {
        g_era_split_link.running_level       = ERA_SPLIT_LINK_LEVEL_LOW;
        g_era_split_link.running_initialized = true;
    }
    return g_era_split_link.running_level;
}

uint32_t era_split_link_speed(uint8_t level) {
    switch (level) {
        case ERA_SPLIT_LINK_LEVEL_MEDIUM:
            return ERA_SPLIT_LINK_SPEED_MEDIUM;
        case ERA_SPLIT_LINK_LEVEL_LOW:
            return ERA_SPLIT_LINK_SPEED_LOW;
        default:
            return ERA_SPLIT_LINK_SPEED_HIGH;
    }
}

uint8_t era_split_link_pending_level(void) {
    if (!g_era_split_link.pending_cached) {
        /* Seeded from the *stored* level and not the running one, so a half
         * still at boot Low shows the owner what they chose. */
        if (!era_split_link_load()) {
            return ERA_SPLIT_LINK_LEVEL_HIGH;
        }
        g_era_split_link.pending_level  = g_era_split_link.stored_level;
        g_era_split_link.pending_cached = true;
    }
    return g_era_split_link.pending_level;
}

bool era_split_link_set_pending_level(uint8_t level) {
    if (level > ERA_SPLIT_LINK_LEVEL_LOW) {
        return false;
    }
    (void)era_split_link_pending_level();
    g_era_split_link.pending_level  = level;
    g_era_split_link.pending_cached = true;
    return true;
}

bool era_split_link_request_apply(void) {
    era_split_link_apply_feedback_task();
    if (era_split_restart_agreement_in_flight() ||
        g_era_split_link.phase == ERA_SPLIT_LINK_CONFIRMING) {
        if (g_era_split_link.apply_status != ERA_SPLIT_LINK_APPLY_PENDING && !g_era_split_link.apply_confirming) {
            era_split_link_apply_feedback_set(ERA_SPLIT_LINK_APPLY_BUSY);
        }
        return false;
    }
    if (!era_split_link_load()) {
        era_split_link_apply_feedback_set(ERA_SPLIT_LINK_APPLY_FAILED);
        return false;
    }
    uint8_t level = era_split_link_pending_level();
    g_era_split_link.apply_target = level;
    /* A runtime-success/storage-failure must remain explicitly retryable at
     * the same running level. Inert means both running AND durable agree. */
    if (level == era_split_link_active_level() && level == g_era_split_link.stored_level) {
        era_split_link_apply_feedback_set(ERA_SPLIT_LINK_APPLY_UNCHANGED);
        return false;
    }
    if (!era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_SPEED, level)) {
        era_split_link_apply_feedback_set(ERA_SPLIT_LINK_APPLY_BUSY);
        return false;
    }
    g_era_split_link.apply_confirming = false;
    era_split_link_apply_feedback_set(ERA_SPLIT_LINK_APPLY_PENDING);
    /* The reconciliation decision is spent, not duplicated: only the
     * agreement owns the request from here through commit or abort. */
    g_era_split_link.phase            = ERA_SPLIT_LINK_IDLE;
    g_era_split_link.fallback_latched = false;
    return true;
}

bool era_split_link_apply(uint8_t level) {
    if (level > ERA_SPLIT_LINK_LEVEL_LOW) {
        return false;
    }
    /* Do not attach a later peer/boot act's health check to an old receipt. */
    g_era_split_link.apply_confirming = false;
    bool requested_here = g_era_split_link.apply_status == ERA_SPLIT_LINK_APPLY_PENDING;
    if (requested_here && g_era_split_link.apply_target != level) {
        era_split_link_apply_feedback_set(ERA_SPLIT_LINK_APPLY_CANCELLED);
        requested_here = false;
    }
    bool changed = era_split_link_active_level() != level;
    /* T_commit is a physical transition, not a flash-work start time. The
     * scheduler also restores the new epoch's standing plan/responder snapshot
     * before returning, so Core1 can service the link during this NVM write. */
    if (!era_split_transport_scheduler_apply_link_level(level)) {
        g_era_split_link.phase            = ERA_SPLIT_LINK_RECOVERING;
        g_era_split_link.fallback_latched = true;
        g_era_split_link.recovery_report_pending = false;
        g_era_split_link.recovery_report_active = false;
        g_era_split_link.recovery_report_cancelled = true;
        if (requested_here) {
            era_split_link_apply_feedback_set(ERA_SPLIT_LINK_APPLY_FAILED);
        }
        return false;
    }
    /* A newly accepted transition replaces the previous recovery attempt,
     * regardless of which half requested it. No stale local-only latch. */
    g_era_split_link.fallback_latched = false;
    /* The runtime rendezvous is the agreement; durable storage below is this
     * half's own result and never reopens reconciliation (header: Failure). */
    g_era_split_link.agreed_level_valid = true;
    g_era_split_link.agreed_level       = level;
    g_era_split_link.phase = changed && g_era_split_link.relation_serviced ? ERA_SPLIT_LINK_CONFIRMING : ERA_SPLIT_LINK_IDLE;
    g_era_split_link.upgrade_confirm_since_ms = timer_read32();
    /* Success means runtime ready AND this half durably stored the parameter.
     * On storage failure retain the new runtime and the last confirmed stored
     * cache; no unilateral rollback and no later implicit persistence retry. */
    bool stored = era_split_link_store(level);
    if (requested_here) {
        /* Both checks have completed before success is observable. The NVM
         * call is synchronous on the renderer's core, so announcing before
         * it buys no earlier LED frame and would lie on a failed write. */
        era_split_link_apply_feedback_set(stored ? ERA_SPLIT_LINK_APPLY_APPLIED : ERA_SPLIT_LINK_APPLY_FAILED);
        g_era_split_link.apply_confirming = stored && g_era_split_link.phase == ERA_SPLIT_LINK_CONFIRMING;
    }
    return stored;
}

void era_split_link_note_relation(bool serviced, bool listening, bool local_is_rate_winner, bool fresh_meeting,
                                  bool peer_rate_searched, bool local_is_initiator) {
    bool winner_changed = local_is_rate_winner != g_era_split_link.local_is_rate_winner;
    if (serviced && (!g_era_split_link.relation_serviced || winner_changed)) {
        g_era_split_link.serviced_since_ms    = timer_read32();
        g_era_split_link.serviced_since_valid = true;
        if (!g_era_split_link.relation_serviced &&
            (g_era_split_link.reconcile_search_stepped || peer_rate_searched)) {
            g_era_split_link.recovery_report_cancelled = false;
            /* The settled initiator alone requests the report, from its own
             * search or the listener's answer. A Left HOST is discovery's
             * talker but becomes responder: requesting here from that HOST
             * can put LINK_RECOVERED straight after LINK_SPEED without an
             * observable idle, which the peer's replay guard must suppress.
             * Keep that guard; the side that arms already knows the fact. */
            g_era_split_link.reconcile_search_stepped = false;
            if (local_is_initiator) {
                g_era_split_link.recovery_report_pending = true;
                g_era_split_link.recovery_report_requested_ms = timer_read32();
            }
        }
        /* One agreement per meeting (header: Reconciliation). A same-pair
         * reopen inside the scheduler's fast recovery window -- the SESSION
         * revalidation every EEPROM SYNC push close forces -- arrives with
         * fresh_meeting false, and a pair already agreed at this running level
         * has nothing to agree there. A winner that never completed its
         * agreement (aborted by that same churn) still owes it and retries. */
        bool agreed_here = g_era_split_link.agreed_level_valid &&
                           g_era_split_link.agreed_level == era_split_link_active_level();
        if ((fresh_meeting || winner_changed || !agreed_here) &&
            g_era_split_link.phase == ERA_SPLIT_LINK_IDLE && !g_era_split_link.fallback_latched &&
            !era_split_restart_agreement_in_flight()) {
            g_era_split_link.phase = ERA_SPLIT_LINK_RECONCILE;
        }
    }
    if (!serviced) {
        g_era_split_link.recovery_report_pending = false;
        g_era_split_link.serviced_since_valid = false;
        if (!listening) {
            /* A listener-search episode ended without opening a relation. Do
             * not let unrelated later service inherit its success marker. */
            g_era_split_link.reconcile_search_stepped = false;
        }
    }
    g_era_split_link.relation_serviced    = serviced;
    g_era_split_link.wire_listening       = listening;
    g_era_split_link.local_is_rate_winner = local_is_rate_winner;
}

bool era_split_link_rate_searched(void) {
    return g_era_split_link.reconcile_search_stepped;
}

static void era_split_link_scan_open(uint32_t now_ms, uint32_t accepted, uint32_t undecodable) {
    g_era_split_link.scan_valid               = true;
    g_era_split_link.scan_since_ms            = now_ms;
    g_era_split_link.scan_accepted_at_open    = accepted;
    g_era_split_link.scan_undecodable_at_open = undecodable;
}

bool era_split_link_step_due(uint8_t *next_level) {
    if (next_level == NULL || era_split_restart_agreement_in_flight()) {
        return false;
    }
    (void)era_split_link_active_level();
    if (g_era_split_link.phase == ERA_SPLIT_LINK_RECOVERING && g_era_split_link.running_level != ERA_SPLIT_LINK_LEVEL_LOW) {
        *next_level = ERA_SPLIT_LINK_LEVEL_LOW;
        return true;
    }

    if (g_era_split_link.relation_serviced || !g_era_split_link.wire_listening) {
        /* A talker, an unavailable wire, or an open relation: none of them
         * listens for anything, and a window kept across the change would be
         * about a state that no longer exists. */
        g_era_split_link.scan_valid = false;
        return false;
    }

    /* Two core1 counters, read as plain words the way the responder-silence
     * watch reads the first of them (scheduler/era_split_transport_scheduler_timing.c):
     * each is a monotonic uint32 that core1 alone writes, so a stale read
     * costs one housekeeping pass of latency and nothing else. */
    uint32_t accepted    = era_split_communication_core_responder_accepted_rx_count();
    uint32_t undecodable = era_split_communication_core_responder_undecodable_rx_count();
    uint32_t now_ms      = timer_read32();
    if (!g_era_split_link.scan_valid) {
        era_split_link_scan_open(now_ms, accepted, undecodable);
        return false;
    }
    if (timer_elapsed32(g_era_split_link.scan_since_ms) < ERA_SPLIT_LINK_SCAN_DWELL_MS) {
        return false;
    }

    /* The window is judged once, at its end. An accepted frame anywhere in it
     * is a talker this half can hear -- the right level, whatever else arrived
     * -- and the answer is to stay and open a fresh window. Noise without one
     * is a talker it cannot hear, and the answer is the next level on the
     * ring. Silence is neither and also stays: there is nobody to follow. */
    bool heard_a_frame = accepted != g_era_split_link.scan_accepted_at_open;
    bool heard_noise   = (uint32_t)(undecodable - g_era_split_link.scan_undecodable_at_open) >= ERA_SPLIT_LINK_SCAN_NOISE_MIN;
    if (heard_a_frame || !heard_noise) {
        era_split_link_scan_open(now_ms, accepted, undecodable);
        return false;
    }
    g_era_split_link.reconcile_search_stepped = true;
    *next_level = g_era_split_link.running_level == ERA_SPLIT_LINK_LEVEL_LOW ? ERA_SPLIT_LINK_LEVEL_HIGH : (uint8_t)(g_era_split_link.running_level + 1U);
    return true;
}

void era_split_link_note_level_selected(uint8_t level) {
    /* This is the configured divider, not proof of an available wire lease.
     * A failed subsequent restart is repaired by the scheduler dirty word. */
    g_era_split_link.running_initialized = true;
    g_era_split_link.running_level       = level;
    g_era_split_link.scan_valid          = false;
    /* A divider that moved off the agreed level -- a fallback or a listener
     * step -- retires the agreement; the checked act that follows an agreed
     * transition re-latches it (era_split_link_apply). */
    if (g_era_split_link.agreed_level_valid && g_era_split_link.agreed_level != level) {
        g_era_split_link.agreed_level_valid = false;
    }
    if (g_era_split_link.phase == ERA_SPLIT_LINK_RECOVERING && level == ERA_SPLIT_LINK_LEVEL_LOW) {
        g_era_split_link.phase = ERA_SPLIT_LINK_IDLE;
    }
}

bool era_split_link_runtime_settled(void) {
    if (era_split_restart_agreement_in_flight()) {
        return false;
    }
    return !g_era_split_link.relation_serviced || g_era_split_link.phase == ERA_SPLIT_LINK_IDLE;
}

void era_split_link_task(void) {
    /* Recovery/liveness never depends on storage readiness. A peer can commit
     * while this half's durable record has not even been loaded successfully. */
    (void)era_split_link_active_level();
    if (g_era_split_link.phase == ERA_SPLIT_LINK_CONFIRMING &&
        timer_elapsed32(g_era_split_link.upgrade_confirm_since_ms) >= ERA_SPLIT_LINK_UPGRADE_CONFIRM_MS) {
        if (g_era_split_link.relation_serviced) {
            g_era_split_link.phase = ERA_SPLIT_LINK_IDLE;
        } else {
            g_era_split_link.phase            = ERA_SPLIT_LINK_RECOVERING;
            g_era_split_link.fallback_latched = true;
            g_era_split_link.recovery_report_pending = false;
            g_era_split_link.recovery_report_active = false;
            g_era_split_link.recovery_report_cancelled = true;
        }
    }
    if (g_era_split_link.phase == ERA_SPLIT_LINK_RECOVERING &&
        g_era_split_link.running_level == ERA_SPLIT_LINK_LEVEL_LOW) {
        g_era_split_link.phase = ERA_SPLIT_LINK_IDLE;
    }
    /* Observe this act's terminal boundary before a report or a subsequent
     * automatic request can replace the agreement's latest result. */
    era_split_link_apply_feedback_task();
    /* Prepare the report during the tail of rate confirmation, not after it.
     * Its existing commit lead covers the remaining observation window. The
     * local confirmation still gates visibility and a failure cancels it;
     * neither the recovery event nor its shared epoch is guessed from a rate
     * act. No presentation-only request waits for VIA silence. */
    if (g_era_split_link.recovery_report_pending) {
        const uint32_t report_prepare_after_ms = ERA_SPLIT_LINK_UPGRADE_CONFIRM_MS > ERA_SPLIT_RESTART_COMMIT_DELAY_MS
                                                    ? ERA_SPLIT_LINK_UPGRADE_CONFIRM_MS - ERA_SPLIT_RESTART_COMMIT_DELAY_MS
                                                    : 0;
        bool report_ready = g_era_split_link.phase == ERA_SPLIT_LINK_IDLE ||
                            (g_era_split_link.phase == ERA_SPLIT_LINK_CONFIRMING &&
                             timer_elapsed32(g_era_split_link.upgrade_confirm_since_ms) >= report_prepare_after_ms);
        if (timer_elapsed32(g_era_split_link.recovery_report_requested_ms) >= ERA_SPLIT_RESTART_REQUEST_LIFETIME_MS) {
            g_era_split_link.recovery_report_pending = false;
        } else if (g_era_split_link.relation_serviced && report_ready &&
                   !era_split_restart_agreement_in_flight() &&
                   era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_RECOVERED, 0)) {
            g_era_split_link.recovery_report_pending = false;
        }
    }
    if (!g_era_split_link.relation_serviced || g_era_split_link.fallback_latched ||
        g_era_split_link.phase != ERA_SPLIT_LINK_RECONCILE || era_split_restart_agreement_in_flight()) {
        return;
    }
    if (!era_split_link_load()) {
        return;
    }
    if (g_era_split_link.local_is_rate_winner) {
        /* Even a Low/unchanged target goes through the same agreement. Silence
         * is not proof that the winner chose Low, and a guess must never write
         * the loser's durable record outside a checked LINK transaction. */
        if (era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_SPEED, g_era_split_link.stored_level)) {
            g_era_split_link.phase = ERA_SPLIT_LINK_IDLE;
        }
        return;
    }
    if (g_era_split_link.serviced_since_valid &&
        timer_elapsed32(g_era_split_link.serviced_since_ms) >= ERA_SPLIT_LINK_UPGRADE_WAIT_MS) {
        /* Bound the initial storage wait when no usable request arrives.
         * Nothing was agreed: release the gate without persisting a guess. */
        g_era_split_link.phase = ERA_SPLIT_LINK_IDLE;
    }
}

typedef struct {
    uint16_t ms;
    uint8_t  on;
} era_split_link_report_step_t;

era_split_link_apply_status_t era_split_link_apply_status(void) {
    return g_era_split_link.apply_status;
}

uint8_t era_split_link_apply_target(void) {
    return g_era_split_link.apply_target;
}

bool era_split_link_get_stored_level(uint8_t *level) {
    if (level == NULL || !era_split_link_load()) {
        return false;
    }
    *level = g_era_split_link.stored_level;
    return true;
}

bool era_split_link_apply_report_advance(era_split_link_apply_status_t *status, bool *on) {
    bool lit = false;
    if (g_era_split_link.apply_report_active) {
        uint32_t elapsed = timer_elapsed32(g_era_split_link.apply_report_since_ms);
        /* One terminal pulse for success (including no change) or failure.
         * No progress cadence and no minimum animation before completion. */
        lit = elapsed < 640U;
        if (elapsed >= 640U + 160U) {
            g_era_split_link.apply_report_active = false;
        }
    }
    if (status != NULL) {
        *status = g_era_split_link.apply_status;
    }
    if (on != NULL) {
        *on = lit;
    }
    return g_era_split_link.apply_report_active;
}

/* Three long pulses, then a tail so the end is not a fourth off. Cost is per
   transition: a held STATUS frame that is ACTIVE and not DIRTY neither
   renders nor flushes. */
static const era_split_link_report_step_t era_split_link_fallback_report_steps[] = {
    {ERA_SPLIT_LINK_FALLBACK_REPORT_ON_MS, 1},
    {ERA_SPLIT_LINK_FALLBACK_REPORT_OFF_MS, 0},
    {ERA_SPLIT_LINK_FALLBACK_REPORT_ON_MS, 1},
    {ERA_SPLIT_LINK_FALLBACK_REPORT_OFF_MS, 0},
    {ERA_SPLIT_LINK_FALLBACK_REPORT_ON_MS, 1},
    {ERA_SPLIT_LINK_FALLBACK_REPORT_TAIL_MS, 0},
};

#define ERA_SPLIT_LINK_FALLBACK_REPORT_STEP_COUNT \
    (sizeof(era_split_link_fallback_report_steps) / sizeof(era_split_link_fallback_report_steps[0]))

static struct {
    bool     started;
    bool     running;
    uint8_t  step;
    uint32_t step_start_ms;
} g_era_split_link_fallback_report;

bool era_split_link_fallback_report_advance(bool *on) {
    bool lit = false;

    if (!g_era_split_link_fallback_report.started) {
        if (g_era_split_link.fallback_latched) {
            g_era_split_link_fallback_report.started       = true;
            g_era_split_link_fallback_report.running       = true;
            g_era_split_link_fallback_report.step          = 0;
            g_era_split_link_fallback_report.step_start_ms = timer_read32();
        }
    } else if (g_era_split_link_fallback_report.running) {
        uint32_t now_ms = timer_read32();
        while (g_era_split_link_fallback_report.running &&
               (uint32_t)(now_ms - g_era_split_link_fallback_report.step_start_ms) >=
                   era_split_link_fallback_report_steps[g_era_split_link_fallback_report.step].ms) {
            g_era_split_link_fallback_report.step_start_ms +=
                era_split_link_fallback_report_steps[g_era_split_link_fallback_report.step].ms;
            g_era_split_link_fallback_report.step++;
            if (g_era_split_link_fallback_report.step >= (uint8_t)ERA_SPLIT_LINK_FALLBACK_REPORT_STEP_COUNT) {
                g_era_split_link_fallback_report.running = false;
            }
        }
    }

    if (g_era_split_link_fallback_report.running) {
        lit = era_split_link_fallback_report_steps[g_era_split_link_fallback_report.step].on != 0;
    }
    if (on != NULL) {
        *on = lit;
    }
    return g_era_split_link_fallback_report.running;
}

static const era_split_link_report_step_t era_split_link_reconcile_success_report_steps[] = {
    {ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_ON_MS, 1},
    {ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_OFF_MS, 0},
    {ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_ON_MS, 1},
    {ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_TAIL_MS, 0},
};

#define ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_STEP_COUNT \
    (sizeof(era_split_link_reconcile_success_report_steps) / sizeof(era_split_link_reconcile_success_report_steps[0]))

bool era_split_link_reconcile_success_report_commit(void) {
    uint32_t start_ms;
    if (g_era_split_link.recovery_report_cancelled || !era_split_restart_agreement_agreed_deadline(&start_ms)) {
        return false;
    }
    g_era_split_link.recovery_report_start_ms = start_ms;
    g_era_split_link.recovery_report_active = true;
    return true;
}

bool era_split_link_reconcile_success_report_advance(bool *on) {
    bool active = false;
    bool lit = false;
    if (g_era_split_link.recovery_report_active && g_era_split_link.phase != ERA_SPLIT_LINK_CONFIRMING) {
        int32_t elapsed = (int32_t)(timer_read32() - g_era_split_link.recovery_report_start_ms);
        if (elapsed >= 0) {
            /* Compute phase from the agreed instant, never from a renderer's
             * first call or a delayed task dispatch. Missed/hidden steps are
             * skipped, not replayed; uint32 wrap keeps the same elapsed time. */
            uint32_t remaining = (uint32_t)elapsed;
            for (uint8_t step = 0; step < ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_STEP_COUNT; ++step) {
                if (remaining < era_split_link_reconcile_success_report_steps[step].ms) {
                    active = true;
                    lit = era_split_link_reconcile_success_report_steps[step].on != 0;
                    break;
                }
                remaining -= era_split_link_reconcile_success_report_steps[step].ms;
            }
            if (!active) {
                g_era_split_link.recovery_report_active = false;
            }
        }
    }
    if (on != NULL) {
        *on = lit;
    }
    return active;
}
