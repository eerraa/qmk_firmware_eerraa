// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_scheduler_session.h"

#include <string.h>

#include "../system/era_matrix_engine.h"
#include "atomic_util.h"
#include "era_split_scheduler_events.h"

typedef struct {
    bool     valid;
    bool     accepted_host_open;
    bool     accepted_no_host;
    bool     matrix_ready;
    bool     bulk_page_supported;
    uint16_t usb_epoch;
    uint16_t host_open_generation;
    uint16_t host_close_generation;
} era_split_scheduler_status_cache_t;

typedef struct {
    bool                               initialized;
    bool                               peer_known;
    bool                               peer_generation_changed;
    era_split_scheduler_status_cache_t local;
    era_split_scheduler_status_cache_t peer;
    uint32_t                           local_session_tx_count;
    uint32_t                           peer_session_rx_count;
    uint32_t                           peer_session_forget_count;
} era_split_scheduler_session_state_t;

static era_split_scheduler_session_state_t g_era_split_scheduler_session;

static uint16_t era_split_scheduler_session_next_generation(uint16_t generation) {
    generation++;
    return generation == 0 ? 1 : generation;
}

static bool era_split_scheduler_session_authority_host_open(const era_authority_snapshot_t *authority) {
    return authority != NULL && authority->valid && authority->usb_state == ERA_AUTH_USB_HOST_OPEN;
}

static bool era_split_scheduler_session_authority_no_host(const era_authority_snapshot_t *authority) {
    return authority != NULL && authority->valid && authority->usb_state == ERA_AUTH_USB_NO_HOST;
}

void era_split_scheduler_session_init(void) {
    ATOMIC_BLOCK_RESTORESTATE {
        if (!g_era_split_scheduler_session.initialized) {
            memset(&g_era_split_scheduler_session, 0, sizeof(g_era_split_scheduler_session));
            g_era_split_scheduler_session.initialized = true;
        }
    }
}

void era_split_scheduler_session_note_local_facts(const era_authority_snapshot_t *authority) {
    era_split_scheduler_session_init();

    bool accepted_host_open = era_split_scheduler_session_authority_host_open(authority);
    bool accepted_no_host   = era_split_scheduler_session_authority_no_host(authority);

    ATOMIC_BLOCK_RESTORESTATE {
        if (!g_era_split_scheduler_session.local.valid) {
            g_era_split_scheduler_session.local.host_open_generation  = accepted_host_open ? 1 : 0;
            g_era_split_scheduler_session.local.host_close_generation = accepted_no_host ? 1 : 0;
        } else {
            if (!g_era_split_scheduler_session.local.accepted_host_open && accepted_host_open) {
                g_era_split_scheduler_session.local.host_open_generation = era_split_scheduler_session_next_generation(g_era_split_scheduler_session.local.host_open_generation);
            }
            if (!g_era_split_scheduler_session.local.accepted_no_host && accepted_no_host) {
                g_era_split_scheduler_session.local.host_close_generation = era_split_scheduler_session_next_generation(g_era_split_scheduler_session.local.host_close_generation);
            }
        }

        g_era_split_scheduler_session.local.valid               = authority != NULL && authority->valid && accepted_host_open != accepted_no_host;
        g_era_split_scheduler_session.local.accepted_host_open  = accepted_host_open;
        g_era_split_scheduler_session.local.accepted_no_host    = accepted_no_host;
        g_era_split_scheduler_session.local.matrix_ready        = accepted_no_host && era_matrix_engine_local_matrix_ready();
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
        g_era_split_scheduler_session.local.bulk_page_supported = true;
#else
        g_era_split_scheduler_session.local.bulk_page_supported = false;
#endif
        g_era_split_scheduler_session.local.usb_epoch           = authority != NULL ? authority->usb_epoch : 0;
    }
}

void era_split_scheduler_session_forget_peer_from_scheduler(void) {
    era_split_scheduler_session_init();
    ATOMIC_BLOCK_RESTORESTATE {
        memset(&g_era_split_scheduler_session.peer, 0, sizeof(g_era_split_scheduler_session.peer));
        g_era_split_scheduler_session.peer_known              = false;
        g_era_split_scheduler_session.peer_generation_changed = false;
        g_era_split_scheduler_session.peer_session_forget_count++;
    }
}

