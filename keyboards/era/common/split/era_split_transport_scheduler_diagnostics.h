// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

#include "era_split_transaction_types.h"

/* The head every relation-era block shares: whether the block ever had a
   boundary, the mode it closed in, whether that boundary was actually
   observed, and the five compact-IO counters. Three blocks carried these
   eight names each -- PEER era, HOST era, DUAL-HOST era -- and the fill was
   the same eight assignments three times. A fourth relation adds one member
   here and one fill call, not eight more fields.

   **The per-era tails below each block do not generalize the same way**, and a
   name-shape reading of this file will offer them: `host_peer_host_era_`
   repeats six live `host_peer_` suffixes and `host_peer_peer_era_` repeats
   four. Each of those pairs is a live value and the same value as it stood at
   an era boundary -- two values, not one name written twice -- and the two
   tails are different subsets, the PEER era's being what an initiator does and
   the HOST era's what a responder does. Giving either a shared type means
   splitting the live block into sub-structs whose membership is "whatever the
   era snapshot happens to copy", which puts a diagnostics reading in charge of
   the live block's shape. Counted: of the sixteen live `host_peer_` fields the
   PEER tail takes four and the HOST tail six, they interleave, and six belong
   to neither -- so the split is three sub-structs cut by a diagnostics
   membership, which is the objection made concrete. What made the fold above
   free is that the three era heads were the same eight names *as each other*,
   and no live field was touched to do it.

   **Two folds nobody has proposed are refused here too, because the shape
   invites both.** Giving each tail its own type declines: a struct with one
   instance and one filler is a namespace, and it deduplicates nothing, since
   the two tails share no member. Sharing the producer's own record is the real
   one and is the closer call --
   `diagnostics/era_split_transport_scheduler_role_diagnostics.c` already holds
   `..._peer_era_counts_t` and `..._host_era_counts_t`, and `write_snapshot()`
   copies them into these flat fields one assignment at a time, which is the
   shape the `io` member below was folded to escape.

   **The .bss argument that used to decline it did not survive being checked,
   and the fix was to move the timing block rather than to keep the types
   apart.** The argument said a shared type would put the per-bucket
   transaction timing into snapshots that never read it — which the HOST
   snapshot was already doing, twice, through the very type the argument
   defended. So the timing block now sits in `..._transaction_timing_counts_t`
   and belongs to the PEER counts alone
   (`diagnostics/era_split_transport_scheduler_role_diagnostics.c`), all three
   era blocks share `..._transaction_counts_t` for the compact five, and the
   move recovered 360 bytes of `.bss` on the console profiles that nothing had
   ever read.

   What that leaves declined is only the flattening below: `write_snapshot()`
   copies the producer's records into these flat fields one assignment at a
   time, and a tail that embedded the producer's types instead would tie the
   console's published shape to the producer's internal one. The tail is a
   published surface and the counts are not, which is a reason the .bss
   argument never was. */
typedef struct {
    uint8_t  valid;
    uint8_t  last_mode;
    /* 0 when a boundary read of this era could not prove a stable engine
     * publication: the deltas in this block are not a measurement. Prints as
     * `meas=`; semantics in era_capture_reading.md. This member is shared by
     * the PEER, HOST and DUAL-HOST blocks, which is why the comment is here
     * and not on one of them. */
    uint8_t  measured;
    uint32_t compact_tx_count;
    uint32_t compact_rx_count;
    uint32_t compact_miss_count;
    uint32_t compact_bad_count;
    uint32_t compact_fail_count;
} era_split_transport_scheduler_era_common_t;

