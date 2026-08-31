// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_link.h"

#include <string.h>

#include "../storage/era_eeprom_storage.h"
#include "communication_core/era_split_communication_core_responder.h"
#include "era_split_restart_agreement.h"
#include "timer.h"

/* The stored block: the level, one flags byte, two reserved. Every way it can
 * fail to say something -- zeroed, an out-of-range level, an unknown flag, a
 * nonzero reserved byte -- reads as High and agreed, which is what makes an
 * uninitialised region correct rather than merely tolerated and is why this
 * feature owed no initialiser when the region was allocated a session earlier.
 *
 * **The flags byte was one of three reserved bytes until 2026-08-19, and the
 * schema change needs no key bump.** A block written by an image that predates
 * it has the byte at zero, which reads as *agreed*, and nothing on this lane
 * acts on the mark for control (the header's **Reconciliation**), so the level
 * byte itself is never reinterpreted and a pre-upgrade record converges on the
 * winner's level after the Low meet. In the other direction a pre-upgrade
 * image reads a set flag as a nonzero reserved byte and falls to High, which
 * is safe -- and owed to nobody, since both halves always run the identical
 * image (`era_source_map.md`'s **Stored-Data Compatibility**). */
#define ERA_SPLIT_LINK_STORAGE_FLAG_UNAGREED 0x01
#define ERA_SPLIT_LINK_STORAGE_FLAG_MASK ERA_SPLIT_LINK_STORAGE_FLAG_UNAGREED

typedef struct {
    uint8_t level;
    uint8_t flags;
    uint8_t reserved[2];
} era_split_link_storage_t;
_Static_assert(sizeof(era_split_link_storage_t) == ERA_EEPROM_LINK_CONFIG_SIZE, "The link block must fill its EEPROM region exactly.");

/* **What is left here is the level itself.** The two-phase agreement that used
 * to live in this file is era_split_restart_agreement.c, which took the state
 * machine, the wire, the quiet gate and the deadline with it. What stays is what
 * a link rate actually is on one half: what is stored and whether that claim was
 * agreed, what the wire is running, what the owner has picked but not applied,
 * and Reconciliation -- boot Low, the winner's raise, the fallback, the
 * listener's recovery step and the adoption. */
static struct {
    bool    stored_cached;
    uint8_t stored_level;
    bool    stored_unagreed;

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

    bool     owner_apply_armed;
    bool     fallback_latched;
    bool     upgrade_applied;
    bool     upgrade_succeeded;
    uint32_t upgrade_confirm_since_ms;
    bool     serviced_since_valid;
    uint32_t serviced_since_ms;
} g_era_split_link;

static void era_split_link_load(void) {
    if (g_era_split_link.stored_cached) {
        return;
    }
    era_split_link_storage_t block;
    memset(&block, 0, sizeof(block));
    uint8_t level    = ERA_SPLIT_LINK_LEVEL_HIGH;
    bool    unagreed = false;
    if (era_eeprom_read_config(&block, ERA_EEPROM_LINK_CONFIG_OFFSET, sizeof(block)) == sizeof(block) &&
        block.level <= ERA_SPLIT_LINK_LEVEL_LOW && (block.flags & (uint8_t)~ERA_SPLIT_LINK_STORAGE_FLAG_MASK) == 0 &&
        block.reserved[0] == 0 && block.reserved[1] == 0) {
        level    = block.level;
        unagreed = (block.flags & ERA_SPLIT_LINK_STORAGE_FLAG_UNAGREED) != 0;
    }
    g_era_split_link.stored_level    = level;
    g_era_split_link.stored_unagreed = unagreed;
    if (!g_era_split_link.running_initialized) {
        g_era_split_link.running_level       = ERA_SPLIT_LINK_LEVEL_LOW;
        g_era_split_link.running_initialized = true;
    }
    g_era_split_link.stored_cached = true;
}

