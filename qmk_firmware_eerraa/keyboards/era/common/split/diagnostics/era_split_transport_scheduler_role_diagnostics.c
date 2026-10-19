// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_transport_scheduler_role_diagnostics.h"

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE

#include <string.h>

#include "../era_host_peer_matrix_link.h"
#include "../era_split_responder_projection.h"
#include "../era_split_transaction_engine.h"
#include "../era_split_transport_scheduler.h"
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "../era_host_peer_storage.h"
#endif

/* The compact wire IO every era block measures, and nothing else. The
   per-bucket transaction timing is a separate member below because only the
   PEER era reads it: while it lived here, the HOST era carried it twice --
   once in `base` and once in `accum` -- for an instrument that has no field in
   the tail, no line in the printer and no reader anywhere. */
typedef struct {
    uint32_t compact_tx_count;
    uint32_t compact_rx_count;
    uint32_t compact_miss_count;
    uint32_t compact_bad_count;
    uint32_t compact_fail_count;
} era_split_transport_scheduler_transaction_counts_t;

#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
typedef struct {
    uint32_t                                          timing_sample_count;
    uint32_t                                          timing_timeout_count;
    era_split_transaction_timing_bucket_diagnostics_t timing_buckets[ERA_SPLIT_TRANSACTION_TIMING_BUCKET_COUNT];
} era_split_transport_scheduler_transaction_timing_counts_t;
#    endif

typedef struct {
    uint32_t                                           source_push_tx_count;
    uint32_t                                           source_push_ack_count;
    era_split_transport_scheduler_transaction_counts_t io;
#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transport_scheduler_transaction_timing_counts_t timing_io;
#    endif
} era_split_transport_scheduler_peer_era_counts_t;

typedef struct {
    bool                                            valid;
    bool                                            active;
    uint8_t                                         last_peer_mode;
    uint8_t                                         local_current_seq8;
    uint8_t                                         local_host_known_seq8;
    era_split_transport_scheduler_peer_era_counts_t base;
    era_split_transport_scheduler_peer_era_counts_t accum;
#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transaction_timing_diagnostics_t timing;
#    endif
} era_split_transport_scheduler_peer_era_snapshot_t;

typedef struct {
    uint32_t                                           ack_status_tx_count;
    uint32_t                                           peer_cache_update_count;
    uint32_t                                           peer_cache_project_count;
    uint32_t                                           peer_cache_flush_count;
    uint32_t                                           responder_relation_request_rx_count;
    uint32_t                                           responder_host_peer_heartbeat_rx_count;
    uint32_t                                           responder_host_peer_source_push_rx_count;
    era_split_transport_scheduler_transaction_counts_t io;
} era_split_transport_scheduler_host_era_counts_t;

typedef struct {
    bool                                            valid;
    bool                                            active;
    uint8_t                                         last_host_mode;
    uint8_t                                         peer_cache_valid;
    uint8_t                                         peer_matrix_seq8;
    era_split_transport_scheduler_host_era_counts_t base;
    era_split_transport_scheduler_host_era_counts_t accum;
} era_split_transport_scheduler_host_era_snapshot_t;

/* DUAL-HOST era. It shares the compact-IO type with the other two blocks,
 * which is what "the era's compact wire IO" means in all three, and adds the
 * facts only this block reads: the storage episode counts, which in a settled
 * window must not move at all. The compact five must equal the SESSION_STATUS
 * traffic and nothing else. */
typedef struct {
    era_split_transport_scheduler_transaction_counts_t io;
    uint32_t                                           storage_open_count;
    uint32_t                                           storage_close_count;
    uint32_t                                           storage_transfer_count;
    uint32_t                                           runtime_tx_count;
    uint32_t                                           runtime_rx_count;
} era_split_transport_scheduler_dual_host_era_counts_t;

typedef struct {
    bool                                                 valid;
    bool                                                 active;
    uint8_t                                              last_mode;
    era_split_transport_scheduler_dual_host_era_counts_t base;
    era_split_transport_scheduler_dual_host_era_counts_t accum;
} era_split_transport_scheduler_dual_host_era_snapshot_t;

/* One bit per era block: a boundary read (this block's activate or capture)
 * could not prove a stable publication and accepted the mirror's fallback, so
 * this block's delta is not a measurement. Kept as a shared bitfield rather
 * than a flag per block because it is one fact about one shared mirror. */
enum {
    ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_PEER      = 1U << 0,
    ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_HOST      = 1U << 1,
    ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_DUAL_HOST = 1U << 2,
};

typedef struct {
    era_split_transport_scheduler_peer_era_snapshot_t       peer_era;
    era_split_transport_scheduler_host_era_snapshot_t       host_era;
    era_split_transport_scheduler_dual_host_era_snapshot_t  dual_host_era;
    uint8_t                                                 boundary_lost;
} era_split_transport_scheduler_role_diagnostics_state_t;

typedef struct {
    era_split_transport_scheduler_peer_era_counts_t      peer_era_counts;
    era_split_transport_scheduler_host_era_counts_t      host_era_counts;
    era_split_transport_scheduler_dual_host_era_counts_t dual_host_era_counts;
#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transaction_timing_diagnostics_t peer_era_timing;
#    endif
} era_split_transport_scheduler_role_diagnostics_write_scratch_t;

static era_split_transport_scheduler_role_diagnostics_state_t g_era_split_transport_scheduler_role_diagnostics;
static era_split_transport_scheduler_role_diagnostics_write_scratch_t g_era_split_transport_scheduler_role_diagnostics_write_scratch;