typedef struct {
    uint8_t  scheduler_enabled;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    uint8_t  transaction_engine_diagnostics_fresh;
    uint32_t transaction_engine_diagnostics_fallback_count;
#endif
    /* The transaction engine's own diagnostics record, whole. It used to be
       flattened into this struct -- four backend fields here and the compact
       and timing block further down -- which is one record declared twice and
       filled a field at a time. `tx_seq`/`ack_seq` arrive with it and no
       printer reads them; two unread bytes in a compiled-out struct is the
       price of the record having one declaration. */
    era_split_transaction_engine_diagnostics_t io;
    uint8_t  relation;
    uint8_t  previous_relation;
    uint8_t  owner_route_kind;
    uint8_t  owner_route_reason;
    uint32_t transport_step_call_count;
    uint32_t scheduler_init_call_count;
    uint32_t scheduler_housekeeping_task_count;
    uint32_t scheduler_plan_count;
    uint8_t  scheduler_dirty_flags;
    uint8_t  scheduler_route_due_flags;
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
    uint16_t communication_core_start_entry_ms;
    uint16_t communication_core_start_exit_ms;
#endif
    uint32_t owner_step_count;
    uint8_t  responder_thread_started;
    uint8_t  responder_service_enabled;
    uint8_t  responder_admission_blocked;
    uint8_t  responder_reset_requested;
    uint8_t  responder_in_serial_io;
    uint8_t  responder_in_transaction;
    uint8_t  responder_quiesced;
    uint32_t responder_frame_rx_count;
    uint32_t responder_relation_request_rx_count;
    uint32_t responder_session_request_rx_count;
    uint32_t responder_session_response_tx_count;
    uint32_t responder_host_peer_heartbeat_rx_count;
    uint32_t responder_host_peer_source_push_rx_count;
    uint32_t responder_host_peer_visual_snapshot_tx_count;
    uint32_t responder_host_peer_rgb_state_tx_count;
    uint32_t responder_ignored_frame_count;
    uint8_t  peer_session_known;
    uint8_t  peer_accepted_host_open;
    uint8_t  peer_accepted_no_host;
    uint8_t  peer_matrix_ready;
    uint8_t  peer_bulk_page_supported;
    /* The peer's advertised storage news value, as this half last took it off
     * the relation's lane. It prints as `pnews=` on `wire sess`; read it
     * against the peer's own `news=` on `wire storage`, and only for equality.
     * era_capture_reading.md is the authority on that reading -- decoding a
     * console column is that manual's, per era_identifier_map.md's own scope
     * note -- and there is nothing else to derive from it: since D2 the value
     * carries no domain identity at all.
     *
     * This block used to describe the retired `phint` bit -- one advisory flag
     * on SESSION_STATUS, read as a wire *level*, with `arb` bit `0x08` as this
     * half's rising-edge latch for it -- and it told the reader to pair the
     * two. Slice 11.7 deleted the bit and the latch together and moved the
     * fact onto the relation's own lane, and D2 then replaced the per-domain
     * mask 11.7 shipped with a forward-only news value. So both halves of that
     * instruction are dead: bit `0x08` is retired and left unreused
     * (the `arb` enum in era_host_peer_storage.c), and a capture whose `arb`
     * ever sets it is a pre-11.7 image. What is worth reading beside this
     * field on `wire storage recency` is `arb` itself, because a news change
     * is what arms SUMMARY_PENDING.
     *
     * The comment sat *below* its field until this correction: 11.7 inserted
     * the replacement member above the block and deleted the old member below
     * it, leaving three lines of prose attached to `peer_usb_epoch`. */
    uint8_t  peer_storage_news_observed;
    uint16_t peer_usb_epoch;
    uint16_t peer_host_open_generation;
    uint16_t peer_host_close_generation;
    uint32_t peer_session_rx_count;
    uint32_t peer_session_forget_count;
    uint8_t  peer_session_stale_pending;
    uint32_t local_session_tx_count;
    uint8_t  host_peer_local_matrix_ready;
    uint8_t  host_peer_source_push_forced;
    uint8_t  host_peer_peer_cache_valid;
    uint8_t  host_peer_local_current_seq8;
    uint8_t  host_peer_local_host_known_seq8;
    uint8_t  host_peer_peer_matrix_seq8;
    uint32_t host_peer_source_push_tx_count;
    uint32_t host_peer_source_push_ack_count;
    uint32_t host_peer_ack_status_tx_count;
    uint32_t host_peer_visual_snapshot_rx_count;
    uint32_t host_peer_rgb_state_rx_count;
    uint32_t host_peer_authority_rx_count;
    uint32_t host_peer_input_layer_apply_count;
    uint32_t host_peer_peer_cache_update_count;
    uint32_t host_peer_peer_cache_project_count;
    uint32_t host_peer_peer_cache_flush_count;
    era_split_transport_scheduler_era_common_t host_peer_peer_era;
    uint8_t  host_peer_peer_era_local_current_seq8;
    uint8_t  host_peer_peer_era_local_host_known_seq8;
    uint32_t host_peer_peer_era_source_push_tx_count;
    uint32_t host_peer_peer_era_source_push_ack_count;
#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
    uint32_t host_peer_peer_era_timing_sample_count;
    uint32_t host_peer_peer_era_timing_timeout_count;
    era_split_transaction_timing_diagnostics_t host_peer_peer_era_timing;
    era_split_transaction_timing_bucket_diagnostics_t host_peer_peer_era_timing_buckets[ERA_SPLIT_TRANSACTION_TIMING_BUCKET_COUNT];
#endif
    era_split_transport_scheduler_era_common_t host_peer_host_era;
    uint8_t  host_peer_host_era_peer_cache_valid;
    uint8_t  host_peer_host_era_peer_matrix_seq8;
    uint32_t host_peer_host_era_ack_status_tx_count;
    uint32_t host_peer_host_era_peer_cache_update_count;
    uint32_t host_peer_host_era_peer_cache_project_count;
    uint32_t host_peer_host_era_peer_cache_flush_count;
    uint32_t host_peer_host_era_responder_relation_request_rx_count;
    uint32_t host_peer_host_era_responder_host_peer_heartbeat_rx_count;
    uint32_t host_peer_host_era_responder_host_peer_source_push_rx_count;
    /* DUAL-HOST era block, and it exists because the relation has a runtime:
     * an era block is owed wherever a gate reads counters across a mode
     * window. It carries only what the DUAL-HOST gates read:
     * the era's compact wire IO, and the storage episode counts that must
     * stay frozen across a window with no settled config change. */
    era_split_transport_scheduler_era_common_t dual_host_era;
    uint32_t dual_host_era_storage_open_count;
    uint32_t dual_host_era_storage_close_count;
    uint32_t dual_host_era_storage_transfer_count;
    /* Runtime section traffic, and the pair the steady-state legs read.
     * `tx` counts frames this half sent carrying a runtime *section*, `rx`
     * counts runtime sections it applied. Counting sections rather than polls
     * is what keeps both legs readable under Slice 11.5's constant cadence:
     * an idle window still reads 0/0 while `io` rises at the poll rate, and a
     * typing window with no layer transition also reads 0/0, which is what
     * distinguishes a latest-state section from an event stream. There is
     * deliberately no defer field: nothing here can defer, and a counter that
     * is structurally zero reads as evidence when it is not. */
    uint32_t dual_host_era_runtime_tx_count;
    uint32_t dual_host_era_runtime_rx_count;
} era_split_transport_scheduler_diagnostics_snapshot_t;