static void era_split_link_store(uint8_t level, bool agreed) {
    era_split_link_load();

    era_split_link_storage_t block;
    memset(&block, 0, sizeof(block));
    block.level = level;
    /* **High is never unagreed.** It is the compiled default every image
     * stores, so no peer can hold a different opinion of it; the mark exists
     * to record a claim a peer has not seen, and there is no such claim to
     * make about the level a zeroed block already reads as. */
    if (!agreed && level != ERA_SPLIT_LINK_LEVEL_HIGH) {
        block.flags = ERA_SPLIT_LINK_STORAGE_FLAG_UNAGREED;
    }
    era_eeprom_update_config(&block, ERA_EEPROM_LINK_CONFIG_OFFSET, sizeof(block));

    g_era_split_link.stored_level    = block.level;
    g_era_split_link.stored_unagreed = block.flags != 0;
}

uint8_t era_split_link_active_level(void) {
    era_split_link_load();
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
        era_split_link_load();
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
    g_era_split_link.pending_level = level;
    return true;
}

bool era_split_link_request_apply(void) {
    if (era_split_link_pending_level() == era_split_link_active_level()) {
        /* The inert apply. It is answered here and not by the agreement,
           because the running level is this unit's fact and asking the service
           to compare a param against it would be the service learning what a
           param means. False is what keeps VIA Enable on: no USB bounce. */
        return false;
    }
    g_era_split_link.owner_apply_armed = true;
    if (!era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_SPEED, g_era_split_link.pending_level)) {
        g_era_split_link.owner_apply_armed = false;
        return false;
    }
    return true;
}

bool era_split_link_commit_stores(void) {
    bool stores                       = g_era_split_link.owner_apply_armed;
    g_era_split_link.owner_apply_armed = false;
    return stores;
}

bool era_split_link_commit_persists(uint8_t param, bool agreed, bool owner_apply) {
    if (owner_apply) {
        return true;
    }
    if (!agreed) {
        return false;
    }
    era_split_link_load();
    return param != g_era_split_link.stored_level;
}

void era_split_link_store_level(uint8_t level, bool agreed) {
    /* The write is at the commit rather than at the arm, and the disarm is the
     * reason: a half that stored at the arm and then disarmed would hold a
     * level its peer does not. An agreed peer that does not already store
     * `level` comes through here too (`era_split_link.h` **Owner Apply,
     * joined**). */
    era_split_link_store(level, agreed);
    g_era_split_link.pending_level  = level;
    g_era_split_link.pending_cached = true;
}

void era_split_link_note_relation(bool serviced, bool listening, bool local_is_rate_winner) {
    if (serviced && !g_era_split_link.relation_serviced) {
        g_era_split_link.serviced_since_ms    = timer_read32();
        g_era_split_link.serviced_since_valid = true;
    }
    if (!serviced) {
        g_era_split_link.serviced_since_valid = false;
    }
    g_era_split_link.relation_serviced    = serviced;
    g_era_split_link.wire_listening       = listening;
    g_era_split_link.local_is_rate_winner = local_is_rate_winner;
}

static void era_split_link_scan_open(uint32_t now_ms, uint32_t accepted, uint32_t undecodable) {
    g_era_split_link.scan_valid               = true;
    g_era_split_link.scan_since_ms            = now_ms;
    g_era_split_link.scan_accepted_at_open    = accepted;
    g_era_split_link.scan_undecodable_at_open = undecodable;
}