bool era_split_scheduler_session_build_local_status(bool response_requested, era_split_wire_session_status_t *status) {
    era_split_scheduler_session_init();
    era_authority_snapshot_t auth;
    era_split_authority_reducer_get_snapshot(&auth);
    era_split_scheduler_session_note_local_facts(&auth);

    if (status == NULL) {
        return false;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        if (!g_era_split_scheduler_session.local.valid) {
            memset(status, 0, sizeof(*status));
            return false;
        }

        memset(status, 0, sizeof(*status));
        status->accepted_host_open        = g_era_split_scheduler_session.local.accepted_host_open;
        status->accepted_no_host          = g_era_split_scheduler_session.local.accepted_no_host;
        status->status_response_requested = response_requested;
        status->matrix_ready              = g_era_split_scheduler_session.local.matrix_ready;
        status->bulk_page_supported       = g_era_split_scheduler_session.local.bulk_page_supported;
        status->usb_epoch                 = g_era_split_scheduler_session.local.usb_epoch;
        status->host_open_generation      = g_era_split_scheduler_session.local.host_open_generation;
        status->host_close_generation     = g_era_split_scheduler_session.local.host_close_generation;
    }
    return true;
}

void era_split_scheduler_session_note_local_status_sent(void) {
    era_split_scheduler_session_init();
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_split_scheduler_session.local_session_tx_count++;
    }
}

/* The three fields that decide "the peer's authority moved", shared by both
 * carriers so the two can never disagree about what a change is. Called with
 * the session lock held. */
static bool era_split_scheduler_session_peer_generation_changed_locked(uint16_t usb_epoch, uint16_t host_open_generation, uint16_t host_close_generation) {
    return !g_era_split_scheduler_session.peer_known ||
           g_era_split_scheduler_session.peer.host_open_generation != host_open_generation ||
           g_era_split_scheduler_session.peer.host_close_generation != host_close_generation ||
           g_era_split_scheduler_session.peer.usb_epoch != usb_epoch;
}

void era_split_scheduler_session_note_peer_status(const era_split_wire_session_status_t *status) {
    era_split_scheduler_session_init();
    if (status == NULL) {
        return;
    }
    if (status->accepted_host_open == status->accepted_no_host) {
        return;
    }
    if (status->matrix_ready && !status->accepted_no_host) {
        return;
    }

    bool peer_mode_status_changed = false;
    ATOMIC_BLOCK_RESTORESTATE {
        bool generation_changed = era_split_scheduler_session_peer_generation_changed_locked(status->usb_epoch, status->host_open_generation, status->host_close_generation);

        g_era_split_scheduler_session.peer.accepted_host_open    = status->accepted_host_open;
        g_era_split_scheduler_session.peer.accepted_no_host      = status->accepted_no_host;
        g_era_split_scheduler_session.peer.matrix_ready          = status->matrix_ready;
        g_era_split_scheduler_session.peer.bulk_page_supported   = status->bulk_page_supported;
        g_era_split_scheduler_session.peer.usb_epoch             = status->usb_epoch;
        g_era_split_scheduler_session.peer.host_open_generation  = status->host_open_generation;
        g_era_split_scheduler_session.peer.host_close_generation = status->host_close_generation;
        g_era_split_scheduler_session.peer_known                 = true;
        g_era_split_scheduler_session.peer_generation_changed |= generation_changed;
        g_era_split_scheduler_session.peer_session_rx_count++;
        peer_mode_status_changed = generation_changed;
    }
    if (peer_mode_status_changed) {
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_PEER_SESSION);
    }
}

bool era_split_scheduler_session_get_local_authority(era_split_wire_authority_section_t *authority) {
    era_split_scheduler_session_init();
    if (authority == NULL) {
        return false;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        if (!g_era_split_scheduler_session.local.valid) {
            memset(authority, 0, sizeof(*authority));
            return false;
        }
        authority->accepted_host_open    = g_era_split_scheduler_session.local.accepted_host_open;
        authority->accepted_no_host      = g_era_split_scheduler_session.local.accepted_no_host;
        authority->matrix_ready          = g_era_split_scheduler_session.local.matrix_ready;
        authority->usb_epoch             = g_era_split_scheduler_session.local.usb_epoch;
        authority->host_open_generation  = g_era_split_scheduler_session.local.host_open_generation;
        authority->host_close_generation = g_era_split_scheduler_session.local.host_close_generation;
    }
    return true;
}