/* Era sources are not all monotonic across in-era identity events: a core1
 * restart zeroes the engine and standing counters, and a standing-plan
 * republication zeroes the standing section counts, while an era that spans
 * the event keeps its base. A raw subtraction then underflows — device-read
 * as `rt=-5/-6`, printed as 2^32-5/-6 — so every era delta degrades to the
 * post-reset portion instead of the underflow. */
static inline uint32_t era_split_transport_scheduler_era_count_delta(uint32_t current, uint32_t base) {
    return current >= base ? current - base : current;
}

#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
static void era_split_transport_scheduler_timing_bucket_accumulate_delta(era_split_transaction_timing_bucket_diagnostics_t       *bucket,
                                                                         const era_split_transaction_timing_bucket_diagnostics_t *current,
                                                                         const era_split_transaction_timing_bucket_diagnostics_t *base) {
    if (bucket == NULL || current == NULL || base == NULL) {
        return;
    }

    uint32_t sample_delta = era_split_transport_scheduler_era_count_delta(current->sample_count, base->sample_count);
    bucket->sample_count += sample_delta;
    bucket->timeout_count += era_split_transport_scheduler_era_count_delta(current->timeout_count, base->timeout_count);
    bucket->tx_us_total += era_split_transport_scheduler_era_count_delta(current->tx_us_total, base->tx_us_total);
    bucket->wait_rx_us_total += era_split_transport_scheduler_era_count_delta(current->wait_rx_us_total, base->wait_rx_us_total);
    bucket->rx_decode_us_total += era_split_transport_scheduler_era_count_delta(current->rx_decode_us_total, base->rx_decode_us_total);
    bucket->total_us_total += era_split_transport_scheduler_era_count_delta(current->total_us_total, base->total_us_total);
    if (sample_delta != 0) {
        bucket->total_us_last = current->total_us_last;
    }
}
#    endif

static void era_split_transport_scheduler_transaction_counts_from_snapshot(era_split_transport_scheduler_transaction_counts_t *counts, const era_split_transaction_engine_diagnostics_t *io) {
    if (counts == NULL) {
        return;
    }

    memset(counts, 0, sizeof(*counts));
    if (io != NULL) {
        counts->compact_tx_count   = io->compact_tx_count;
        counts->compact_rx_count   = io->compact_rx_count;
        counts->compact_miss_count = io->compact_miss_count;
        counts->compact_bad_count  = io->compact_bad_count;
        counts->compact_fail_count = io->compact_fail_count;
    }
}

#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
static void era_split_transport_scheduler_transaction_timing_counts_from_snapshot(era_split_transport_scheduler_transaction_timing_counts_t *counts, const era_split_transaction_engine_diagnostics_t *io) {
    if (counts == NULL) {
        return;
    }

    memset(counts, 0, sizeof(*counts));
    if (io != NULL) {
        counts->timing_sample_count  = io->timing_sample_count;
        counts->timing_timeout_count = io->timing_timeout_count;
        for (uint8_t index = 0; index < ERA_SPLIT_TRANSACTION_TIMING_BUCKET_COUNT; index++) {
            counts->timing_buckets[index] = io->timing_buckets[index];
        }
    }
}
#    endif

static void era_split_transport_scheduler_transaction_counts_accumulate_delta(era_split_transport_scheduler_transaction_counts_t       *counts,
                                                                              const era_split_transport_scheduler_transaction_counts_t *current,
                                                                              const era_split_transport_scheduler_transaction_counts_t *base) {
    if (counts == NULL || current == NULL || base == NULL) {
        return;
    }

    counts->compact_tx_count += era_split_transport_scheduler_era_count_delta(current->compact_tx_count, base->compact_tx_count);
    counts->compact_rx_count += era_split_transport_scheduler_era_count_delta(current->compact_rx_count, base->compact_rx_count);
    counts->compact_miss_count += era_split_transport_scheduler_era_count_delta(current->compact_miss_count, base->compact_miss_count);
    counts->compact_bad_count += era_split_transport_scheduler_era_count_delta(current->compact_bad_count, base->compact_bad_count);
    counts->compact_fail_count += era_split_transport_scheduler_era_count_delta(current->compact_fail_count, base->compact_fail_count);
}

#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
static void era_split_transport_scheduler_transaction_timing_counts_accumulate_delta(era_split_transport_scheduler_transaction_timing_counts_t       *counts,
                                                                                     const era_split_transport_scheduler_transaction_timing_counts_t *current,
                                                                                     const era_split_transport_scheduler_transaction_timing_counts_t *base) {
    if (counts == NULL || current == NULL || base == NULL) {
        return;
    }

    counts->timing_sample_count += era_split_transport_scheduler_era_count_delta(current->timing_sample_count, base->timing_sample_count);
    counts->timing_timeout_count += era_split_transport_scheduler_era_count_delta(current->timing_timeout_count, base->timing_timeout_count);
    for (uint8_t index = 0; index < ERA_SPLIT_TRANSACTION_TIMING_BUCKET_COUNT; index++) {
        era_split_transport_scheduler_timing_bucket_accumulate_delta(&counts->timing_buckets[index], &current->timing_buckets[index], &base->timing_buckets[index]);
    }
}
#    endif

#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
static bool era_split_transport_scheduler_transaction_timing_from_snapshot_since(const era_split_transaction_engine_diagnostics_t          *io,
                                                                                const era_split_transport_scheduler_transaction_timing_counts_t *base,
                                                                                era_split_transaction_timing_diagnostics_t                      *timing) {
    if (timing == NULL) {
        return false;
    }

    memset(timing, 0, sizeof(*timing));
    if (io == NULL || base == NULL || !io->timing.valid || io->timing_sample_count == base->timing_sample_count) {
        return false;
    }

    *timing = io->timing;
    return true;
}