bool era_split_link_step_due(uint8_t *next_level) {
    era_split_link_load();
    if (g_era_split_link.fallback_latched && g_era_split_link.running_level != ERA_SPLIT_LINK_LEVEL_LOW) {
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
    *next_level = g_era_split_link.running_level == ERA_SPLIT_LINK_LEVEL_LOW ? ERA_SPLIT_LINK_LEVEL_HIGH : (uint8_t)(g_era_split_link.running_level + 1U);
    return true;
}

void era_split_link_note_step_applied(uint8_t level) {
    era_split_link_load();
    g_era_split_link.running_level = level;
    g_era_split_link.scan_valid    = false;
    if (g_era_split_link.fallback_latched) {
        return;
    }
    if (g_era_split_link.relation_serviced && level != ERA_SPLIT_LINK_LEVEL_LOW) {
        g_era_split_link.upgrade_applied          = true;
        g_era_split_link.upgrade_succeeded        = false;
        g_era_split_link.upgrade_confirm_since_ms = timer_read32();
    }
}

bool era_split_link_runtime_settled(void) {
    if (!g_era_split_link.relation_serviced) {
        return true;
    }
    if (g_era_split_link.fallback_latched || g_era_split_link.upgrade_succeeded) {
        return true;
    }
    era_split_link_load();
    return g_era_split_link.local_is_rate_winner && g_era_split_link.stored_level == ERA_SPLIT_LINK_LEVEL_LOW;
}

void era_split_link_task(void) {
    era_split_link_load();

    if (!g_era_split_link.relation_serviced) {
        if (g_era_split_link.upgrade_applied && !g_era_split_link.upgrade_succeeded &&
            timer_elapsed32(g_era_split_link.upgrade_confirm_since_ms) >= ERA_SPLIT_LINK_UPGRADE_CONFIRM_MS) {
            g_era_split_link.fallback_latched = true;
            g_era_split_link.upgrade_applied  = false;
        }
        return;
    }

    if (g_era_split_link.upgrade_applied && !g_era_split_link.upgrade_succeeded &&
        timer_elapsed32(g_era_split_link.upgrade_confirm_since_ms) >= ERA_SPLIT_LINK_UPGRADE_CONFIRM_MS) {
        g_era_split_link.upgrade_succeeded = true;
    }

    if (g_era_split_link.fallback_latched) {
        return;
    }

    if (g_era_split_link.upgrade_succeeded && g_era_split_link.running_level != g_era_split_link.stored_level) {
        /* **The adoption.** The raise is live, so both halves run the winner's
           level; a half whose EEPROM still names something else stores what
           it is running, agreed, and lets the dropdown's seed follow. No
           reset. The fallback latch is what keeps this from writing Low over
           a stored High the cable could not hold this boot. */
        era_split_link_store(g_era_split_link.running_level, true);
        g_era_split_link.pending_level  = g_era_split_link.running_level;
        g_era_split_link.pending_cached = true;
        return;
    }
    if (g_era_split_link.upgrade_succeeded && g_era_split_link.stored_unagreed) {
        era_split_link_store(g_era_split_link.stored_level, true);
    }

    if (g_era_split_link.local_is_rate_winner && !g_era_split_link.upgrade_applied && !g_era_split_link.upgrade_succeeded &&
        g_era_split_link.running_level == ERA_SPLIT_LINK_LEVEL_LOW &&
        g_era_split_link.stored_level != ERA_SPLIT_LINK_LEVEL_LOW) {
        g_era_split_link.owner_apply_armed = false;
        (void)era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_SPEED, g_era_split_link.stored_level);
        return;
    }

    if (!g_era_split_link.local_is_rate_winner && !g_era_split_link.upgrade_applied && !g_era_split_link.upgrade_succeeded &&
        g_era_split_link.serviced_since_valid &&
        timer_elapsed32(g_era_split_link.serviced_since_ms) >= ERA_SPLIT_LINK_UPGRADE_WAIT_MS) {
        g_era_split_link.upgrade_succeeded = true;
    }
}

typedef struct {
    uint16_t ms;
    uint8_t  on;
} era_split_link_fallback_report_step_t;

/* Three long pulses, then a tail so the end is not a fourth off. Cost is per
   transition: a held STATUS frame that is ACTIVE and not DIRTY neither
   renders nor flushes. */
static const era_split_link_fallback_report_step_t era_split_link_fallback_report_steps[] = {
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
