// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_split_transaction_types.h"
#include "era_split_wire_protocol.h"

/* No pressed_baseline_valid flag: the reason-only visual form is retired, and
   the response-direction length table pins this section to
   ..._VISUAL_RESYNC_FULL_BYTES with no second entry, so a present section is
   exactly that wide or era_split_wire_layout_walk() refuses the frame. The
   flag was therefore constant-true in five records and its false arms were
   seven branches no sender could select (retired 2026-08-11). Reopening a
   short form means reopening the length table first, which is where the bound
   is. */
typedef struct {
    uint8_t reason;
    uint8_t pressed_baseline[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES];
} era_host_peer_visual_snapshot_t;

typedef struct {
    bool    enabled;
    bool    sleep;
    uint8_t mode;
    uint8_t hue;
    uint8_t sat;
    uint8_t val;
    uint8_t speed;
    uint8_t flags;
} era_host_peer_rgb_state_t;

/* No `result`/`request_sent` mirrors since D3's sweep. This record carries the
   decoded sections and nothing about the exchange that carried them; the lane's
   own outcome lives in the initiator result that wraps it, which is where every
   reader took it from. */
typedef struct {
    bool                                  host_source_lock_state_valid;
    uint8_t                               host_source_lock_state;
    bool                                  host_source_visual_snapshot_valid;
    era_host_peer_visual_snapshot_t       host_source_visual_snapshot;
    bool                                  host_source_rgb_state_valid;
    era_host_peer_rgb_state_t             host_source_rgb_state;
    bool                                  host_source_storage_news_valid;
    uint8_t                               host_source_storage_news;
    bool                                  host_source_time_anchor_valid;
    uint32_t                              host_source_time_anchor_ms;
    /* A wire byte, not a layer_state_t, so this header stays reachable from
       core1 without action_layer.h. The two are the same width and
       era_split_peer_layer.c asserts it. */
    bool                                  host_source_input_layer_valid;
    uint8_t                               host_source_input_layer;
    /* The responder's session facts on the relation's own lane (Slice 11.6).
       It is the one section here that carries no payload the receiving half
       renders or resolves -- it re-decides the relation itself. */
    bool                                  host_source_authority_valid;
    era_split_wire_authority_section_t    host_source_authority;
    /* The responder's tap-hold activity (FA-2 S2), consumed by the tap
       activity cache on the receiving half's core0. */
    bool                                  host_source_activity_valid;
    era_split_wire_activity_section_t     host_source_activity;
} era_host_peer_transaction_result_t;

/* The apply summary retired with the lane-result apply it reported (D3). It
   existed so the route drain could count what a second arrival path had
   applied; there is one arrival path now, and the standing drain counts at its
   own apply sites. */

typedef struct {
    era_split_transaction_engine_result_t result;
    bool                                  ack_status_sent;
    bool                                  lock_state_sent;
    bool                                  visual_snapshot_sent;
    bool                                  rgb_state_sent;
    /* D1: the storage mask joined the sections that confirm their own send. It
       is filled from the section byte the wire carried, like every field here,
       so a mask the eligibility clip or the frame budget dropped never advances
       its shadow. */
    bool                                  storage_news_sent;
    bool                                  time_anchor_sent;
    bool                                  input_layer_sent;
    bool                                  authority_sent;
    bool                                  activity_sent;
} era_host_peer_transaction_responder_response_t;

typedef struct {
    bool                            send_lock_state;
    uint8_t                         lock_state_bits;
    bool                            send_visual_snapshot;
    era_host_peer_visual_snapshot_t visual_snapshot;
    bool                            send_rgb_state;
    era_host_peer_rgb_state_t       rgb_state;
    bool                            send_storage_news;
    uint8_t                         storage_news;
    bool                            send_time_anchor;
    uint32_t                        time_anchor_ms;
    /* The send-side stamp (R6): `timer_hw->timerawl` at the instant
       `time_anchor_ms` was read. Core0 captures both together and core1 adds
       the elapsed at encode, so the value that leaves the wire is the one the
       anchor would carry at send time -- the mirror of the receive-side
       correction R2 built, closing the publish-to-send gap the responder
       snapshot's poll-serving latency opens (measured worst step 9 ms against
       the then 20 ms period, before this stamp existed; the gap is bounded by
       whatever the period is, so it is 10 ms since 2026-08-09). Both cores
       read that counter
       and only differences of it are used. */
    uint32_t                        time_anchor_stamp_us;
    bool                            send_input_layer;
    uint8_t                         input_layer;
    bool                            send_authority;
    era_split_wire_authority_section_t authority;
    bool                            send_activity;
    era_split_wire_activity_section_t activity;
} era_host_peer_transaction_responder_response_plan_t;

/* One walk fills every section's validity and value. It replaced five
   per-section extractors that each re-derived their offsets from the mask;
   the initiator decode runs on core1, so that is four walks and four stack
   frames removed from the path whose depth this board cannot check at compile
   time. */
bool era_host_peer_transaction_extract_sections(const era_split_wire_frame_t *response, era_host_peer_transaction_result_t *result);
bool era_host_peer_transaction_encode_rgb_state_body(const era_host_peer_rgb_state_t *state, uint8_t payload[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES]);
/* One body, one decoder, both id spaces (Slice 12): the response extract and
   the push accept read the same seven bytes through this. Returns false on a
   reserved-bit violation, which the layout walks already exclude. */