/* Read the published transaction-engine mirror for an *era boundary*.
 *
 * `era_split_transaction_engine_get_diagnostics_snapshot` falls back to the
 * last proven snapshot when it cannot prove a stable publication. That is the
 * right answer for a display line, which wants the last known state rather
 * than a hole, and the wrong one for a difference: an era block subtracts two
 * of these reads, and a fallback silently turns the subtraction into a
 * measurement it never made.
 *
 * It is worse than an ordinary wrong number, and that is why this exists.
 * Every caller of the mirror is a boundary read or a print, so nothing
 * refreshes the fallback's stored snapshot between an activate and a capture
 * — a collision at the capture therefore returns *precisely* the bytes the
 * activate stored as the base, and the delta comes out identically zero. It
 * reads as "this era carried no traffic", which is the same shape as the
 * healthy steady-state reading of the same fields.
 *
 * Device-shown 2026-07-30 on a DUAL-HOST exit: the initiator half reported
 * `io=0/0/0/0/0` across a window its own session counters put at +411 tx and
 * +381 rx, while the responder half read the same exit correctly. The
 * initiator is the exposed one by construction — at a cable removal its core1
 * is republishing on every timing-out transaction, which is exactly when this
 * read runs.
 *
 * So retry, and if the retries cannot prove one, say so rather than
 * subtracting it. The caller marks the block and the line reports `meas=0`.
 * Both call sites are cold (a mode edge and print time), so the attempts cost
 * nothing that matters. */
enum { ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_MIRROR_ATTEMPTS = 4 };

static bool era_split_transport_scheduler_read_era_mirror(era_split_transaction_engine_diagnostics_t *io) {
    for (uint8_t attempt = 0; attempt < ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_MIRROR_ATTEMPTS; attempt++) {
        era_split_transaction_engine_get_diagnostics_snapshot(io);
        if (era_split_transaction_engine_diagnostics_snapshot_fresh()) {
            return true;
        }
    }
    return false;
}

/* The bit says "the value this block is about to report crossed a boundary it
 * could not measure", so it is only ever raised at a boundary and is cleared
 * exactly where the accumulator it describes is cleared. Clearing it on a
 * later successful boundary instead would erase the marker on a
 * DUAL_HOST_LEFT -> DUAL_HOST_RIGHT edge, where one block is captured and
 * re-activated in the same call while its accumulated value survives. */
static void era_split_transport_scheduler_note_era_boundary(uint8_t block_bit, bool fresh) {
    if (!fresh) {
        g_era_split_transport_scheduler_role_diagnostics.boundary_lost |= block_bit;
    }
}

static void era_split_transport_scheduler_clear_era_boundary(uint8_t block_bit) {
    g_era_split_transport_scheduler_role_diagnostics.boundary_lost &= (uint8_t)~block_bit;
}

static void era_split_transport_scheduler_capture_peer_era_timing_snapshot(void) {
    era_split_transaction_engine_diagnostics_t                  io;
    era_split_transaction_timing_diagnostics_t timing;
    era_split_transport_scheduler_note_era_boundary(ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_PEER,
                                                    era_split_transport_scheduler_read_era_mirror(&io));
    if (era_split_transport_scheduler_transaction_timing_from_snapshot_since(&io, &g_era_split_transport_scheduler_role_diagnostics.peer_era.base.timing_io, &timing)) {
        g_era_split_transport_scheduler_role_diagnostics.peer_era.timing = timing;
    }
}
#    endif

static bool era_split_transport_scheduler_get_peer_era_counts(era_split_transport_scheduler_peer_era_counts_t *counts, uint8_t *local_current_seq8, uint8_t *local_host_known_seq8) {
    era_split_transaction_engine_diagnostics_t io;
    era_host_peer_matrix_link_diagnostics_t    host_peer;
    bool                                       fresh = era_split_transport_scheduler_read_era_mirror(&io);
    era_host_peer_matrix_link_get_diagnostics_snapshot(&host_peer);

    if (counts != NULL) {
        counts->source_push_tx_count  = host_peer.source_push_tx_count;
        counts->source_push_ack_count = host_peer.source_push_ack_count;
        era_split_transport_scheduler_transaction_counts_from_snapshot(&counts->io, &io);
#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
        era_split_transport_scheduler_transaction_timing_counts_from_snapshot(&counts->timing_io, &io);
#    endif
    }
    if (local_current_seq8 != NULL) {
        *local_current_seq8 = host_peer.local_current_seq8;
    }
    if (local_host_known_seq8 != NULL) {
        *local_host_known_seq8 = host_peer.local_host_known_seq8;
    }
    return fresh;
}

static void era_split_transport_scheduler_peer_era_counts_accumulate_delta(era_split_transport_scheduler_peer_era_counts_t       *counts,
                                                                           const era_split_transport_scheduler_peer_era_counts_t *current,
                                                                           const era_split_transport_scheduler_peer_era_counts_t *base) {
    if (counts == NULL || current == NULL || base == NULL) {
        return;
    }

    counts->source_push_tx_count += era_split_transport_scheduler_era_count_delta(current->source_push_tx_count, base->source_push_tx_count);
    counts->source_push_ack_count += era_split_transport_scheduler_era_count_delta(current->source_push_ack_count, base->source_push_ack_count);
    era_split_transport_scheduler_transaction_counts_accumulate_delta(&counts->io, &current->io, &base->io);
#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transport_scheduler_transaction_timing_counts_accumulate_delta(&counts->timing_io, &current->timing_io, &base->timing_io);
#    endif
}

