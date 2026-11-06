// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_eeprom_sync.h"

#include <string.h>

#include "timer.h"
#ifdef ERA_TOMAK_SYNC_TIMING
#    include "era_sync_timing.h"
#endif

#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "era_host_peer_storage.h"
#endif

/* Storage owns the complete operation and valid peer mirror. Presentation
 * records that fact's span without extending it or granting execution. */

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
} era_split_eeprom_sync_state_t;

static era_split_eeprom_sync_state_t era_split_eeprom_sync_state;

bool era_split_eeprom_sync_indicator_visible_advance(void) {
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    bool pending = era_host_peer_storage_indicator_pending();
#ifdef ERA_TOMAK_SYNC_TIMING
    era_sync_timing_pending(pending);
#endif
#    ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    era_host_peer_storage_cause_note_indicator(pending);
#    endif
    if (pending) {
        /* Stamped on every pending pass, not once on the rising edge: the
         * last stamp IS the fall time when the fact drops, with no second
         * read racing it. The storage owner, not presentation, determines
         * whether inter-episode work still belongs to this span. */
        uint32_t now_ms = timer_read32();
        if (!era_split_eeprom_sync_state.span_open) {
            era_split_eeprom_sync_state.span_open    = true;
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
        /* The breaker latch: an EEPROM SYNC STATUS era ending while the lamp
         * is still commanded visible is never the span's own end — the
         * legitimate end arrives after the advance dropped the command. This
         * branch therefore records the healed-trigger frame, including a
         * higher-priority red/green STATUS report that temporarily preempted
         * blue synchronization. */
        if (era_split_eeprom_sync_state.span_open) {
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
    snapshot->visible = era_split_eeprom_sync_state.span_open ? 1 : 0;
}