/* Returns whether the peer's authority record actually moved. The standing
   record is latest-state, so this site is reached whenever ANY field of it
   changed -- which is the deliberate design (the freshness test belongs at the
   consumer) and is also why the caller cannot count `app=auth` from reaching
   here. Device-shown 2026-08-13: three authority "applies" inside a 174.7 s
   window in which the only thing that crossed the wire was three anchor
   refreshes. */
bool era_split_scheduler_session_note_peer_authority(const era_split_wire_authority_section_t *authority) {
    era_split_scheduler_session_init();
    if (authority == NULL) {
        return false;
    }
    /* The same two semantic refusals SESSION_STATUS applies. The wire
     * validator already enforces both, so reaching either here means a
     * caller reconstructed a record rather than decoding one. */
    if (authority->accepted_host_open == authority->accepted_no_host) {
        return false;
    }
    if (authority->matrix_ready && !authority->accepted_no_host) {
        return false;
    }

    bool peer_mode_status_changed = false;
    bool record_changed           = false;
    ATOMIC_BLOCK_RESTORESTATE {
        /* Discovery is not this section's job: a relation lane only runs on a
         * confirmed relation, so an unknown peer here is a stale result and
         * not a peer to learn about. */
        if (!g_era_split_scheduler_session.peer_known) {
            return false;
        }
        record_changed = g_era_split_scheduler_session.peer.accepted_host_open != authority->accepted_host_open ||
                         g_era_split_scheduler_session.peer.accepted_no_host != authority->accepted_no_host ||
                         g_era_split_scheduler_session.peer.matrix_ready != authority->matrix_ready ||
                         g_era_split_scheduler_session.peer.usb_epoch != authority->usb_epoch ||
                         g_era_split_scheduler_session.peer.host_open_generation != authority->host_open_generation ||
                         g_era_split_scheduler_session.peer.host_close_generation != authority->host_close_generation;
        bool generation_changed = era_split_scheduler_session_peer_generation_changed_locked(authority->usb_epoch, authority->host_open_generation, authority->host_close_generation);

        g_era_split_scheduler_session.peer.accepted_host_open    = authority->accepted_host_open;
        g_era_split_scheduler_session.peer.accepted_no_host      = authority->accepted_no_host;
        g_era_split_scheduler_session.peer.matrix_ready          = authority->matrix_ready;
        /* `bulk_page_supported` is deliberately untouched: it is a
         * compile-time capability this section does not carry, and writing it
         * from here would clear a fact the frame never held.
         *
         * The storage-changed hint used to stand beside it in this sentence
         * for the same reason, and that reason was true -- it was
         * SESSION_STATUS flag bit `0x04` with a `peer_storage_changed_hint`
         * field in this cache, so the frame really was its only carrier.
         * Slice 11.7 (81970ada28) deleted both, because reading one bit was
         * holding a 50 ms core0 poll open in a relation that needed nothing
         * else from the frame: the fact moved onto the relation's own lane as
         * a response section and `0x04` went back un-reused
         * (era_closed_surface_contract.md). There is no hint field in this
         * cache left to skip, and a tree that has one is older than that
         * slice. */
        g_era_split_scheduler_session.peer.usb_epoch             = authority->usb_epoch;
        g_era_split_scheduler_session.peer.host_open_generation  = authority->host_open_generation;
        g_era_split_scheduler_session.peer.host_close_generation = authority->host_close_generation;
        g_era_split_scheduler_session.peer_generation_changed |= generation_changed;
        peer_mode_status_changed = generation_changed;
    }
    if (peer_mode_status_changed) {
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_PEER_SESSION);
    }
    return record_changed;
}

static void era_split_scheduler_session_copy_peer_mode_session_locked(era_split_mode_peer_session_t *peer_session) {
    if (peer_session == NULL) {
        return;
    }

    memset(peer_session, 0, sizeof(*peer_session));
    peer_session->known                 = g_era_split_scheduler_session.peer_known;
    peer_session->accepted_host_open    = g_era_split_scheduler_session.peer.accepted_host_open;
    peer_session->accepted_no_host      = g_era_split_scheduler_session.peer.accepted_no_host;
    peer_session->matrix_ready          = g_era_split_scheduler_session.peer.matrix_ready;
    peer_session->bulk_page_supported   = g_era_split_scheduler_session.peer.bulk_page_supported;
    peer_session->usb_epoch             = g_era_split_scheduler_session.peer.usb_epoch;
    peer_session->host_open_generation  = g_era_split_scheduler_session.peer.host_open_generation;
    peer_session->host_close_generation = g_era_split_scheduler_session.peer.host_close_generation;
}