static void era_split_transport_scheduler_activate_peer_era_snapshot(era_split_mode_t peer_mode) {
    bool fresh = era_split_transport_scheduler_get_peer_era_counts(&g_era_split_transport_scheduler_role_diagnostics.peer_era.base,
                                                      &g_era_split_transport_scheduler_role_diagnostics.peer_era.local_current_seq8,
                                                      &g_era_split_transport_scheduler_role_diagnostics.peer_era.local_host_known_seq8);
    era_split_transport_scheduler_note_era_boundary(ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_PEER, fresh);

    g_era_split_transport_scheduler_role_diagnostics.peer_era.valid          = true;
    g_era_split_transport_scheduler_role_diagnostics.peer_era.active         = true;
    g_era_split_transport_scheduler_role_diagnostics.peer_era.last_peer_mode = (uint8_t)peer_mode;
#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    memset(&g_era_split_transport_scheduler_role_diagnostics.peer_era.timing, 0, sizeof(g_era_split_transport_scheduler_role_diagnostics.peer_era.timing));
#    endif
}

static void era_split_transport_scheduler_capture_peer_era_snapshot(era_split_mode_t peer_mode) {
    era_split_transport_scheduler_peer_era_counts_t current;
    if (!g_era_split_transport_scheduler_role_diagnostics.peer_era.active) {
        return;
    }

    bool fresh = era_split_transport_scheduler_get_peer_era_counts(&current,
                                                      &g_era_split_transport_scheduler_role_diagnostics.peer_era.local_current_seq8,
                                                      &g_era_split_transport_scheduler_role_diagnostics.peer_era.local_host_known_seq8);
    era_split_transport_scheduler_note_era_boundary(ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_PEER, fresh);
    g_era_split_transport_scheduler_role_diagnostics.peer_era.valid          = true;
    g_era_split_transport_scheduler_role_diagnostics.peer_era.active         = false;
    g_era_split_transport_scheduler_role_diagnostics.peer_era.last_peer_mode = (uint8_t)peer_mode;
    era_split_transport_scheduler_peer_era_counts_accumulate_delta(&g_era_split_transport_scheduler_role_diagnostics.peer_era.accum,
                                                                   &current,
                                                                   &g_era_split_transport_scheduler_role_diagnostics.peer_era.base);
#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transport_scheduler_capture_peer_era_timing_snapshot();
#    endif
}

static void __attribute__((noinline)) era_split_transport_scheduler_get_peer_era_snapshot_counts(era_split_transport_scheduler_peer_era_counts_t *counts) {
    if (counts == NULL) {
        return;
    }

    *counts = g_era_split_transport_scheduler_role_diagnostics.peer_era.accum;
    if (g_era_split_transport_scheduler_role_diagnostics.peer_era.active) {
        era_split_transport_scheduler_peer_era_counts_t current;
        era_split_transport_scheduler_get_peer_era_counts(&current,
                                                          &g_era_split_transport_scheduler_role_diagnostics.peer_era.local_current_seq8,
                                                          &g_era_split_transport_scheduler_role_diagnostics.peer_era.local_host_known_seq8);
        era_split_transport_scheduler_peer_era_counts_accumulate_delta(counts, &current, &g_era_split_transport_scheduler_role_diagnostics.peer_era.base);
    }
}

#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
static void __attribute__((noinline)) era_split_transport_scheduler_get_peer_era_timing_snapshot(era_split_transaction_timing_diagnostics_t *timing) {
    if (timing == NULL) {
        return;
    }

    *timing = g_era_split_transport_scheduler_role_diagnostics.peer_era.timing;
    if (g_era_split_transport_scheduler_role_diagnostics.peer_era.active) {
        era_split_transaction_engine_diagnostics_t io;
        era_split_transaction_engine_get_diagnostics_snapshot(&io);
        (void)era_split_transport_scheduler_transaction_timing_from_snapshot_since(&io, &g_era_split_transport_scheduler_role_diagnostics.peer_era.base.timing_io, timing);
    }
}
#    endif

static bool era_split_transport_scheduler_get_host_era_counts(era_split_transport_scheduler_host_era_counts_t *counts, uint8_t *peer_cache_valid, uint8_t *peer_matrix_seq8) {
    era_split_transaction_engine_diagnostics_t io;
    era_split_responder_projection_t          responder;
    era_host_peer_matrix_link_diagnostics_t    host_peer;
    bool                                       fresh = era_split_transport_scheduler_read_era_mirror(&io);
    era_split_responder_projection_get(&responder);
    era_host_peer_matrix_link_get_diagnostics_snapshot(&host_peer);

    if (counts != NULL) {
        counts->ack_status_tx_count                      = host_peer.ack_status_tx_count;
        counts->peer_cache_update_count                  = host_peer.peer_cache_update_count;
        counts->peer_cache_project_count                 = host_peer.peer_cache_project_count;
        counts->peer_cache_flush_count                   = host_peer.peer_cache_flush_count;
        counts->responder_relation_request_rx_count      = responder.relation_request_rx_count;
        counts->responder_host_peer_heartbeat_rx_count   = responder.host_peer_heartbeat_rx_count;
        counts->responder_host_peer_source_push_rx_count = responder.host_peer_source_push_rx_count;
        era_split_transport_scheduler_transaction_counts_from_snapshot(&counts->io, &io);
    }
    if (peer_cache_valid != NULL) {
        *peer_cache_valid = host_peer.peer_cache_valid;
    }
    if (peer_matrix_seq8 != NULL) {
        *peer_matrix_seq8 = host_peer.peer_matrix_seq8;
    }
    return fresh;
}

