// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_split_authority_reducer.h"
#include "era_split_mode_planner.h"
#include "era_split_wire_protocol.h"

typedef struct {
    uint8_t  peer_known;
    uint8_t  peer_accepted_host_open;
    uint8_t  peer_accepted_no_host;
    uint8_t  peer_matrix_ready;
    uint8_t  peer_bulk_page_supported;
    uint16_t peer_usb_epoch;
    uint16_t peer_host_open_generation;
    uint16_t peer_host_close_generation;
    uint32_t peer_session_rx_count;
    uint32_t peer_session_forget_count;

    /* Only the two this half's own consumers read. The six that stood beside
       them -- this half's accepted-authority pair, its matrix-ready flag and
       its three USB-epoch/generation values -- were written on every snapshot
       and never read: no console line ever carried them, so restoring one
       means adding a field and a print, not a print. */
    uint8_t  local_bulk_page_supported;
    uint32_t local_session_tx_count;
} era_split_scheduler_session_diagnostics_t;

void era_split_scheduler_session_init(void);
void era_split_scheduler_session_note_local_facts(const era_authority_snapshot_t *authority);
void era_split_scheduler_session_forget_peer_from_scheduler(void);

bool era_split_scheduler_session_build_local_status(bool response_requested, era_split_wire_session_status_t *status);
void era_split_scheduler_session_note_local_status_sent(void);
void era_split_scheduler_session_note_peer_status(const era_split_wire_session_status_t *status);

/* The AUTHORITY wire section's two ends (Slice 11.6). Same facts, same cache,
 * a second carrier -- the relation's own lane instead of SESSION_STATUS.
 *
 * `get_local_authority` reads the cached local facts and does not refresh
 * them: every caller sits on a path that has already run
 * `note_local_facts()` this pass, and refreshing here would put a reducer
 * snapshot read on the standing plan's per-pass staleness comparison.
 * It returns false while the local session is invalid, which is what keeps a
 * half with no decided authority from advertising one.
 *
 * `note_peer_authority` requires the peer to be known already and otherwise
 * ignores the section. Discovery stays SESSION_STATUS's: this section carries
 * no `bulk_page_supported`, so a peer entry created from it would claim the
 * capability is absent.
 *
 * Since Slice 11.7 that capability is the *whole* of what SESSION_STATUS still
 * carries beyond this section, which is why the frame survives for discovery
 * and for nothing else. */
bool era_split_scheduler_session_get_local_authority(era_split_wire_authority_section_t *authority);
/* Returns whether the peer's authority record moved, which is what `app=auth`
   counts. The apply itself stays unconditional -- reaching it means the
   latest-state standing record changed in some field, not necessarily this
   one. */
bool era_split_scheduler_session_note_peer_authority(const era_split_wire_authority_section_t *authority);

void era_split_scheduler_session_consume_peer_mode_session(era_split_mode_peer_session_t *peer_session, bool *generation_changed);
bool era_split_scheduler_session_peer_known(void);
bool era_split_scheduler_session_host_peer_host_matrix_admitted(void);

void era_split_scheduler_session_get_diagnostics_snapshot(era_split_scheduler_session_diagnostics_t *snapshot);
