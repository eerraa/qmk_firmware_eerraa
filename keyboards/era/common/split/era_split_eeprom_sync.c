// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_eeprom_sync.h"

#include <string.h>

#include "timer.h"

#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "era_host_peer_storage.h"
#endif

/* The 2026-08-14 redesign: one pending fact, no timers.
 *
 * The lamp used to be two arms — a local-save presenter and a storage
 * episode span — each read from this half's own state machine, glued over
 * the protocol's silences by a fixed 1500 ms bridge run on each half's own
 * clock. The bridge existed because the responder structurally cannot know
 * whether the relation still holds work: every queue, cell and completion
 * poll is initiator-side state. A constant standing in for a missing wire
 * fact has no correct value, and the failure it produced was probabilistic
 * by construction — device-measured as the receiving half's lamp dying ~1 s
 * early on layout loads while every predicate stamp agreed to 15 ms.
 *
 * Now the storage engine owns the fact (era_host_peer_storage_indicator_
 * pending: the local arm plus the STORAGE_PENDING wire section's mirror),
 * and this unit only presents it: edge stamps for the console, and the
 * rise-anchored minimum-visible floor that pads a short process to
 * visibility without ever extending a long one. Both halves run this same
 * presenter over one shared fact, so the lamps end within one poll of the
 * initiator's last close instead of within one half's guess. */

typedef struct {
    uint32_t span_rise_ms;
    uint32_t span_fall_ms;
    uint32_t last_pending_ms;
    uint32_t span_count;
    uint32_t red_era_count;
    uint32_t red_on_ms;
    uint32_t red_off_ms;
    uint32_t break_ms;
    uint8_t  break_count;
    uint8_t  break_flags;
    uint8_t  break_state;
    bool     red_present;
    bool     span_open;
    bool     floor_active;
} era_split_eeprom_sync_state_t;

static era_split_eeprom_sync_state_t era_split_eeprom_sync_state;

bool era_split_eeprom_sync_indicator_visible_advance(void) {
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    if (era_host_peer_storage_indicator_pending()) {
        /* Stamped on every pending pass, not once on the rising edge: the
         * last stamp IS the fall time when the fact drops, with no second
         * read racing it. One operator action produces one span however
         * many domains it moves, because the fact holds across the
         * inter-episode gaps the engine's cells and changed shadow cover. */
        uint32_t now_ms = timer_read32();
        if (!era_split_eeprom_sync_state.span_open) {
            era_split_eeprom_sync_state.span_open    = true;
            era_split_eeprom_sync_state.floor_active = true;
            era_split_eeprom_sync_state.span_count++;
            era_split_eeprom_sync_state.span_rise_ms = now_ms;
        }
        era_split_eeprom_sync_state.last_pending_ms = now_ms;
        return true;
    }
    if (era_split_eeprom_sync_state.span_open) {
        era_split_eeprom_sync_state.span_open    = false;
        era_split_eeprom_sync_state.span_fall_ms = era_split_eeprom_sync_state.last_pending_ms;
    }
    /* The floor: hold a just-risen lamp to visibility, anchored at this
     * half's own rise. It expires and never re-arms until the next span, so
     * it cannot become a trailing delay — past rise + floor the lamp
     * tracks the fact exactly. */
    if (era_split_eeprom_sync_state.floor_active) {
        if (timer_elapsed32(era_split_eeprom_sync_state.span_rise_ms) < ERA_SPLIT_EEPROM_SYNC_MIN_VISIBLE_MS) {
            return true;
        }
        era_split_eeprom_sync_state.floor_active = false;
    }
    return false;
#else
    return false;
#endif
}

void era_split_eeprom_sync_note_status_frame_presence(bool status_frame, uint8_t frame_flags, uint8_t panel_state_bits) {
    if (status_frame) {
        if (!era_split_eeprom_sync_state.red_present) {
            era_split_eeprom_sync_state.red_present = true;
            era_split_eeprom_sync_state.red_era_count++;
            era_split_eeprom_sync_state.red_on_ms = timer_read32();
        }
    } else if (era_split_eeprom_sync_state.red_present) {
        era_split_eeprom_sync_state.red_present = false;
        era_split_eeprom_sync_state.red_off_ms = timer_read32();
        /* The breaker latch: a red era ending while the lamp is still
         * commanded visible is never the span's own end — the legitimate
         * end always arrives after the advance dropped the command — so
         * this branch fires exactly on the healed-trigger frames the
         * `rn`-vs-`spans` excess counts, and records which frame broke the
         * era. Last-writer-wins is enough: the excess is the count, this
         * is the identity. */
        if (era_split_eeprom_sync_state.span_open || era_split_eeprom_sync_state.floor_active) {
            era_split_eeprom_sync_state.break_count++;
            era_split_eeprom_sync_state.break_flags = frame_flags;
            era_split_eeprom_sync_state.break_state = panel_state_bits;
            era_split_eeprom_sync_state.break_ms    = era_split_eeprom_sync_state.red_off_ms;
        }
    }
}

__attribute__((weak)) void era_split_eeprom_sync_reload_domain_user(era_split_eeprom_sync_domain_t domain) {
    (void)domain;
}

__attribute__((weak)) void era_split_eeprom_sync_reload_domain_kb(era_split_eeprom_sync_domain_t domain) {
    era_split_eeprom_sync_reload_domain_user(domain);
}

void era_split_eeprom_sync_get_diagnostics_snapshot(era_split_eeprom_sync_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->span_count    = era_split_eeprom_sync_state.span_count;
    snapshot->span_rise_ms  = era_split_eeprom_sync_state.span_rise_ms;
    snapshot->span_fall_ms  = era_split_eeprom_sync_state.span_fall_ms;
    snapshot->red_era_count = era_split_eeprom_sync_state.red_era_count;
    snapshot->red_on_ms     = era_split_eeprom_sync_state.red_on_ms;
    snapshot->red_off_ms    = era_split_eeprom_sync_state.red_off_ms;
    snapshot->break_ms      = era_split_eeprom_sync_state.break_ms;
    snapshot->break_count   = era_split_eeprom_sync_state.break_count;
    snapshot->break_flags   = era_split_eeprom_sync_state.break_flags;
    snapshot->break_state   = era_split_eeprom_sync_state.break_state;
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    snapshot->pending_bits = era_host_peer_storage_indicator_diag();
#endif
    snapshot->visible = era_split_eeprom_sync_state.span_open || era_split_eeprom_sync_state.floor_active ? 1 : 0;
}