static void era_split_transport_scheduler_host_era_counts_accumulate_delta(era_split_transport_scheduler_host_era_counts_t       *counts,
                                                                           const era_split_transport_scheduler_host_era_counts_t *current,
                                                                           const era_split_transport_scheduler_host_era_counts_t *base) {
    if (counts == NULL || current == NULL || base == NULL) {
        return;
    }

    counts->ack_status_tx_count += era_split_transport_scheduler_era_count_delta(current->ack_status_tx_count, base->ack_status_tx_count);
    counts->peer_cache_update_count += era_split_transport_scheduler_era_count_delta(current->peer_cache_update_count, base->peer_cache_update_count);
    counts->peer_cache_project_count += era_split_transport_scheduler_era_count_delta(current->peer_cache_project_count, base->peer_cache_project_count);
    counts->peer_cache_flush_count += era_split_transport_scheduler_era_count_delta(current->peer_cache_flush_count, base->peer_cache_flush_count);
    counts->responder_relation_request_rx_count += era_split_transport_scheduler_era_count_delta(current->responder_relation_request_rx_count, base->responder_relation_request_rx_count);
    counts->responder_host_peer_heartbeat_rx_count += era_split_transport_scheduler_era_count_delta(current->responder_host_peer_heartbeat_rx_count, base->responder_host_peer_heartbeat_rx_count);
    counts->responder_host_peer_source_push_rx_count += era_split_transport_scheduler_era_count_delta(current->responder_host_peer_source_push_rx_count, base->responder_host_peer_source_push_rx_count);
    era_split_transport_scheduler_transaction_counts_accumulate_delta(&counts->io, &current->io, &base->io);
}

static void era_split_transport_scheduler_activate_host_era_snapshot(era_split_mode_t host_mode) {
    bool fresh = era_split_transport_scheduler_get_host_era_counts(&g_era_split_transport_scheduler_role_diagnostics.host_era.base,
                                                      &g_era_split_transport_scheduler_role_diagnostics.host_era.peer_cache_valid,
                                                      &g_era_split_transport_scheduler_role_diagnostics.host_era.peer_matrix_seq8);
    era_split_transport_scheduler_note_era_boundary(ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_HOST, fresh);

    g_era_split_transport_scheduler_role_diagnostics.host_era.valid          = true;
    g_era_split_transport_scheduler_role_diagnostics.host_era.active         = true;
    g_era_split_transport_scheduler_role_diagnostics.host_era.last_host_mode = (uint8_t)host_mode;
}

static void era_split_transport_scheduler_capture_host_era_snapshot(era_split_mode_t host_mode) {
    era_split_transport_scheduler_host_era_counts_t current;
    if (!g_era_split_transport_scheduler_role_diagnostics.host_era.active) {
        return;
    }

    bool fresh = era_split_transport_scheduler_get_host_era_counts(&current,
                                                      &g_era_split_transport_scheduler_role_diagnostics.host_era.peer_cache_valid,
                                                      &g_era_split_transport_scheduler_role_diagnostics.host_era.peer_matrix_seq8);
    era_split_transport_scheduler_note_era_boundary(ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_HOST, fresh);
    g_era_split_transport_scheduler_role_diagnostics.host_era.valid          = true;
    g_era_split_transport_scheduler_role_diagnostics.host_era.active         = false;
    g_era_split_transport_scheduler_role_diagnostics.host_era.last_host_mode = (uint8_t)host_mode;
    era_split_transport_scheduler_host_era_counts_accumulate_delta(&g_era_split_transport_scheduler_role_diagnostics.host_era.accum,
                                                                   &current,
                                                                   &g_era_split_transport_scheduler_role_diagnostics.host_era.base);
}

static void __attribute__((noinline)) era_split_transport_scheduler_get_host_era_snapshot_counts(era_split_transport_scheduler_host_era_counts_t *counts) {
    if (counts == NULL) {
        return;
    }

    *counts = g_era_split_transport_scheduler_role_diagnostics.host_era.accum;
    if (g_era_split_transport_scheduler_role_diagnostics.host_era.active) {
        era_split_transport_scheduler_host_era_counts_t current;
        era_split_transport_scheduler_get_host_era_counts(&current,
                                                          &g_era_split_transport_scheduler_role_diagnostics.host_era.peer_cache_valid,
                                                          &g_era_split_transport_scheduler_role_diagnostics.host_era.peer_matrix_seq8);
        era_split_transport_scheduler_host_era_counts_accumulate_delta(counts, &current, &g_era_split_transport_scheduler_role_diagnostics.host_era.base);
    }
}

/* The two snapshots this block needs are read by sibling noinline leaves, so
 * the ~176-byte transaction-engine record and the 88-byte storage record
 * never hold a frame at the same time. Nested, they would have made this the
 * deepest frame on a print path that already carries the deepest one; as
 * siblings the peak is the engine snapshot alone, which is exactly what the
 * existing HOST-PEER era getters already cost. Both paths that reach here
 * are cold - a mode edge and print time - so the extra call is free. */
static bool __attribute__((noinline)) era_split_transport_scheduler_note_dual_host_era_io_counts(era_split_transport_scheduler_dual_host_era_counts_t *counts) {
    era_split_transaction_engine_diagnostics_t io;
    bool                                       fresh = era_split_transport_scheduler_read_era_mirror(&io);
    era_split_transport_scheduler_transaction_counts_from_snapshot(&counts->io, &io);
    return fresh;
}