void era_split_scheduler_session_consume_peer_mode_session(era_split_mode_peer_session_t *peer_session, bool *generation_changed) {
    era_split_scheduler_session_init();

    ATOMIC_BLOCK_RESTORESTATE {
        era_split_scheduler_session_copy_peer_mode_session_locked(peer_session);
        if (generation_changed != NULL) {
            *generation_changed = g_era_split_scheduler_session.peer_generation_changed;
        }
        g_era_split_scheduler_session.peer_generation_changed = false;
    }
}

bool era_split_scheduler_session_peer_known(void) {
    era_split_scheduler_session_init();
    bool known;
    ATOMIC_BLOCK_RESTORESTATE {
        known = g_era_split_scheduler_session.peer_known;
    }
    return known;
}

/* The session half of HOST-PEER matrix admission: a valid local session, and a
   peer whose own session says it is this relation's PEER and has a snapshot to
   send. **The role term is the caller's**, and it is the relation
   (`mode == ERA_SPLIT_MODE_HOST_PEER_HOST`) rather than a second reading of it
   here.

   It used to ask `local.accepted_host_open` for that role, which was the same
   answer for as long as the relation could not outlive the local host-open --
   and stopped being the same answer when it could. A suspended HOST is still
   its relation's HOST and still owns the projection its own remote wake reads
   (era_authority_contract.md); what its closed host-open governs is whether it
   may emit HID, which QMK's own suspended `should_process_keypress()` already
   holds and this gate never had to repeat. Keeping the term here made "am I
   this relation's HOST" and "may I type right now" one condition, and only the
   second of the two is what a sustained suspend answers. */
bool era_split_scheduler_session_host_peer_host_matrix_admitted(void) {
    era_split_scheduler_session_init();

    era_authority_snapshot_t auth;
    era_split_authority_reducer_get_snapshot(&auth);
    era_split_scheduler_session_note_local_facts(&auth);

    bool admitted;
    ATOMIC_BLOCK_RESTORESTATE {
        admitted = g_era_split_scheduler_session.local.valid && g_era_split_scheduler_session.peer_known && !g_era_split_scheduler_session.peer.accepted_host_open && g_era_split_scheduler_session.peer.accepted_no_host && g_era_split_scheduler_session.peer.matrix_ready;
    }
    return admitted;
}

void era_split_scheduler_session_get_diagnostics_snapshot(era_split_scheduler_session_diagnostics_t *snapshot) {
    era_split_scheduler_session_init();
    if (snapshot == NULL) {
        return;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->peer_known                  = g_era_split_scheduler_session.peer_known ? 1 : 0;
        snapshot->peer_accepted_host_open     = g_era_split_scheduler_session.peer.accepted_host_open ? 1 : 0;
        snapshot->peer_accepted_no_host       = g_era_split_scheduler_session.peer.accepted_no_host ? 1 : 0;
        snapshot->peer_matrix_ready           = g_era_split_scheduler_session.peer.matrix_ready ? 1 : 0;
        snapshot->peer_bulk_page_supported    = g_era_split_scheduler_session.peer.bulk_page_supported ? 1 : 0;
        snapshot->peer_usb_epoch              = g_era_split_scheduler_session.peer.usb_epoch;
        snapshot->peer_host_open_generation   = g_era_split_scheduler_session.peer.host_open_generation;
        snapshot->peer_host_close_generation  = g_era_split_scheduler_session.peer.host_close_generation;
        snapshot->peer_session_rx_count       = g_era_split_scheduler_session.peer_session_rx_count;
        snapshot->peer_session_forget_count   = g_era_split_scheduler_session.peer_session_forget_count;
        snapshot->local_bulk_page_supported   = g_era_split_scheduler_session.local.bulk_page_supported ? 1 : 0;
        snapshot->local_session_tx_count      = g_era_split_scheduler_session.local_session_tx_count;
    }
}