bool era_host_peer_transaction_decode_rgb_state_body(const uint8_t *payload, era_host_peer_rgb_state_t *state);
void era_host_peer_transaction_apply_lock_state(uint8_t lock_state);
void era_host_peer_transaction_apply_visual_snapshot(const era_host_peer_visual_snapshot_t *snapshot);
void era_host_peer_transaction_invalidate_peer_visual_baseline(void);
/* `consume_sleep` is the skip-at-apply half of the Slice 12 sleep rule: the
   sleep bit is a fact about the one USB session a HOST-PEER pair shares, and
   DUAL-HOST halves each own one, so a DUAL-HOST apply consumes configuration
   only and passes false. HOST-PEER's apply is untouched and passes true --
   the receiving PEER has no USB session of its own, so the HOST's render gate
   is the only sleep fact the pair has. A parameter rather than a caller-side
   skip for the same reason the anchor's rx instant is one (R2.1): the
   signature is what stops a second carrier applying the bit anyway. */
/* Returns whether the apply moved the render config or the sleep fact, which
   is what `app=rgb` counts. */
bool era_host_peer_transaction_apply_rgb_state(const era_host_peer_rgb_state_t *state, bool consume_sleep);
/* `rx_us` is core1's `timer_hw->timerawl` reading for the exchange that carried
   the anchor, and it is a parameter rather than a caller-side adjustment
   because that is what stops this defect recurring (R2.1). The wire carries a
   timestamp; `sync_timer_update()` sets the shared clock absolutely; so an
   anchor applied any later than it arrived puts this half's clock behind by
   exactly that lateness. R2 corrected one of the two carriers by adding the
   held time at its call site and left the other applying raw, because the
   signature allowed it. It no longer does: every carrier must produce its own
   receive instant, and the one correction lives here with the anchor watch that
   measures it. */
void era_host_peer_transaction_apply_time_anchor(uint32_t anchor_ms, uint32_t rx_us);
/* True after this half has applied at least one relation time-anchor. The
   link raise's initiator asks this before it arms, so T_commit is in the
   shared-clock domain (`era_split_link.h`'s **Reconciliation**). The
   responder is the clock source and does not apply. */
bool era_host_peer_transaction_time_anchor_adopted(void);
#if defined(SPLIT_KEYBOARD) && defined(ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE)
/* Anchor applies, backwards steps, and the worst backwards step in ms. Read
   after a PEER era, because the half that applies an anchor has no console
   while it is applying one. */
void era_host_peer_transaction_get_time_anchor_diagnostics(uint32_t *apply_count, uint32_t *back_count, uint32_t *back_max_ms);
/* R6 criterion item 3: the last signed per-refresh correction and the local
   inter-refresh interval, whose quotient is the drift rate. */
void era_host_peer_transaction_get_time_anchor_refresh_diagnostics(int32_t *last_correction_ms, uint32_t *last_interval_ms);
#endif
/* The ACTIVITY apply (FA-2 S2): into the tap activity unit's peer cache, a
   cache update in the invariant's sense -- the tapping engine consumes it on
   its own passes and no HID event is produced here. */
void era_host_peer_transaction_apply_activity(const era_split_wire_activity_section_t *activity);
void era_host_peer_transaction_force_responder_lock_state_response(void);
/* The relation rotation drops every edge-armed send-shadow this unit owns but
   the lock's -- seven of the eight. They are one call because they rotate for
   one reason -- the peer clears what it holds -- and separating them once
   already produced a shadow that survived an event its twin did not. This read
   "both edge-armed shadows" until 2026-08-10, which was true when the call
   dropped INPUT and AUTHORITY; five sections joined it between Slice 12 and D1
   and the sentence did not follow. The accretion and each section's reason are
   at the definition. */
/* The pressed-baseline byte copy, `static inline` here because both halves of
   the visual section need it -- era_host_peer_responder.c stages and commits
   the baseline, era_host_peer_response.c encodes and applies it -- and it had
   been written out twice, byte-identical, once in each. Two copies of one
   fixed-width copy is the shape where a widened baseline updates one of them.
   The same reasoning already put the responder rings' wrap rule in
   era_split_communication_core_responder_internal.h. */
static inline void era_host_peer_transaction_visual_baseline_copy(uint8_t dst[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES], const uint8_t src[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES]) {
    for (uint8_t index = 0; index < ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES; index++) {
        dst[index] = src[index];
    }
}

void era_host_peer_transaction_forget_responder_input_layer(void);
void era_host_peer_transaction_clear_responder_visual_snapshot(void);
bool era_host_peer_transaction_publish_responder_visual_snapshot(const uint8_t baseline[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES]);
void era_host_peer_transaction_clear_responder_rgb_state(void);
bool era_host_peer_transaction_publish_responder_rgb_state(const era_host_peer_rgb_state_t *state);
/* `eligible_sections` is this relation's response-direction eligibility, and
   it clips the plan rather than only the publish. A section that is due until
   the wire confirms it, and that this relation cannot send, would otherwise
   stay due forever -- which for the time anchor means re-capturing a fresh
   sync_timer_read32() on every call and making the responder snapshot differ
   from the last one every time. */
void era_host_peer_transaction_prepare_responder_response(uint8_t lock_state_bits, uint8_t storage_news, uint8_t input_layer, const era_split_wire_authority_section_t *authority, const era_split_wire_activity_section_t *activity, uint8_t eligible_sections, era_host_peer_transaction_responder_response_plan_t *plan);
void era_host_peer_transaction_commit_responder_response(const era_host_peer_transaction_responder_response_plan_t *plan, const era_host_peer_transaction_responder_response_t *response);