static void __attribute__((noinline)) era_split_transport_scheduler_note_dual_host_era_storage_counts(era_split_transport_scheduler_dual_host_era_counts_t *counts) {
#    ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    era_host_peer_storage_diagnostics_t storage;
    era_host_peer_storage_get_diagnostics_snapshot(&storage);
    counts->storage_open_count     = storage.open_count;
    counts->storage_close_count    = storage.close_count;
    counts->storage_transfer_count = storage.transfer_count;
#    else
    (void)counts;
#    endif
    /* Scheduler-owned and read directly, like the storage half: a lost
     * mirror boundary falsifies `io` toward zero and must not falsify the
     * runtime pair the same way, because zero is this pair's pass value. */
    era_split_transport_scheduler_get_dual_runtime_counts(&counts->runtime_tx_count, &counts->runtime_rx_count);
}

static bool era_split_transport_scheduler_get_dual_host_era_counts(era_split_transport_scheduler_dual_host_era_counts_t *counts) {
    if (counts == NULL) {
        return false;
    }

    memset(counts, 0, sizeof(*counts));
    /* Only the io half crosses the fallback-capable mirror. The storage half
     * is read straight from its own snapshot, so a lost boundary falsifies
     * `io` and leaves `stor` correct. */
    bool fresh = era_split_transport_scheduler_note_dual_host_era_io_counts(counts);
    era_split_transport_scheduler_note_dual_host_era_storage_counts(counts);
    return fresh;
}

static void era_split_transport_scheduler_dual_host_era_counts_accumulate_delta(era_split_transport_scheduler_dual_host_era_counts_t       *counts,
                                                                                const era_split_transport_scheduler_dual_host_era_counts_t *current,
                                                                                const era_split_transport_scheduler_dual_host_era_counts_t *base) {
    if (counts == NULL || current == NULL || base == NULL) {
        return;
    }

    era_split_transport_scheduler_transaction_counts_accumulate_delta(&counts->io, &current->io, &base->io);
    counts->storage_open_count += era_split_transport_scheduler_era_count_delta(current->storage_open_count, base->storage_open_count);
    counts->storage_close_count += era_split_transport_scheduler_era_count_delta(current->storage_close_count, base->storage_close_count);
    counts->storage_transfer_count += era_split_transport_scheduler_era_count_delta(current->storage_transfer_count, base->storage_transfer_count);
    counts->runtime_tx_count += era_split_transport_scheduler_era_count_delta(current->runtime_tx_count, base->runtime_tx_count);
    counts->runtime_rx_count += era_split_transport_scheduler_era_count_delta(current->runtime_rx_count, base->runtime_rx_count);
}

static void era_split_transport_scheduler_activate_dual_host_era_snapshot(era_split_mode_t dual_host_mode) {
    era_split_transport_scheduler_note_era_boundary(
        ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_DUAL_HOST,
        era_split_transport_scheduler_get_dual_host_era_counts(&g_era_split_transport_scheduler_role_diagnostics.dual_host_era.base));
    g_era_split_transport_scheduler_role_diagnostics.dual_host_era.valid     = true;
    g_era_split_transport_scheduler_role_diagnostics.dual_host_era.active    = true;
    g_era_split_transport_scheduler_role_diagnostics.dual_host_era.last_mode = (uint8_t)dual_host_mode;
}

static void era_split_transport_scheduler_capture_dual_host_era_snapshot(era_split_mode_t dual_host_mode) {
    era_split_transport_scheduler_dual_host_era_counts_t current;
    if (!g_era_split_transport_scheduler_role_diagnostics.dual_host_era.active) {
        return;
    }

    era_split_transport_scheduler_note_era_boundary(ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_DUAL_HOST,
                                                    era_split_transport_scheduler_get_dual_host_era_counts(&current));
    g_era_split_transport_scheduler_role_diagnostics.dual_host_era.valid     = true;
    g_era_split_transport_scheduler_role_diagnostics.dual_host_era.active    = false;
    g_era_split_transport_scheduler_role_diagnostics.dual_host_era.last_mode = (uint8_t)dual_host_mode;
    era_split_transport_scheduler_dual_host_era_counts_accumulate_delta(&g_era_split_transport_scheduler_role_diagnostics.dual_host_era.accum,
                                                                        &current,
                                                                        &g_era_split_transport_scheduler_role_diagnostics.dual_host_era.base);
}

static void __attribute__((noinline)) era_split_transport_scheduler_get_dual_host_era_snapshot_counts(era_split_transport_scheduler_dual_host_era_counts_t *counts) {
    if (counts == NULL) {
        return;
    }

    *counts = g_era_split_transport_scheduler_role_diagnostics.dual_host_era.accum;
    if (g_era_split_transport_scheduler_role_diagnostics.dual_host_era.active) {
        era_split_transport_scheduler_dual_host_era_counts_t current;
        era_split_transport_scheduler_get_dual_host_era_counts(&current);
        era_split_transport_scheduler_dual_host_era_counts_accumulate_delta(counts, &current, &g_era_split_transport_scheduler_role_diagnostics.dual_host_era.base);
    }
}

static bool era_split_transport_scheduler_mode_is_dual_host(era_split_mode_t mode) {
    return mode == ERA_SPLIT_MODE_DUAL_HOST_LEFT || mode == ERA_SPLIT_MODE_DUAL_HOST_RIGHT;
}

void era_split_transport_scheduler_role_diagnostics_note_mode_change(era_split_mode_t previous_mode, era_split_mode_t next_mode) {
    if (previous_mode == next_mode) {
        return;
    }

    switch (previous_mode) {
        case ERA_SPLIT_MODE_HOST_PEER_PEER:
            era_split_transport_scheduler_capture_peer_era_snapshot(previous_mode);
            break;
        case ERA_SPLIT_MODE_HOST_PEER_HOST:
            era_split_transport_scheduler_capture_host_era_snapshot(previous_mode);
            break;
        default:
            break;
    }
    /* Both DUAL-HOST modes share one block: a half holds exactly one of them
     * and the value is recorded in `last_mode`, so splitting them would give
     * every capture one populated block and one permanently empty one. */
    if (era_split_transport_scheduler_mode_is_dual_host(previous_mode)) {
        era_split_transport_scheduler_capture_dual_host_era_snapshot(previous_mode);
    }

    switch (next_mode) {
        case ERA_SPLIT_MODE_HOST_PEER_PEER:
            era_split_transport_scheduler_activate_peer_era_snapshot(next_mode);
            break;
        case ERA_SPLIT_MODE_HOST_PEER_HOST:
            era_split_transport_scheduler_activate_host_era_snapshot(next_mode);
            break;
        default:
            break;
    }
    if (era_split_transport_scheduler_mode_is_dual_host(next_mode)) {
        era_split_transport_scheduler_activate_dual_host_era_snapshot(next_mode);
    }
}

void era_split_transport_scheduler_role_diagnostics_write_snapshot(era_split_transport_scheduler_diagnostics_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    era_split_transport_scheduler_get_peer_era_snapshot_counts(&g_era_split_transport_scheduler_role_diagnostics_write_scratch.peer_era_counts);
    era_split_transport_scheduler_get_host_era_snapshot_counts(&g_era_split_transport_scheduler_role_diagnostics_write_scratch.host_era_counts);
    era_split_transport_scheduler_get_dual_host_era_snapshot_counts(&g_era_split_transport_scheduler_role_diagnostics_write_scratch.dual_host_era_counts);
#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    era_split_transport_scheduler_get_peer_era_timing_snapshot(&g_era_split_transport_scheduler_role_diagnostics_write_scratch.peer_era_timing);
#    endif

    const era_split_transport_scheduler_peer_era_counts_t *peer_era_counts = &g_era_split_transport_scheduler_role_diagnostics_write_scratch.peer_era_counts;
    const era_split_transport_scheduler_host_era_counts_t *host_era_counts = &g_era_split_transport_scheduler_role_diagnostics_write_scratch.host_era_counts;
#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    const era_split_transaction_timing_diagnostics_t *peer_era_timing = &g_era_split_transport_scheduler_role_diagnostics_write_scratch.peer_era_timing;
#    endif

    snapshot->host_peer_peer_era.valid                                    = g_era_split_transport_scheduler_role_diagnostics.peer_era.valid ? 1 : 0;
    snapshot->host_peer_peer_era.last_mode                                = g_era_split_transport_scheduler_role_diagnostics.peer_era.last_peer_mode;
    snapshot->host_peer_peer_era.measured                                 = (g_era_split_transport_scheduler_role_diagnostics.boundary_lost & ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_PEER) == 0 ? 1 : 0;
    snapshot->host_peer_peer_era_local_current_seq8                       = g_era_split_transport_scheduler_role_diagnostics.peer_era.local_current_seq8;
    snapshot->host_peer_peer_era_local_host_known_seq8                    = g_era_split_transport_scheduler_role_diagnostics.peer_era.local_host_known_seq8;
    snapshot->host_peer_peer_era_source_push_tx_count                     = peer_era_counts->source_push_tx_count;
    snapshot->host_peer_peer_era_source_push_ack_count                    = peer_era_counts->source_push_ack_count;
    snapshot->host_peer_peer_era.compact_tx_count                         = peer_era_counts->io.compact_tx_count;
    snapshot->host_peer_peer_era.compact_rx_count                         = peer_era_counts->io.compact_rx_count;
    snapshot->host_peer_peer_era.compact_miss_count                       = peer_era_counts->io.compact_miss_count;
    snapshot->host_peer_peer_era.compact_bad_count                        = peer_era_counts->io.compact_bad_count;
    snapshot->host_peer_peer_era.compact_fail_count                       = peer_era_counts->io.compact_fail_count;
#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    snapshot->host_peer_peer_era_timing_sample_count                      = peer_era_counts->timing_io.timing_sample_count;
    snapshot->host_peer_peer_era_timing_timeout_count                     = peer_era_counts->timing_io.timing_timeout_count;
    snapshot->host_peer_peer_era_timing                                   = *peer_era_timing;
    for (uint8_t index = 0; index < ERA_SPLIT_TRANSACTION_TIMING_BUCKET_COUNT; index++) {
        snapshot->host_peer_peer_era_timing_buckets[index] = peer_era_counts->timing_io.timing_buckets[index];
    }
#    endif
    snapshot->host_peer_host_era.valid                                    = g_era_split_transport_scheduler_role_diagnostics.host_era.valid ? 1 : 0;
    snapshot->host_peer_host_era.last_mode                                = g_era_split_transport_scheduler_role_diagnostics.host_era.last_host_mode;
    snapshot->host_peer_host_era.measured                                 = (g_era_split_transport_scheduler_role_diagnostics.boundary_lost & ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_HOST) == 0 ? 1 : 0;
    snapshot->host_peer_host_era_peer_cache_valid                         = g_era_split_transport_scheduler_role_diagnostics.host_era.peer_cache_valid;
    snapshot->host_peer_host_era_peer_matrix_seq8                         = g_era_split_transport_scheduler_role_diagnostics.host_era.peer_matrix_seq8;
    snapshot->host_peer_host_era_ack_status_tx_count                      = host_era_counts->ack_status_tx_count;
    snapshot->host_peer_host_era_peer_cache_update_count                  = host_era_counts->peer_cache_update_count;
    snapshot->host_peer_host_era_peer_cache_project_count                 = host_era_counts->peer_cache_project_count;
    snapshot->host_peer_host_era_peer_cache_flush_count                   = host_era_counts->peer_cache_flush_count;
    snapshot->host_peer_host_era_responder_relation_request_rx_count      = host_era_counts->responder_relation_request_rx_count;
    snapshot->host_peer_host_era_responder_host_peer_heartbeat_rx_count   = host_era_counts->responder_host_peer_heartbeat_rx_count;
    snapshot->host_peer_host_era_responder_host_peer_source_push_rx_count = host_era_counts->responder_host_peer_source_push_rx_count;
    snapshot->host_peer_host_era.compact_tx_count                         = host_era_counts->io.compact_tx_count;
    snapshot->host_peer_host_era.compact_rx_count                         = host_era_counts->io.compact_rx_count;
    snapshot->host_peer_host_era.compact_miss_count                       = host_era_counts->io.compact_miss_count;
    snapshot->host_peer_host_era.compact_bad_count                        = host_era_counts->io.compact_bad_count;
    snapshot->host_peer_host_era.compact_fail_count                       = host_era_counts->io.compact_fail_count;

    const era_split_transport_scheduler_dual_host_era_counts_t *dual_host_era_counts = &g_era_split_transport_scheduler_role_diagnostics_write_scratch.dual_host_era_counts;
    snapshot->dual_host_era.valid                = g_era_split_transport_scheduler_role_diagnostics.dual_host_era.valid ? 1 : 0;
    snapshot->dual_host_era.last_mode            = g_era_split_transport_scheduler_role_diagnostics.dual_host_era.last_mode;
    snapshot->dual_host_era.measured             = (g_era_split_transport_scheduler_role_diagnostics.boundary_lost & ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_DUAL_HOST) == 0 ? 1 : 0;
    snapshot->dual_host_era.compact_tx_count     = dual_host_era_counts->io.compact_tx_count;
    snapshot->dual_host_era.compact_rx_count     = dual_host_era_counts->io.compact_rx_count;
    snapshot->dual_host_era.compact_miss_count   = dual_host_era_counts->io.compact_miss_count;
    snapshot->dual_host_era.compact_bad_count    = dual_host_era_counts->io.compact_bad_count;
    snapshot->dual_host_era.compact_fail_count   = dual_host_era_counts->io.compact_fail_count;
    snapshot->dual_host_era_storage_open_count   = dual_host_era_counts->storage_open_count;
    snapshot->dual_host_era_storage_close_count  = dual_host_era_counts->storage_close_count;
    snapshot->dual_host_era_storage_transfer_count = dual_host_era_counts->storage_transfer_count;
    snapshot->dual_host_era_runtime_tx_count       = dual_host_era_counts->runtime_tx_count;
    snapshot->dual_host_era_runtime_rx_count       = dual_host_era_counts->runtime_rx_count;
}

void era_split_transport_scheduler_role_diagnostics_reset_baselines(era_split_mode_t current_mode) {
    /* The lost-boundary marker describes the accumulator, so it is cleared
     * here with it and nowhere else. Each activate below may raise it again
     * for the era it is starting. */
    if (g_era_split_transport_scheduler_role_diagnostics.peer_era.valid) {
        memset(&g_era_split_transport_scheduler_role_diagnostics.peer_era.accum, 0, sizeof(g_era_split_transport_scheduler_role_diagnostics.peer_era.accum));
        g_era_split_transport_scheduler_role_diagnostics.peer_era.active = false;
        era_split_transport_scheduler_clear_era_boundary(ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_PEER);
#    ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
        memset(&g_era_split_transport_scheduler_role_diagnostics.peer_era.timing, 0, sizeof(g_era_split_transport_scheduler_role_diagnostics.peer_era.timing));
#    endif
    }
    if (current_mode == ERA_SPLIT_MODE_HOST_PEER_PEER) {
        era_split_transport_scheduler_activate_peer_era_snapshot(current_mode);
    }

    if (g_era_split_transport_scheduler_role_diagnostics.host_era.valid) {
        memset(&g_era_split_transport_scheduler_role_diagnostics.host_era.accum, 0, sizeof(g_era_split_transport_scheduler_role_diagnostics.host_era.accum));
        g_era_split_transport_scheduler_role_diagnostics.host_era.active = false;
        era_split_transport_scheduler_clear_era_boundary(ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_HOST);
    }
    if (current_mode == ERA_SPLIT_MODE_HOST_PEER_HOST) {
        era_split_transport_scheduler_activate_host_era_snapshot(current_mode);
    }

    if (g_era_split_transport_scheduler_role_diagnostics.dual_host_era.valid) {
        memset(&g_era_split_transport_scheduler_role_diagnostics.dual_host_era.accum, 0, sizeof(g_era_split_transport_scheduler_role_diagnostics.dual_host_era.accum));
        g_era_split_transport_scheduler_role_diagnostics.dual_host_era.active = false;
        era_split_transport_scheduler_clear_era_boundary(ERA_SPLIT_TRANSPORT_SCHEDULER_ERA_LOST_DUAL_HOST);
    }
    if (era_split_transport_scheduler_mode_is_dual_host(current_mode)) {
        era_split_transport_scheduler_activate_dual_host_era_snapshot(current_mode);
    }
}

#endif
