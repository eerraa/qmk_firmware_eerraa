// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_host_peer_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "action_layer.h"
#include "dynamic_keymap.h"
#include "eeconfig.h"
#include "eeprom.h"
#include "hal.h"
#include "hardware/structs/timer.h"
#include "keycode_config.h"
#include "nvm_eeprom_eeconfig_internal.h"
#include "nvm_eeprom_via_internal.h"
#include "platforms/eeprom.h"
#include "rgb_matrix.h"
#include "timer.h"
#include "via.h"
#include "../storage/era_eeprom_config_io.h"
#include "../storage/era_eeprom_layout.h"
#include "era_split_sync_storage.h"
#include "communication_core/era_split_communication_core_storage.h"
#include "communication_core/era_split_communication_core_lifecycle.h"
#include "communication_core/era_split_communication_core_owner.h"
#include "era_split_link.h"
#include "era_split_mode_planner.h"
#include "era_split_transport_scheduler.h"
#include "era_split_wire_frame.h"

#ifndef ERA_SRAM_RESIDENT_IMAGE
#    error ERA HOST-PEER storage v1 requires the SRAM-resident load image: the live-wire durable apply keeps Core1 running through flash commits
#endif

/* The VIA and RGB Matrix requirements were a second #error here until
   2026-08-11 and are now $(error)s in era_split_qmk_rules.mk, beside the
   selector that adds this unit: make can see a QMK feature switch since the
   option layer moved to the post_rules.mk phase, and a refusal there fires once
   before any compile instead of after this translation unit has been reached.
   The residency requirement above stays, because ERA_SRAM_RESIDENT_IMAGE is a
   define rather than a feature switch and a define is best checked where
   defines are. */

/* The arm that took DYNAMIC_KEYMAP_EEPROM_ADDR selected on a board config.h
   that defines it only when VIA is off, and no build of this unit has VIA off
   -- the make refusal named above admits none. */
#define ERA_HOST_PEER_STORAGE_DYNAMIC_KEYMAP_ADDR VIA_EEPROM_CONFIG_END

#define ERA_HOST_PEER_STORAGE_DYNAMIC_KEYMAP_BYTES (DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2U)
#define ERA_HOST_PEER_STORAGE_DYNAMIC_ENCODER_ADDR (ERA_HOST_PEER_STORAGE_DYNAMIC_KEYMAP_ADDR + ERA_HOST_PEER_STORAGE_DYNAMIC_KEYMAP_BYTES)

/* No ERA board declares an encoder, so the encoder-map arm reserved nothing. */
#define ERA_HOST_PEER_STORAGE_DYNAMIC_MACRO_ADDR ERA_HOST_PEER_STORAGE_DYNAMIC_ENCODER_ADDR

typedef struct {
    uint32_t address;
    uint16_t size;
    uint8_t  schema;
} era_host_peer_storage_domain_descriptor_t;

/* Local-EEPROM truth: what this half knows about its own store — manifests'
 * companions (dirty deadlines, capture/revision counters) and the pinned
 * image publication state. Survives relation rotation untouched. The split
 * along this line is the Slice 10 family model's foundation; it changes
 * layout only, never reset timing — every reset below still clears exactly
 * the fields it cleared before. */
typedef struct {
    uint32_t dirty_deadline_ms[ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT];
    uint32_t dirty_deadline_valid_mask;
    uint32_t dirty_domain_mask;
    uint32_t source_revision_counter;
    volatile uint32_t image_publication_seq;
    uint32_t image_source_revision;
    uint32_t image_crc32;
    uint32_t next_dirty_deadline_ms;
    uint16_t image_size;
    uint8_t  image_domain;
    uint8_t  initialized;
    uint8_t  image_valid;
    uint8_t  image_stale;
    uint8_t  next_dirty_deadline_valid;
    uint8_t  revision_wrap_pending;
    /* RAM shadow of "current CRC differs from own persisted baseline", one
     * bit per domain (2026-08-14 indicator redesign). It is written at
     * exactly the three sites that already read the baseline record —
     * settled capture, boot capture, convergence close — plus the terminal
     * refusal, which retires a claim the pair can no longer act on. Display
     * truth for the indicator predicate only: the arbitration keeps reading
     * the persisted record through the recency snapshot, so a wrong bit
     * here can mis-light a lamp and can never mis-direct a transfer. It
     * lives in local truth because it is a fact about this half's own store
     * against its own baselines — the term that carries the lamp across a
     * relation identity rotation while the relation-scoped queues drop and
     * re-derive. */
    uint8_t  changed_domain_mask;
} era_host_peer_storage_local_state_t;

/* Relation-scoped episode bookkeeping: probe scheduling, hint consumption,
 * backoff pacing, and the deferred slot — the state whose meaning is bound
 * to the confirmed relation rather than to this half's own EEPROM. The
 * delta-full-fetch repair mark sits here because it steers the transfer
 * lane, not the local store. */
typedef struct {
    uint32_t idle_due_deadline_ms;
    uint8_t  idle_due_domain;
    uint8_t  deferred_probe_domain;
    uint8_t  idle_due;
    uint8_t  active_due;
    uint8_t  runtime_service_active;
    /* This half's storage news value (D2): a forward-only 7-bit counter, one
     * step per settled capture, `0` reserved for "nothing to claim". It
     * replaced a seven-bit per-domain mask, and the replacement is what makes
     * the whole hint lane expressible.
     *
     * **A level has to be able to fall, and this one could not.** The mask's
     * clear was a *derived* fact — a bit came down when its domain converged —
     * so it needed a wire form for the fall, a receiver that could represent
     * absence, and a guard for every case where a domain's content moved again
     * before its bit could. The one case no discipline reached was the domain
     * proven while this half already held newer content: the bit stayed raised,
     * the value never moved, and the peer heard nothing more.
     *
     * A counter has no fall to express. Every settled capture is one step, any
     * step is news, and news means "ask" rather than "pull domain N" — so the
     * peer's summary re-derives every domain from both halves' current facts
     * and the value's own history stops mattering. The domain identity the mask
     * carried was never load-bearing: the `SYNC_STATUS` summary already carries
     * it per domain, derived from the durable baseline, and carries direction
     * with it. */
    uint8_t  settled_news_value;
    uint8_t  probe_pending_mask;
    /* The peer's advertised storage-news value as last taken, and the whole of
     * what this half remembers about the peer's hint (D2). It answers one
     * question -- is this the same claim I already acted on -- and the answer
     * arms a summary or nothing.
     *
     * **Three fields and a round-end check stood beside it and are gone.** An
     * in-hand set, a per-advertisement re-arm budget and a round-end re-read
     * existed because the hint named domains and armed probes *directly*, which
     * made every one of its imperfections a scheduling bug someone had to
     * compensate for: an echo re-arming a running episode, a claim the peer
     * could not lower, a domain proven while the peer held newer content. None
     * of them survives a hint that only says "ask", because the asking is a
     * summary exchange that re-derives every domain's direction from both
     * halves' current facts. */
    uint8_t  peer_news_value;
    uint8_t  probe_backoff_domain;
    uint8_t  probe_backoff_streak;
    uint8_t  delta_full_fetch_domain;
    /* Slice 10 arbitration bookkeeping. The pending masks are the four
     * cells' work queues: probe covers both the match-verify and pull
     * cells (the proof result distinguishes them), push is the
     * initiator-newer cell, and conflict holds domains whose counter
     * exchange must run before a direction exists. A domain pending in
     * both probe and push collides into conflict at grant time. */
    uint8_t  push_pending_mask;
    uint8_t  conflict_pending_mask;
    uint8_t  peer_changed_mask;
    uint8_t  arbitration_flags;
    uint8_t  idle_due_kind;
    /* The indicator's two cold-produced facts (2026-08-14 redesign), one
     * byte: bit0 is the peer's advertised storage-pending mirror (the
     * STORAGE_PENDING push section's last applied value), bit1 the cached
     * serviceability-and-policy gate. The mirror deliberately survives the
     * relation identity rotation — the fact it mirrors does, and the live
     * section re-crosses on the fresh relation — and clears only when the
     * relation leaves service, so a standalone half never wears a stale
     * lamp. */
    uint8_t  indicator_bits;
} era_host_peer_storage_relation_state_t;

enum {
    ERA_HOST_PEER_STORAGE_INDICATOR_PEER_PENDING = 1U << 0,
    ERA_HOST_PEER_STORAGE_INDICATOR_GATE         = 1U << 1,
};

enum {
    ERA_HOST_PEER_STORAGE_ARB_FLAG_SUMMARY_DONE    = 1U << 0,
    ERA_HOST_PEER_STORAGE_ARB_FLAG_SUMMARY_PENDING = 1U << 1,
    /* The *round* is the mandatory relation-open one, whose job is to prove
     * every domain. It is round-scoped rather than token-scoped: a summary
     * consumes SUMMARY_PENDING but leaves this set, so an episode that
     * aborts mid-sweep and returns through a fresh summary is still proven.
     * Cleared when the round drains. An in-session refresh never sets it. */
    ERA_HOST_PEER_STORAGE_ARB_FLAG_ROUND_VERIFY_ALL = 1U << 2,
    /* Bits 3, 4 and 5 are retired and left unreused, so an `arb` value in an
     * older capture keeps its meaning. A live field can therefore only read
     * `0..7`, and a capture showing `8` or above is a pre-D2 image.
     *
     * Bits 3 and 5 went at Slice 11.7. They were the responder-changed hint's
     * rising-edge latch and the round-end re-read's stop condition, and both
     * existed only because DUAL-HOST's carrier was a single advisory bit: a
     * second domain changing mid-round raised no new edge, so the level had to
     * be re-read at round end, and re-reading a level that a lying peer never
     * lowers had to be bounded by whether the round's summary found work.
     *
     * Both were compensations for a carrier that could not express its own
     * state, and D2 removed the carrier rather than the compensations: the
     * hint is one forward-only value in both relations, any change is news,
     * and news arms a summary that re-derives every domain. Nothing is left
     * for a latch or a stop condition to bound.
     *
     * Bit 4 went at D2 with that same carrier. It was the edge detector for
     * this half's own EEPROM sync policy, so the enable could re-arm the hint
     * the policy gate had been suppressing; settled captures step the news
     * value whether the policy is open or closed, so the enable edge is news by
     * construction and needs no mechanism to notice it. The full account sits
     * at the retirement site in `era_host_peer_storage_peer_task`. Unlike 3 and
     * 5 it kept its enumerator for two more slices with nothing setting or
     * testing it, which is the one shape a retired bit must not take -- a name
     * in this enum reads as a live flag, and only a comment reads as history. */
    /* The two that describe a round this half still owes, as opposed to a
       record of one it ran. A half with no initiator role releases exactly
       these (Slice 11.7) and keeps the rest: the record bits say what the
       previous era did. Bit 4 was excluded on a third reason -- it was the
       responder's own, not a record of the previous era -- and since D2 there
       is nothing left for it to release. */
    ERA_HOST_PEER_STORAGE_ARB_ROUND_OWED_FLAGS = ERA_HOST_PEER_STORAGE_ARB_FLAG_SUMMARY_PENDING |
                                                 ERA_HOST_PEER_STORAGE_ARB_FLAG_ROUND_VERIFY_ALL,
};

enum {
    ERA_HOST_PEER_STORAGE_TOKEN_PROBE = 0,
    ERA_HOST_PEER_STORAGE_TOKEN_PUSH,
    ERA_HOST_PEER_STORAGE_TOKEN_CONFLICT,
    ERA_HOST_PEER_STORAGE_TOKEN_SUMMARY,
};

#define ERA_HOST_PEER_STORAGE_ALL_DOMAINS_MASK ((uint8_t)((1U << ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) - 1U))

_Static_assert(ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT == 7U,
               "ERA storage domain masks require exactly seven domains.");

/* The news value's range, and it is the wire section's range rather than a
 * choice made here: bit 7 of the same body byte carries the responder's
 * storage-pending flag (`..._STORAGE_NEWS_FLAG_PENDING`), so the value lives
 * in `1..127` with `0` reserved. Wrap is harmless because the consumer tests
 * inequality and never ordering, and it is unreachable in practice — the value
 * crosses on the poll following each step, so aliasing would need 127 settled
 * captures inside one delivery gap. */
#define ERA_HOST_PEER_STORAGE_NEWS_VALUE_MAX ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_VALUE_MASK

typedef enum {
    ERA_HOST_PEER_STORAGE_RUNTIME_IDLE = 0,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PROBE,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_TRANSFER,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_APPLY,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_REVALIDATE,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_COMPLETE,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_ABORT,
    ERA_HOST_PEER_STORAGE_RUNTIME_HOST_READY,
    ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PINNED,
    ERA_HOST_PEER_STORAGE_RUNTIME_HOST_CLOSED,
    /* Appended after the HOST states so existing diagnostic state values
     * stay stable across the 8.3 sliced-apply change. */
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_APPLY_WRITE,
    /* Slice 10 push lane, appended in order for the same reason. The
     * responder (11..15) pins as the apply target and walks open ->
     * staging -> apply-wait -> sliced write -> durable-declared; the
     * initiator (16..19) mirrors its pull states for the opposite
     * direction; 20..21 are the arbitration exchanges (whole-family
     * summary and the per-conflict counter form). */
    ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_OPEN,
    ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_STAGING,
    ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WAIT,
    ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WRITE,
    ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_DURABLE,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_OPEN,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_CHUNKS,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_APPLY,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_COMPLETE,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_SYNC_STATUS,
    ERA_HOST_PEER_STORAGE_RUNTIME_PEER_CONFLICT_STATUS,
} era_host_peer_storage_runtime_state_t;

static bool era_host_peer_storage_state_is_host_push(uint8_t state) {
    return state >= (uint8_t)ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_OPEN &&
           state <= (uint8_t)ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_DURABLE;
}

static void era_host_peer_storage_host_close(uint32_t now_ms, bool aborted, bool revalidate);
static bool era_host_peer_storage_publish_current_image(era_split_eeprom_sync_domain_t domain, uint32_t image_crc32);
static void era_host_peer_storage_peer_begin_abort(uint8_t status, uint32_t now_ms);

typedef enum {
    ERA_HOST_PEER_STORAGE_ROLE_NONE = 0,
    ERA_HOST_PEER_STORAGE_ROLE_PEER,
    ERA_HOST_PEER_STORAGE_ROLE_HOST,
} era_host_peer_storage_runtime_role_t;

enum {
    ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING = 1U << 0,
    ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE = 1U << 1,
    ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_APPLY_WRITE      = 1U << 2,
    ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY   = 1U << 3,
    ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TARGET_DIRTY     = 1U << 4,
    /* Data is moving: raised only where a validated TRANSFER starts one,
     * and never for the wire priority an abort takes. ROUTE_EXCLUSIVE is a
     * scheduler fact — who owns the wire — and the two coincided only while
     * a transfer was the sole reason to raise it. The storage indicator
     * reads this one, so an episode that moves nothing shows nothing. */
    ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TRANSFER_ACTIVE  = 1U << 5,
    /* The abort reason cannot change without an event that would itself
     * re-trigger, so this episode must close rather than re-arm. */
    ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TERMINAL_ABORT   = 1U << 6,
    /* Latched at episode start from the token's own cell: a push, a
     * conflict exchange, or a probe whose domain the peer declared changed
     * is decided content movement, so the indicator holds through the
     * episode's pre-transfer exchanges — the grant consumed the cell bit,
     * and without this the lamp would blink out for the probe/open
     * round-trip between grant and validated TRANSFER. A verify probe (the
     * relation-open audit's business) never sets it, which is what keeps
     * the audit sweep dark. Cleared with the rest of the flags at every
     * episode reset. */
    ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_CONTENT_EXPECTED = 1U << 7,
};

typedef struct {
    uint32_t episode_deadline_ms;
    uint32_t retry_deadline_ms;
    uint32_t source_revision;
    uint32_t expected_crc32;
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint16_t request_generation;
    uint16_t policy_generation;
    uint16_t transaction_generation;
    uint16_t image_size;
    uint16_t staged_bytes;
    uint16_t responder_snapshot_generation;
    uint16_t peer_usb_epoch;
    uint16_t peer_host_open_generation;
    uint16_t peer_host_close_generation;
    uint16_t apply_offset;
    /* The episode phase `episode_deadline_ms` was armed under (Slice 11.7).
     * The deadline re-arms when the phase advances and never when a request is
     * merely repeated, which is what makes it mean "this episode stopped
     * advancing" rather than "this episode took a while". A phase is the state
     * plus the pinned operation, because the two halves express an advance
     * differently: the initiator walks states, and a responder waiting through
     * the initiator's durable apply stays in `HOST_PINNED` and moves only its
     * pinned operation. */
    uint8_t  deadline_state;
    uint8_t  deadline_operation;
    uint8_t  state;
    uint8_t  role;
    uint8_t  domain;
    uint8_t  next_chunk;
    uint8_t  retry_count;
    uint8_t  pending_operation;
    uint8_t  last_status;
    uint8_t  flags;
    uint8_t  apply_abort_status;
} era_host_peer_storage_runtime_state_data_t;


/* Schema tripwires. The geometry-independent anchors stay pinned to their
 * literal addresses so a QMK layout change fails here instead of moving the
 * portable image silently — they resolve to the same numbers on every ERA
 * board. The geometry pins that used to sit beside them (MATRIX_ROWS == 12,
 * MATRIX_COLS == 9, and the 864/1161 literals downstream) retired on
 * 2026-08-13 when schema 1 became the formula: the keymap image derives from
 * the board's own geometry on both cores, so those pins had stopped guarding
 * anything and started refusing boards (sirind/tomak, 12x11). What binds the
 * formula's one non-geometry factor is the first assert — the schema layer
 * literal core1 reads, against DYNAMIC_KEYMAP_LAYER_COUNT, which only core0
 * can see. The macro-base relation assert at the end is what a future
 * encoder arm reserving bytes between the two domains would fail. */
_Static_assert(DYNAMIC_KEYMAP_LAYER_COUNT == ERA_HOST_PEER_STORAGE_SCHEMA_DYNAMIC_KEYMAP_LAYERS, "ERA storage schema layer literal drifted from DYNAMIC_KEYMAP_LAYER_COUNT.");
_Static_assert(TOTAL_EEPROM_BYTE_COUNT == 24576U, "ERA storage schema v1 logical EEPROM size changed.");
_Static_assert(ERA_EEPROM_CONFIG_ADDR == 37U, "ERA storage schema v1 ERA config base changed.");
_Static_assert((uintptr_t)EECONFIG_DEFAULT_LAYER == 3U, "ERA storage schema v1 default-layer address changed.");
_Static_assert((uintptr_t)EECONFIG_KEYMAP == 4U, "ERA storage schema v1 keymap-config address changed.");
_Static_assert((uintptr_t)EECONFIG_RGB_MATRIX == 23U, "ERA storage schema v1 RGB Matrix address changed.");
_Static_assert(VIA_EEPROM_LAYOUT_OPTIONS_ADDR == 296U, "ERA storage schema v1 VIA layout-options address changed.");
_Static_assert(ERA_HOST_PEER_STORAGE_DYNAMIC_KEYMAP_ADDR == 297U, "ERA storage schema v1 dynamic keymap address changed.");
_Static_assert(ERA_HOST_PEER_STORAGE_DYNAMIC_MACRO_ADDR == ERA_HOST_PEER_STORAGE_DYNAMIC_KEYMAP_ADDR + ERA_HOST_PEER_STORAGE_DOMAIN_DYNAMIC_KEYMAP_BYTES, "ERA storage schema v1: the macro domain no longer sits immediately after the keymap domain.");
/* The seven size asserts below are also the core-boundary binding. Each
 * compares a layout expression only core0 can evaluate against the shared
 * macro core1's codec table reads, so the two tables cannot drift: a layout
 * change fails here, and correcting the macro carries core1 with it. They
 * were bare literals until Slice 10, which made them a second copy of
 * core1's literals rather than a check on them. */
_Static_assert(ERA_HOST_PEER_STORAGE_DYNAMIC_KEYMAP_BYTES == ERA_HOST_PEER_STORAGE_DOMAIN_DYNAMIC_KEYMAP_BYTES, "ERA storage schema v1 dynamic keymap size changed.");
_Static_assert(DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE == ERA_HOST_PEER_STORAGE_DOMAIN_DYNAMIC_MACRO_BYTES, "ERA storage schema v1 dynamic macro size changed.");
_Static_assert(DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE == ERA_HOST_PEER_STORAGE_IMAGE_BYTES, "ERA storage schema v1 macro domain no longer fits the shared image.");
_Static_assert(VIA_EEPROM_LAYOUT_OPTIONS_SIZE == ERA_HOST_PEER_STORAGE_DOMAIN_VIA_LAYOUT_OPTIONS_BYTES, "ERA storage schema v1 VIA layout options size changed.");
_Static_assert(sizeof(rgb_config_t) == ERA_HOST_PEER_STORAGE_DOMAIN_QMK_RGB_MATRIX_BYTES, "ERA storage schema v1 RGB Matrix config size changed.");
_Static_assert(sizeof(keymap_config_t) == ERA_HOST_PEER_STORAGE_DOMAIN_QMK_KEYMAP_CONFIG_BYTES, "ERA storage schema v1 keymap config size changed.");
_Static_assert(sizeof(uint8_t) == ERA_HOST_PEER_STORAGE_DOMAIN_QMK_DEFAULT_LAYER_BYTES, "ERA storage schema v1 default-layer size changed.");
_Static_assert(ERA_EEPROM_SYNCABLE_CONFIG_SIZE == ERA_HOST_PEER_STORAGE_DOMAIN_ERA_CONFIG_BYTES, "ERA storage schema v1 ERA config size changed.");
_Static_assert(ERA_HOST_PEER_STORAGE_DYNAMIC_MACRO_ADDR + ERA_HOST_PEER_STORAGE_IMAGE_BYTES <= TOTAL_EEPROM_BYTE_COUNT,
               "ERA storage schema v1 exceeds logical EEPROM.");
/* 8.2C raised this local budget by one alignment step (128 -> 132) for the
 * two probe-backoff pacing bytes; 8.3 takes one more step (132 -> 136) for
 * the sliced-apply cursor and deferred-abort latch. The contract-level
 * static cap is verified separately at the ELF gate.
 *
 * Equality, not `<=`. The communication-core aggregate budget carries this same
 * quantity as ERA_HOST_PEER_STORAGE_CORE0_STATE_BYTES, and an inequality here
 * cannot detect that the peer term needs to move: 8.3's two added fields grew
 * this sum 132 -> 136 while the aggregate stayed at 132, understating the
 * static total by four bytes for a whole slice. `==` forces whoever adds a
 * field to update the shared macro, and every profile total then recomputes
 * from it. */
_Static_assert(sizeof(era_host_peer_storage_local_state_t) + sizeof(era_host_peer_storage_relation_state_t) +
                       sizeof(era_host_peer_storage_runtime_state_data_t) ==
                   ERA_HOST_PEER_STORAGE_CORE0_STATE_BYTES,
               "ERA HOST-PEER storage core0 state size changed; update ERA_HOST_PEER_STORAGE_CORE0_STATE_BYTES.");

static const era_host_peer_storage_domain_descriptor_t g_era_host_peer_storage_domains[ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT] = {
    [ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG] = {
        .address = ERA_EEPROM_CONFIG_ADDR + ERA_EEPROM_SYNCABLE_CONFIG_OFFSET,
        .size    = ERA_EEPROM_SYNCABLE_CONFIG_SIZE,
        .schema  = ERA_HOST_PEER_STORAGE_SCHEMA_V1,
    },
    [ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP] = {
        .address = ERA_HOST_PEER_STORAGE_DYNAMIC_KEYMAP_ADDR,
        .size    = ERA_HOST_PEER_STORAGE_DYNAMIC_KEYMAP_BYTES,
        .schema  = ERA_HOST_PEER_STORAGE_SCHEMA_V1,
    },
    [ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_MACRO] = {
        .address = ERA_HOST_PEER_STORAGE_DYNAMIC_MACRO_ADDR,
        .size    = DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE,
        .schema  = ERA_HOST_PEER_STORAGE_SCHEMA_V1,
    },
    [ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_RGB_MATRIX] = {
        .address = (uintptr_t)EECONFIG_RGB_MATRIX,
        .size    = sizeof(rgb_config_t),
        .schema  = ERA_HOST_PEER_STORAGE_SCHEMA_V1,
    },
    [ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_KEYMAP_CONFIG] = {
        .address = (uintptr_t)EECONFIG_KEYMAP,
        .size    = sizeof(keymap_config_t),
        .schema  = ERA_HOST_PEER_STORAGE_SCHEMA_V1,
    },
    [ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_DEFAULT_LAYER] = {
        .address = (uintptr_t)EECONFIG_DEFAULT_LAYER,
        .size    = sizeof(uint8_t),
        .schema  = ERA_HOST_PEER_STORAGE_SCHEMA_V1,
    },
    [ERA_SPLIT_EEPROM_SYNC_DOMAIN_VIA_LAYOUT_OPTIONS] = {
        .address = VIA_EEPROM_LAYOUT_OPTIONS_ADDR,
        .size    = VIA_EEPROM_LAYOUT_OPTIONS_SIZE,
        .schema  = ERA_HOST_PEER_STORAGE_SCHEMA_V1,
    },
};

static uint8_t g_era_host_peer_storage_image[ERA_HOST_PEER_STORAGE_IMAGE_BYTES] __attribute__((aligned(4)));
static era_host_peer_storage_manifest_entry_t g_era_host_peer_storage_manifest[ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT];
static era_host_peer_storage_local_state_t g_era_host_peer_storage_local __attribute__((aligned(4)));
static era_host_peer_storage_relation_state_t g_era_host_peer_storage_relation __attribute__((aligned(4)));
static era_host_peer_storage_runtime_state_data_t g_era_host_peer_storage_runtime __attribute__((aligned(4)));
static era_host_peer_storage_diagnostics_t g_era_host_peer_storage_diagnostics __attribute__((aligned(4)));
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
static era_host_peer_storage_cause_timeline_t g_era_host_peer_storage_cause_timeline __attribute__((aligned(4)));
#endif

#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
static bool era_host_peer_storage_cause_event_role_allowed(era_host_peer_storage_cause_event_t event, uint8_t role) {
    switch (event) {
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_CHUNK_RESULT:
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_APPLY_READY:
            return role == ERA_HOST_PEER_STORAGE_ROLE_PEER || role == ERA_HOST_PEER_STORAGE_ROLE_HOST;
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_APPLY_BEGIN:
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_EEPROM_BEGIN:
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_EEPROM_END:
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_CORE1_RESTART:
            /* The apply family follows the applying role, and that is
             * lane-dependent since the push lane: pull applies on the
             * initiator, push on the responder. */
            return role == ERA_HOST_PEER_STORAGE_ROLE_PEER || role == ERA_HOST_PEER_STORAGE_ROLE_HOST;
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_SESSION_SUBMIT:
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_SESSION_RESULT:
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_REVALIDATED:
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_COMPLETE_SUBMIT:
            return role == ERA_HOST_PEER_STORAGE_ROLE_PEER;
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_SESSION_RX:
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_RESPONDER_STALE:
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_SESSION_FORGET:
            return role == ERA_HOST_PEER_STORAGE_ROLE_HOST;
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_COMPLETE_RESULT:
        case ERA_HOST_PEER_STORAGE_CAUSE_EVENT_ABORT:
            return role == ERA_HOST_PEER_STORAGE_ROLE_PEER || role == ERA_HOST_PEER_STORAGE_ROLE_HOST;
        default:
            return false;
    }
}

static bool era_host_peer_storage_cause_event_recorded(era_host_peer_storage_cause_event_t event) {
    for (uint8_t index = 0; index < g_era_host_peer_storage_cause_timeline.event_count; index++) {
        if ((g_era_host_peer_storage_cause_timeline.event[index] >> 4) == (uint8_t)event) {
            return true;
        }
    }
    return false;
}

static void era_host_peer_storage_cause_timeline_begin(uint8_t role, uint8_t domain, uint16_t transaction_generation) {
    memset(&g_era_host_peer_storage_cause_timeline, 0, sizeof(g_era_host_peer_storage_cause_timeline));
    memset(g_era_host_peer_storage_cause_timeline.elapsed_ms, UINT8_MAX, sizeof(g_era_host_peer_storage_cause_timeline.elapsed_ms));
    g_era_host_peer_storage_cause_timeline.anchor_ms              = timer_read32();
    g_era_host_peer_storage_cause_timeline.transaction_generation = transaction_generation;
    g_era_host_peer_storage_cause_timeline.role                   = role;
    g_era_host_peer_storage_cause_timeline.domain                 = domain;
}

void era_host_peer_storage_cause_timeline_note(era_host_peer_storage_cause_event_t event, uint8_t detail) {
    if (g_era_host_peer_storage_cause_timeline.role == ERA_HOST_PEER_STORAGE_ROLE_NONE ||
        g_era_host_peer_storage_runtime.role != g_era_host_peer_storage_cause_timeline.role ||
        g_era_host_peer_storage_runtime.transaction_generation != g_era_host_peer_storage_cause_timeline.transaction_generation ||
        g_era_host_peer_storage_runtime.domain != g_era_host_peer_storage_cause_timeline.domain ||
        !era_host_peer_storage_cause_event_role_allowed(event, g_era_host_peer_storage_cause_timeline.role) ||
        (event != ERA_HOST_PEER_STORAGE_CAUSE_EVENT_CHUNK_RESULT && era_host_peer_storage_cause_event_recorded(event)) ||
        era_host_peer_storage_cause_event_recorded(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_COMPLETE_RESULT) ||
        era_host_peer_storage_cause_event_recorded(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_ABORT)) {
        return;
    }
    /* Sample the chunk stream instead of logging it. It is the only repeating
       event here, and at 66 chunks it fills any ring worth having before the
       apply, the rotation or the abort — which are the events this timeline
       exists for. One in sixteen keeps the stream's timing skeleton at a cost
       of five entries, and the detail carries the sixteenth rather than the
       chunk id, so `1X` reads as chunk `X * 16`. */
    if (event == ERA_HOST_PEER_STORAGE_CAUSE_EVENT_CHUNK_RESULT) {
        if ((detail & 0x0FU) != 0) {
            return;
        }
        detail = (uint8_t)(detail >> 4);
    }
    if (g_era_host_peer_storage_cause_timeline.event_count >= ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_EVENT_CAPACITY) {
        g_era_host_peer_storage_cause_timeline.overflow = 1;
        return;
    }

    uint32_t elapsed_ms = timer_elapsed32(g_era_host_peer_storage_cause_timeline.anchor_ms);
    uint8_t  index      = g_era_host_peer_storage_cause_timeline.event_count++;
    g_era_host_peer_storage_cause_timeline.event[index]      = (uint8_t)(((uint8_t)event << 4) | (detail & 0x0FU));
    g_era_host_peer_storage_cause_timeline.elapsed_ms[index] = elapsed_ms < UINT16_MAX ? (uint16_t)elapsed_ms : (uint16_t)(UINT16_MAX - 1U);
}

void era_host_peer_storage_cause_timeline_note_stale(uint16_t watch_age_ms, uint16_t stale_limit_ms) {
    if (era_host_peer_storage_cause_event_recorded(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_RESPONDER_STALE)) {
        return;
    }
    era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_RESPONDER_STALE, 0);
    if (era_host_peer_storage_cause_event_recorded(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_RESPONDER_STALE)) {
        g_era_host_peer_storage_cause_timeline.stale_watch_age_ms = watch_age_ms;
        g_era_host_peer_storage_cause_timeline.stale_limit_ms     = stale_limit_ms;
    }
}

void era_host_peer_storage_get_cause_timeline_snapshot(era_host_peer_storage_cause_timeline_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }
    __DMB();
    *snapshot = g_era_host_peer_storage_cause_timeline;
    __DMB();
}
#endif

static bool era_host_peer_storage_domain_valid(era_split_eeprom_sync_domain_t domain) {
    return (uint8_t)domain < (uint8_t)ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT;
}

static uint32_t era_host_peer_storage_domain_mask(era_split_eeprom_sync_domain_t domain) {
    return era_host_peer_storage_domain_valid(domain) ? (1UL << (uint8_t)domain) : 0;
}

static void era_host_peer_storage_refresh_next_dirty_deadline(uint32_t now_ms) {
    bool     valid      = false;
    uint32_t next_delay = 0;
    uint32_t next_ms    = 0;
    uint32_t pending    = g_era_host_peer_storage_local.dirty_deadline_valid_mask;

    for (uint8_t domain = 0; domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT; domain++) {
        if ((pending & (1UL << domain)) == 0) {
            continue;
        }
        uint32_t deadline = g_era_host_peer_storage_local.dirty_deadline_ms[domain];
        uint32_t delay    = timer_expired32(now_ms, deadline) ? 0 : deadline - now_ms;
        if (!valid || delay < next_delay) {
            valid      = true;
            next_delay = delay;
            next_ms    = deadline;
        }
    }

    g_era_host_peer_storage_local.next_dirty_deadline_ms    = next_ms;
    g_era_host_peer_storage_local.next_dirty_deadline_valid = valid ? 1 : 0;
}

/* ---- Recency layer (Slice 10) ----------------------------------------
 * Persisted per-domain sync baselines (the last converged content's CRC32,
 * in the protected reserved record at ERA_EEPROM_SYNC_BASELINE_CONFIG_OFFSET)
 * and divergence counters (settled edits since that baseline, in the
 * sync-policy block's counter bytes). Both live in sync-excluded local
 * EEPROM outside every domain range, so a recency write can never re-dirty
 * a domain. There is no RAM shadow: every reader and writer runs at the
 * cold task boundary through the wear-level cache. Baselines move only at
 * convergence closes and counters only at settled captures, so an idle
 * boot writes nothing. Rules are canonical in
 * era_host_peer_storage_contract.md (Recency Layer). */

typedef struct {
    uint32_t baseline_crc32[ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT];
    uint32_t guard;
} era_host_peer_storage_baseline_record_t;

_Static_assert(sizeof(era_host_peer_storage_baseline_record_t) == ERA_EEPROM_SYNC_BASELINE_CONFIG_SIZE,
               "ERA sync baseline record must fill the reserved range exactly.");
_Static_assert(ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_OFFSET +
                       ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT * ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_BYTES ==
                   ERA_EEPROM_LOCAL_POLICY_CONFIG_SIZE,
               "ERA divergence counters must fill the sync-policy counter bytes exactly.");

static uint32_t era_host_peer_storage_baseline_guard(const era_host_peer_storage_baseline_record_t *record) {
    return era_split_wire_crc32((const uint8_t *)record->baseline_crc32, sizeof(record->baseline_crc32)) ^
           ERA_HOST_PEER_STORAGE_BASELINE_GUARD_XOR;
}

static bool era_host_peer_storage_read_baseline_record(era_host_peer_storage_baseline_record_t *record) {
    if (era_eeprom_read_config(record, ERA_EEPROM_SYNC_BASELINE_CONFIG_OFFSET, sizeof(*record)) != sizeof(*record)) {
        return false;
    }
    return record->guard == era_host_peer_storage_baseline_guard(record);
}

static void era_host_peer_storage_write_baseline(era_split_eeprom_sync_domain_t domain, uint32_t crc32) {
    era_host_peer_storage_baseline_record_t record;
    bool valid = era_host_peer_storage_read_baseline_record(&record);
    if (valid && record.baseline_crc32[domain] == crc32) {
        return;
    }
    if (!valid) {
        /* Unknown neighbours stay zero under the fresh guard: they read as
         * changed until their own convergence close, which is the
         * conservative direction. */
        memset(&record, 0, sizeof(record));
    }
    record.baseline_crc32[domain] = crc32;
    record.guard                  = era_host_peer_storage_baseline_guard(&record);
    era_eeprom_update_config(&record, ERA_EEPROM_SYNC_BASELINE_CONFIG_OFFSET, sizeof(record));

}

static uint32_t era_host_peer_storage_counter_config_offset(era_split_eeprom_sync_domain_t domain) {
    return ERA_SPLIT_EEPROM_SYNC_POLICY_CONFIG_OFFSET + ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_OFFSET +
           (uint32_t)domain * ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_BYTES;
}

/* Hand-expanded rather than routed through era_split_wire_get16/put16, and the
   reason is the contract rather than the arithmetic: these two bytes are the
   stored record's, laid out by era_split_sync_storage.h, and the accessors are
   the wire's. The two orders agree today and are free to stop agreeing, so
   sharing one statement would couple a stored layout to a wire decision. The
   peer's copy of this same counter arrives on the wire and does use the
   accessor, at the arbitration read further down. */
static uint16_t era_host_peer_storage_read_divergence_counter(era_split_eeprom_sync_domain_t domain) {
    uint8_t bytes[ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_BYTES];
    if (era_eeprom_read_config(bytes, era_host_peer_storage_counter_config_offset(domain), sizeof(bytes)) != sizeof(bytes)) {
        return 0;
    }
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8));
}

static void era_host_peer_storage_write_divergence_counter(era_split_eeprom_sync_domain_t domain, uint16_t value) {
    uint8_t bytes[ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_BYTES] = {
        (uint8_t)(value & 0xFFU),
        (uint8_t)(value >> 8),
    };
    era_eeprom_update_config(bytes, era_host_peer_storage_counter_config_offset(domain), sizeof(bytes));
}

/* The changed-mask shadow's one writer pair. Set/clear beside the record
 * reads that already happen — never from the lamp path, which only loads
 * the byte. */
static void era_host_peer_storage_note_changed_shadow(era_split_eeprom_sync_domain_t domain, bool changed) {
    uint8_t bit = (uint8_t)era_host_peer_storage_domain_mask(domain);
    if (changed) {
        g_era_host_peer_storage_local.changed_domain_mask |= bit;
    } else {
        g_era_host_peer_storage_local.changed_domain_mask &= (uint8_t)~bit;
    }
}

/* Settled capture after the trailing quiet interval: content returning to
 * the baseline dissolves the divergence (edit-and-revert), anything else is
 * one more settled edit since the last agreement — quiet coalescing makes
 * that the natural change-count unit. An invalid baseline record still
 * counts edits; convergence is what rebuilds the record.
 *
 * Returns whether the settle departed from the agreement — false exactly
 * when the record is valid and the content sits on its own baseline. Both
 * settle signals (the news step and the in-relation summary arm) are
 * conditioned on it, and the invalid-record arm reads as departed, so
 * degradation errs toward signalling. */
static bool era_host_peer_storage_recency_note_settled(era_split_eeprom_sync_domain_t domain, uint32_t crc32) {
    era_host_peer_storage_baseline_record_t record;
    bool     valid   = era_host_peer_storage_read_baseline_record(&record);
    uint16_t counter = era_host_peer_storage_read_divergence_counter(domain);
    if (valid && record.baseline_crc32[domain] == crc32) {
        era_host_peer_storage_note_changed_shadow(domain, false);
        if (counter != 0) {
            era_host_peer_storage_write_divergence_counter(domain, 0);
        }
        return false;
    }
    era_host_peer_storage_note_changed_shadow(domain, true);
    if (counter < ERA_HOST_PEER_STORAGE_DIVERGENCE_COUNTER_MAX) {
        era_host_peer_storage_write_divergence_counter(domain, (uint16_t)(counter + 1U));
    }
    return true;
}

/* Convergence close (MATCH re-proof or durable COMPLETE, either role): the
 * one place a domain's durable agreement is recorded. **It used to be the one
 * place a domain stopped being news as well, and since D2 nothing stops being
 * news anywhere** - the body carries that account.
 *
 * Two facts retired together here because they answered the same question from
 * opposite ends - "do I have something to tell the peer" and "what did we
 * last agree on" - and keeping them in one function is what made the rule
 * role-independent. It had been two role-specific rules, the responder's
 * close and the HOST role's push-durable path, and an initiator converging
 * from the PEER role matched neither: every DUAL-HOST push and every pull
 * the cable-side PEER closed left its bit set with nothing behind it. Only the
 * second fact is left, and it is still written from one role-independent site
 * for the same reason.
 *
 * The settled-dirty guard that stood on the first fact was the HOST-side
 * clear's, restated in role-independent terms. Retire nothing a newer local
 * write has already dirtied again, and nothing whose current captured content
 * is no longer the content that converged. That second term replaced a
 * pinned-source-revision comparison which named a different field on each role
 * - an initiator's pull episode pins the *responder's* revision - and was
 * equivalent in every reachable state, because `era_host_peer_storage_task`
 * runs no settled capture while `active_due` is raised, so the manifest cannot
 * move under an open episode. A retirement that races a local write is a
 * data-loss defect, which is why the check lived in the rule and not at each
 * caller - and why the guard is gone rather than relaxed, a carrier that never
 * falls having nothing for it to protect.
 *
 * The baseline write needs no such guard because it is self-suppressing: a
 * MATCH re-proof of an unchanged agreement finds its own CRC already stored
 * and writes nothing. */
static void era_host_peer_storage_note_domain_converged(era_split_eeprom_sync_domain_t domain, uint32_t crc32) {
    if (!era_host_peer_storage_domain_valid(domain)) {
        return;
    }
    /* **No hint retirement here since D2**, and the two guards that stood on it
     * went with it: the domain-not-dirty test and the content-still-converged
     * test existed so a bit could not be cleared out from under a newer local
     * write, which is a data-loss defect only a *falling* carrier can have. A
     * forward-only news value has nothing to retire, so the close writes the
     * baseline and stops. */
    era_host_peer_storage_write_baseline(domain, crc32);
    era_host_peer_storage_note_changed_shadow(domain, false);
    if (era_host_peer_storage_read_divergence_counter(domain) != 0) {
        era_host_peer_storage_write_divergence_counter(domain, 0);
    }
}

/* Boot capture: no edit happened here, so the increment arm must not run —
 * a reboot loop would count phantom edits. Two idempotent repairs only:
 * content equal to a valid baseline clears a stale counter, and a
 * divergence whose settling was lost to a power cut inside the quiet
 * interval (changed content, counter zero) is bumped to one so the
 * conflict cell still sees that work. An invalid record has no baseline to
 * diverge from and writes nothing. */
static void era_host_peer_storage_recency_note_boot(const era_host_peer_storage_baseline_record_t *record, bool record_valid,
                                                    era_split_eeprom_sync_domain_t domain, uint32_t crc32) {
    if (!record_valid) {
        /* No baseline to diverge from; the changed shadow was seeded
         * all-set by init's invalid-record arm, mirroring the recency
         * snapshot's conservative degradation. */
        return;
    }
    era_host_peer_storage_note_changed_shadow(domain, record->baseline_crc32[domain] != crc32);
    uint16_t counter = era_host_peer_storage_read_divergence_counter(domain);
    if (record->baseline_crc32[domain] == crc32) {
        if (counter != 0) {
            era_host_peer_storage_write_divergence_counter(domain, 0);
        }
    } else if (counter == 0) {
        era_host_peer_storage_write_divergence_counter(domain, 1);
    }
}

/* One range, because the 2026-08-18 regrouping put the syncable half's whole
 * reserve at its end. It was two loops over two scattered holes, which is what
 * a reserve looks like when it is whatever the last claim left behind rather
 * than the room the next claim takes from. The index is domain-relative and
 * equals the config-block offset because ERA_EEPROM_SYNCABLE_CONFIG_OFFSET is
 * zero -- the assert below is what keeps that true rather than assumed, since
 * a nonzero syncable offset would make every macro here read the wrong byte. */
_Static_assert(ERA_EEPROM_SYNCABLE_CONFIG_OFFSET == 0, "The ERA_CONFIG domain image is indexed by config-block offset; a nonzero syncable offset breaks the reserved-zero walk.");
_Static_assert(ERA_EEPROM_SYNCABLE_RESERVED_OFFSET + ERA_EEPROM_SYNCABLE_RESERVED_SIZE <= ERA_EEPROM_SYNCABLE_CONFIG_SIZE, "The ERA syncable reserve must lie inside the ERA_CONFIG domain image.");

static bool era_host_peer_storage_reserved_era_config_is_zero(void) {
    for (uint16_t i = ERA_EEPROM_SYNCABLE_RESERVED_OFFSET;
         i < ERA_EEPROM_SYNCABLE_RESERVED_OFFSET + ERA_EEPROM_SYNCABLE_RESERVED_SIZE;
         i++) {
        if (g_era_host_peer_storage_image[i] != 0) {
            return false;
        }
    }
    return true;
}

static uint32_t era_host_peer_storage_next_revision(void) {
    uint32_t next = g_era_host_peer_storage_local.source_revision_counter + 1U;
    if (next == 0) {
        g_era_host_peer_storage_local.revision_wrap_pending = 1;
        return 0;
    }
    g_era_host_peer_storage_local.source_revision_counter = next;
    return next;
}

static void era_host_peer_storage_invalidate_image_publication(void) {
    uint32_t current = g_era_host_peer_storage_local.image_publication_seq;
    if (current >= UINT32_MAX - 2U) {
        g_era_host_peer_storage_local.revision_wrap_pending = 1;
        g_era_host_peer_storage_local.image_valid           = 0;
        return;
    }
    uint32_t odd_seq = (current & 1U) != 0 ? current : current + 1U;
    g_era_host_peer_storage_local.image_publication_seq = odd_seq;
    __DMB();
    g_era_host_peer_storage_local.image_valid = 0;
    __DMB();
    g_era_host_peer_storage_local.image_publication_seq = odd_seq + 1U;
    __DMB();
}

/* The image seqlock's writer half, in three steps because the two publishers
   below do different work between the open and the close: one reads the domain
   out of EEPROM inside the window, the other already holds the content and only
   restates it. What must not differ is the ordering, and a second copy of that
   is not a defect a build or a test can see -- it is a reader tear on the other
   core, at a rate nobody reproduces.
   `era_host_peer_storage_invalidate_image_publication()` above deliberately
   does NOT use these: it reuses an already-odd sequence where a publisher skips
   past it, because it is closing a window rather than opening one. */
static uint32_t era_host_peer_storage_image_publish_open(void) {
    uint32_t odd_seq = g_era_host_peer_storage_local.image_publication_seq + 1U;
    if ((odd_seq & 1U) == 0) {
        odd_seq++;
    }
    g_era_host_peer_storage_local.image_publication_seq = odd_seq;
    g_era_host_peer_storage_local.image_valid           = 0;
    __DMB();
    return odd_seq;
}

/* The published record itself: the manifest entry the peer reads and the local
   image facts core1 reads beside it. `image_valid` is the revision test rather
   than a flag the caller passes -- the capture path publishes an invalid image
   with revision 0 on an integrity reject, and the republish path cannot reach
   here with revision 0 at all, so one expression covers both. */
static void era_host_peer_storage_image_publish_record(era_split_eeprom_sync_domain_t domain, const era_host_peer_storage_domain_descriptor_t *descriptor, uint32_t image_crc32, uint32_t source_revision) {
    era_host_peer_storage_manifest_entry_t *manifest = &g_era_host_peer_storage_manifest[domain];
    manifest->domain          = (uint8_t)domain;
    manifest->schema          = descriptor->schema;
    manifest->image_size      = descriptor->size;
    manifest->image_crc32     = image_crc32;
    manifest->source_revision = source_revision;

    g_era_host_peer_storage_local.image_domain          = (uint8_t)domain;
    g_era_host_peer_storage_local.image_size            = descriptor->size;
    g_era_host_peer_storage_local.image_crc32           = image_crc32;
    g_era_host_peer_storage_local.image_source_revision = source_revision;
    g_era_host_peer_storage_local.image_stale           = 0;
    g_era_host_peer_storage_local.image_valid           = source_revision != 0;
}

static void era_host_peer_storage_image_publish_close(uint32_t odd_seq) {
    __DMB();
    g_era_host_peer_storage_local.image_publication_seq = odd_seq + 1U;
    __DMB();
}

static bool era_host_peer_storage_capture_domain(era_split_eeprom_sync_domain_t domain) {
    if (!era_host_peer_storage_domain_valid(domain) || g_era_host_peer_storage_relation.active_due) {
        return false;
    }

    if (g_era_host_peer_storage_local.image_publication_seq >= UINT32_MAX - 1U) {
        g_era_host_peer_storage_local.revision_wrap_pending = 1;
        g_era_host_peer_storage_local.image_valid           = 0;
        return false;
    }

    const era_host_peer_storage_domain_descriptor_t *descriptor = &g_era_host_peer_storage_domains[domain];
    era_host_peer_storage_manifest_entry_t *manifest = &g_era_host_peer_storage_manifest[domain];
    uint32_t dirty_generation = manifest->dirty_generation;

    uint32_t odd_seq = era_host_peer_storage_image_publish_open();

    eeprom_read_block(g_era_host_peer_storage_image, (const void *)(uintptr_t)descriptor->address, descriptor->size);
    bool image_valid = domain != ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG || era_host_peer_storage_reserved_era_config_is_zero();
    uint32_t source_revision = image_valid ? era_host_peer_storage_next_revision() : 0;
    uint32_t image_crc32 = era_split_wire_crc32(g_era_host_peer_storage_image, descriptor->size);
    if (!image_valid) {
        g_era_host_peer_storage_diagnostics.integrity_reject_count++;
    }

    era_host_peer_storage_image_publish_record(domain, descriptor, image_crc32, source_revision);
    era_host_peer_storage_image_publish_close(odd_seq);
    if (g_era_host_peer_storage_runtime.role == ERA_HOST_PEER_STORAGE_ROLE_HOST) {
        g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
    }

    uint32_t domain_mask = era_host_peer_storage_domain_mask(domain);
    if (manifest->dirty_generation == dirty_generation) {
        g_era_host_peer_storage_local.dirty_domain_mask &= ~domain_mask;
        g_era_host_peer_storage_local.dirty_deadline_valid_mask &= ~domain_mask;
    }
    return source_revision != 0;
}

static bool era_host_peer_storage_range_overlaps(uint32_t offset, uint32_t length, const era_host_peer_storage_domain_descriptor_t *descriptor) {
    if (length == 0 || descriptor == NULL) {
        return false;
    }
    uint32_t end = offset + length;
    uint32_t domain_end = descriptor->address + descriptor->size;
    return offset < domain_end && descriptor->address < end;
}

void era_host_peer_storage_init(void) {
    if (g_era_host_peer_storage_local.initialized) {
        return;
    }

    memset(&g_era_host_peer_storage_local, 0, sizeof(g_era_host_peer_storage_local));
    memset(&g_era_host_peer_storage_relation, 0, sizeof(g_era_host_peer_storage_relation));
    memset(&g_era_host_peer_storage_runtime, 0, sizeof(g_era_host_peer_storage_runtime));
    memset(&g_era_host_peer_storage_diagnostics, 0, sizeof(g_era_host_peer_storage_diagnostics));
    memset(g_era_host_peer_storage_manifest, 0, sizeof(g_era_host_peer_storage_manifest));
    memset(g_era_host_peer_storage_image, 0, sizeof(g_era_host_peer_storage_image));
    g_era_host_peer_storage_runtime.domain              = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    g_era_host_peer_storage_local.image_domain          = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    g_era_host_peer_storage_relation.idle_due_domain       = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    g_era_host_peer_storage_relation.delta_full_fetch_domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    g_era_host_peer_storage_local.initialized           = 1;
    era_split_communication_core_storage_capacity_init();

    for (uint8_t domain = 0; domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT; domain++) {
        g_era_host_peer_storage_manifest[domain].domain     = domain;
        g_era_host_peer_storage_manifest[domain].schema     = g_era_host_peer_storage_domains[domain].schema;
        g_era_host_peer_storage_manifest[domain].image_size = g_era_host_peer_storage_domains[domain].size;
        (void)era_host_peer_storage_capture_domain((era_split_eeprom_sync_domain_t)domain);
    }

    {
        /* Recency boot pass over the fresh captures. One record read serves
         * all seven domains; an ordinary boot with content at its baselines
         * writes nothing. An invalid record seeds the changed shadow
         * all-set — the same conservative degradation the recency snapshot
         * reports — so a fresh or CLEANed store lights the lamp through its
         * first convergence sweep and per-domain closes clear it, while an
         * ordinary converged boot seeds it dark. */
        era_host_peer_storage_baseline_record_t baseline_record;
        bool baseline_valid = era_host_peer_storage_read_baseline_record(&baseline_record);
        if (!baseline_valid) {
            g_era_host_peer_storage_local.changed_domain_mask = ERA_HOST_PEER_STORAGE_ALL_DOMAINS_MASK;
        }
        for (uint8_t domain = 0; domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT; domain++) {
            if (g_era_host_peer_storage_manifest[domain].source_revision != 0) {
                era_host_peer_storage_recency_note_boot(&baseline_record, baseline_valid,
                                                        (era_split_eeprom_sync_domain_t)domain,
                                                        g_era_host_peer_storage_manifest[domain].image_crc32);
            }
        }
    }

    g_era_host_peer_storage_relation.deferred_probe_domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    g_era_host_peer_storage_relation.idle_due_deadline_ms  = timer_read32();
    /* **The boot conservative `0x7F` is gone (D2)** and nothing replaces it.
     * It made every domain advertise settled-dirty until its own close, which
     * was the mask's way of saying "prove everything at relation open" — and
     * the relation-open audit already says exactly that, as a verify-all
     * summary that queues all seven. The two were the same instruction issued
     * twice, and the duplicate was the one the device caught: the initiator's
     * cache froze on this value and re-armed the family in batches of seven. A
     * boot with no settle behind it now claims nothing. */
    g_era_host_peer_storage_relation.settled_news_value = 0;
}

/* Arm a whole-family summary exchange.
 *
 * **Every direction decision goes through one of these.** A summary carries
 * both halves' changed masks, so the cell it lands a domain in is decided
 * from both halves' facts. That is the whole of the Slice 10 semantic: the
 * direction is derived, never fixed and never remembered.
 *
 * Two callers used to bypass it, and both cost an edit. Queueing a push
 * straight off this half's own settled capture used only this half's facts,
 * so a responder edit that had already settled was overwritten by a push
 * granted before the responder's hint arrived. And an aborted episode
 * re-armed the direction it had been given, which is a decision whose
 * inputs may have changed while the abort ran — device-shown 2026-07-29,
 * where a push aborted against a policy-closed responder came back as a
 * pull and destroyed the initiator's keymap edit.
 *
 * One compact exchange against a 1000 ms settle, or against a failure
 * backoff, is not a cost worth racing for.
 *
 * A summary already pending covers the news. The round's scope lives in
 * ROUND_VERIFY_ALL and is untouched here, so arming inside the mandatory
 * relation-open sweep cannot narrow it. */
static void era_host_peer_storage_arm_summary_refresh(void) {
    g_era_host_peer_storage_relation.arbitration_flags |= ERA_HOST_PEER_STORAGE_ARB_FLAG_SUMMARY_PENDING;
}

void era_host_peer_storage_note_host_news(uint8_t news_value) {
    if (!g_era_host_peer_storage_local.initialized) {
        return;
    }
    /* **The hint arms a summary and nothing else** (D2). It used to write
     * `probe_pending_mask` directly, which was the surviving third bypass of
     * `era_host_peer_storage_arm_summary_refresh()` -- the function whose own
     * comment says every direction decision goes through one of these. The two
     * earlier bypasses each cost an edit before they were closed, and this one
     * was the same shape: a direction queued from *one* half's facts, here the
     * peer's claim about itself, with no reading of what this half holds.
     *
     * A summary costs one compact exchange against a 1000 ms settle and answers
     * strictly more: it classifies the domain from both halves' current changed
     * masks, so a hint that arrives while this half has also edited resolves
     * through the conflict cell instead of being executed as a pull. That is
     * the Slice 10 semantic finally reaching the last carrier that bypassed it.
     *
     * What is left here is a news test, and the rest of this function is gone
     * with the machinery it fed.
     *
     * The old contract terms still bind and now bind trivially: a hint cannot
     * bypass quiet deadlines, the BUSY pin handoff, generation allocation or
     * the storage selection gates, because it no longer selects anything.
     *
     * **The news test stays and is now the whole function** (D1). This consumer
     * still costs work -- an armed summary is an exchange -- so a value already
     * taken must arm nothing, whichever carrier repeated it: a responder
     * answering many polls from one published snapshot, or the standing record
     * replaying its cached byte on an unrelated edge. Without the test that
     * replay armed a probe every time, device-measured 2026-08-09 as `match`
     * rising in multiples of seven against a contract asking for three, with
     * every failure counter at zero because nothing was failing.
     *
     * Zero is the peer saying it has nothing to claim -- a fresh relation, or a
     * responder whose own EEPROM policy is closed. It is recorded so a later
     * nonzero value is news, and it arms nothing itself: there is no work to do
     * about an absence of news. */
    uint8_t advertised = (uint8_t)(news_value & ERA_HOST_PEER_STORAGE_NEWS_VALUE_MAX);
    if (advertised == g_era_host_peer_storage_relation.peer_news_value) {
        return;
    }
    g_era_host_peer_storage_relation.peer_news_value = advertised;
    if (advertised != 0) {
        era_host_peer_storage_arm_summary_refresh();
    }
}

static void era_host_peer_storage_note_domain_dirty(era_split_eeprom_sync_domain_t domain, uint16_t quiet_ms) {
    if (!g_era_host_peer_storage_local.initialized || !era_host_peer_storage_domain_valid(domain)) {
        return;
    }

    era_host_peer_storage_manifest_entry_t *manifest = &g_era_host_peer_storage_manifest[domain];
    manifest->dirty_generation++;
    if (manifest->dirty_generation == 0) {
        manifest->dirty_generation = 1;
    }

    uint32_t domain_mask = era_host_peer_storage_domain_mask(domain);
    g_era_host_peer_storage_local.dirty_domain_mask |= domain_mask;
    g_era_host_peer_storage_local.dirty_deadline_valid_mask |= domain_mask;
    uint32_t now_ms = timer_read32();
    g_era_host_peer_storage_local.dirty_deadline_ms[domain] = now_ms + quiet_ms;
    era_host_peer_storage_refresh_next_dirty_deadline(now_ms);
    if (g_era_host_peer_storage_relation.active_due &&
        g_era_host_peer_storage_runtime.role == ERA_HOST_PEER_STORAGE_ROLE_PEER &&
        g_era_host_peer_storage_runtime.domain == (uint8_t)domain) {
        g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TARGET_DIRTY;
    }
    if (g_era_host_peer_storage_runtime.role == ERA_HOST_PEER_STORAGE_ROLE_HOST &&
        g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WRITE &&
        g_era_host_peer_storage_runtime.domain == (uint8_t)domain &&
        g_era_host_peer_storage_runtime.apply_abort_status == 0) {
        /* A local write into the domain a push is durably applying: latch the
         * abort, never interrupt the sliced write — the write-through rule.
         * The earlier push phases need no arm here: the publication
         * invalidation above already makes core1 refuse further staging. */
        g_era_host_peer_storage_runtime.apply_abort_status = ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED;
    }
    if (g_era_host_peer_storage_local.image_domain == (uint8_t)domain) {
        g_era_host_peer_storage_local.image_stale = 1;
        era_host_peer_storage_invalidate_image_publication();
        if (g_era_host_peer_storage_runtime.role == ERA_HOST_PEER_STORAGE_ROLE_HOST) {
            g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
        }
    }
}

void nvm_eeprom_changed_kb(uint16_t offset, uint16_t length) {
    if (!g_era_host_peer_storage_local.initialized || length == 0 ||
        (g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_APPLY_WRITE) != 0) {
        return;
    }

    for (uint8_t domain = 0; domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT; domain++) {
        if (era_host_peer_storage_range_overlaps(offset, length, &g_era_host_peer_storage_domains[domain])) {
            era_host_peer_storage_note_domain_dirty((era_split_eeprom_sync_domain_t)domain, ERA_HOST_PEER_STORAGE_DIRTY_QUIET_MS);
        }
    }
}

bool era_host_peer_storage_task(uint32_t now_ms) {
    if (!g_era_host_peer_storage_local.initialized || g_era_host_peer_storage_relation.active_due) {
        return false;
    }

    if (!timer_expired32(now_ms, g_era_host_peer_storage_relation.idle_due_deadline_ms) &&
        g_era_host_peer_storage_relation.idle_due_deadline_ms - now_ms > ERA_HOST_PEER_STORAGE_DIRTY_QUIET_MS) {
        /* Heal half-range timer staleness after long idle: no legitimate
         * probe or defer deadline exceeds the trailing quiet interval. */
        g_era_host_peer_storage_relation.idle_due_deadline_ms = now_ms;
    }

    if (g_era_host_peer_storage_local.revision_wrap_pending) {
        if (!timer_expired32(now_ms, g_era_host_peer_storage_relation.idle_due_deadline_ms)) {
            return false;
        }
        if (!era_split_transport_scheduler_rotate_storage_relation()) {
            g_era_host_peer_storage_diagnostics.quiesce_fail_count++;
            g_era_host_peer_storage_relation.idle_due_deadline_ms = now_ms + ERA_HOST_PEER_STORAGE_RETRY_MS;
            return false;
        }
        g_era_host_peer_storage_local.source_revision_counter   = 0;
        g_era_host_peer_storage_local.image_publication_seq     = 0;
        g_era_host_peer_storage_local.image_source_revision     = 0;
        g_era_host_peer_storage_local.image_crc32               = 0;
        g_era_host_peer_storage_local.image_size                = 0;
        g_era_host_peer_storage_local.image_domain              = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
        g_era_host_peer_storage_local.revision_wrap_pending     = 0;
        g_era_host_peer_storage_local.image_valid               = 0;
        g_era_host_peer_storage_local.image_stale               = 1;
        __DMB();
        g_era_host_peer_storage_local.dirty_domain_mask         = (1UL << ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) - 1UL;
        g_era_host_peer_storage_local.dirty_deadline_valid_mask = g_era_host_peer_storage_local.dirty_domain_mask;
        g_era_host_peer_storage_relation.settled_news_value        = 0;
        g_era_host_peer_storage_relation.probe_pending_mask        = 0;
        g_era_host_peer_storage_relation.deferred_probe_domain     = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
        for (uint8_t domain = 0; domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT; domain++) {
            g_era_host_peer_storage_manifest[domain].source_revision = 0;
            g_era_host_peer_storage_local.dirty_deadline_ms[domain]  = now_ms;
        }
        era_host_peer_storage_refresh_next_dirty_deadline(now_ms);
        return true;
    }

    if (g_era_host_peer_storage_local.next_dirty_deadline_valid &&
        timer_expired32(now_ms, g_era_host_peer_storage_local.next_dirty_deadline_ms)) {
        uint32_t pending = g_era_host_peer_storage_local.dirty_deadline_valid_mask;
        for (uint8_t domain = 0; domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT; domain++) {
            uint32_t domain_mask = 1UL << domain;
            if ((pending & domain_mask) == 0 || !timer_expired32(now_ms, g_era_host_peer_storage_local.dirty_deadline_ms[domain])) {
                continue;
            }

            bool captured = era_host_peer_storage_capture_domain((era_split_eeprom_sync_domain_t)domain);
            era_host_peer_storage_refresh_next_dirty_deadline(now_ms);
            if (captured) {
                g_era_host_peer_storage_relation.idle_due        = 1;
                g_era_host_peer_storage_relation.idle_due_domain = domain;
                g_era_host_peer_storage_relation.idle_due_kind   = ERA_HOST_PEER_STORAGE_TOKEN_PROBE;
                /* Settled capture after the trailing quiet interval. The
                 * recency judgment runs first because both settle signals are
                 * conditioned on its answer: a capture whose content sits on
                 * its own valid baseline contributes a zero changed mask to
                 * any summary, so the round either signal would buy is decided
                 * before it runs (device-read 2026-08-14/15, both edit
                 * directions of a matched pair). An invalid or torn record
                 * reads as departed, so degradation errs toward signalling. */
                bool departed = era_host_peer_storage_recency_note_settled((era_split_eeprom_sync_domain_t)domain,
                                                                           g_era_host_peer_storage_manifest[domain].image_crc32);
                if (departed) {
                    /* One step of the news value, whichever domain it was (D2).
                     * Stepping per capture rather than per *domain* is the
                     * property that closes the case no mask discipline could: a
                     * second edit of a domain already claimed moves the value
                     * even though the domain set has not changed, so the peer
                     * hears the second edit. `0` stays reserved for "nothing to
                     * claim", so the wrap skips it. A settle at the agreement
                     * steps nothing: every claim the value delivers is then
                     * backed by the changed shadow until the two-sided close,
                     * so no emitted claim ever outlives this half's own lamp
                     * arm — the peer's armed summary crosses back as the
                     * mirror for whatever remains. */
                    g_era_host_peer_storage_relation.settled_news_value =
                        (uint8_t)((g_era_host_peer_storage_relation.settled_news_value %
                                   ERA_HOST_PEER_STORAGE_NEWS_VALUE_MAX) + 1U);
                }
                /* The recency seat core1 answers SYNC_STATUS from is built at
                 * the snapshot publish boundary, so a settle that moves the
                 * recency mask has to invalidate the published snapshot or
                 * the responder keeps answering the summary from a view that
                 * predates its own change. It is also what makes retiring the
                 * hint on a served summary race-free: a settle landing after
                 * the answering snapshot now bumps the generation, and the
                 * drain's identity gate rejects the stale result instead of
                 * clearing a bit that was never reported. */
                g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
                if (g_era_host_peer_storage_relation.runtime_service_active &&
                    g_era_host_peer_storage_runtime.role == ERA_HOST_PEER_STORAGE_ROLE_PEER) {
                    if (departed) {
                        /* In-relation local settle that departed from the
                         * agreement: a direction has to be recomputed. Arm
                         * the summary rather than queueing the push directly
                         * - the reason is at `arm_summary_refresh`. A revert
                         * back onto a queued cell's domain needs no arm of
                         * its own: the stale cell's episode opens against
                         * matching content and closes MATCH, healing the
                         * baselines it re-proves. */
                        era_host_peer_storage_arm_summary_refresh();
                    }
                    /* The probe token drops whether or not a summary was
                     * armed: an at-agreement settle would otherwise run a
                     * MATCH probe for content neither signal claims. */
                    g_era_host_peer_storage_relation.idle_due = 0;
                }
            }
            return true;
        }
        era_host_peer_storage_refresh_next_dirty_deadline(now_ms);
    }

    /* No periodic idle patrol: tokens come only from the deferred
     * SOURCE_CHANGED slot, the relation-open arbitration, settled-dirty
     * mask hints, and local settled captures, paced by the storage retry
     * deadline. The grant order is summary, then conflict, then push, then
     * probe — arbitration first, then the directions it decided. */
    if (!g_era_host_peer_storage_relation.idle_due && timer_expired32(now_ms, g_era_host_peer_storage_relation.idle_due_deadline_ms)) {
        if (g_era_host_peer_storage_relation.deferred_probe_domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) {
            g_era_host_peer_storage_relation.idle_due              = 1;
            g_era_host_peer_storage_relation.idle_due_domain       = g_era_host_peer_storage_relation.deferred_probe_domain;
            g_era_host_peer_storage_relation.idle_due_kind         = ERA_HOST_PEER_STORAGE_TOKEN_PROBE;
            g_era_host_peer_storage_relation.deferred_probe_domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
            return true;
        }
        if ((g_era_host_peer_storage_relation.arbitration_flags & ERA_HOST_PEER_STORAGE_ARB_FLAG_SUMMARY_PENDING) != 0) {
            g_era_host_peer_storage_relation.idle_due        = 1;
            g_era_host_peer_storage_relation.idle_due_domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
            g_era_host_peer_storage_relation.idle_due_kind   = ERA_HOST_PEER_STORAGE_TOKEN_SUMMARY;
            return true;
        }
        /* A domain pending in both directions has changed on both halves:
         * collide it into the conflict queue so the counter exchange runs
         * before either direction moves content. */
        uint8_t collided = (uint8_t)(g_era_host_peer_storage_relation.probe_pending_mask &
                                     g_era_host_peer_storage_relation.push_pending_mask);
        if (collided != 0) {
            g_era_host_peer_storage_relation.conflict_pending_mask |= collided;
            g_era_host_peer_storage_relation.probe_pending_mask &= (uint8_t)~collided;
            g_era_host_peer_storage_relation.push_pending_mask &= (uint8_t)~collided;
        }
        for (uint8_t domain = 0; domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT; domain++) {
            uint8_t bit = (uint8_t)(1U << domain);
            bool    pending = (g_era_host_peer_storage_relation.conflict_pending_mask & bit) != 0 ||
                              (g_era_host_peer_storage_relation.push_pending_mask & bit) != 0 ||
                              (g_era_host_peer_storage_relation.probe_pending_mask & bit) != 0;
            if (!pending || (g_era_host_peer_storage_local.dirty_domain_mask & (1UL << domain)) != 0) {
                /* A locally dirty domain stays pending and is retried after
                 * its trailing-quiet capture instead of stalling the
                 * remaining drain behind its quiet interval. */
                continue;
            }
            /* The pending bit clears only when the episode actually starts,
             * so a parked token overwritten by a dirty-quiet capture is
             * re-issued instead of being lost. */
            g_era_host_peer_storage_relation.idle_due        = 1;
            g_era_host_peer_storage_relation.idle_due_domain = domain;
            g_era_host_peer_storage_relation.idle_due_kind =
                (g_era_host_peer_storage_relation.conflict_pending_mask & bit) != 0 ? ERA_HOST_PEER_STORAGE_TOKEN_CONFLICT :
                (g_era_host_peer_storage_relation.push_pending_mask & bit) != 0     ? ERA_HOST_PEER_STORAGE_TOKEN_PUSH :
                                                                                      ERA_HOST_PEER_STORAGE_TOKEN_PROBE;
            return true;
        }
        /* The round is over when nothing is queued, nothing is deferred and
         * no summary is pending. */
        bool round_over = (g_era_host_peer_storage_relation.arbitration_flags &
                           ERA_HOST_PEER_STORAGE_ARB_FLAG_SUMMARY_PENDING) == 0 &&
                          g_era_host_peer_storage_relation.deferred_probe_domain >= ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT &&
                          (g_era_host_peer_storage_relation.probe_pending_mask |
                           g_era_host_peer_storage_relation.push_pending_mask |
                           g_era_host_peer_storage_relation.conflict_pending_mask) == 0;
        if (round_over && g_era_host_peer_storage_relation.runtime_service_active &&
            g_era_host_peer_storage_runtime.role == ERA_HOST_PEER_STORAGE_ROLE_PEER) {
            /* Only here does the verify-all scope retire: holding it until
             * the round drains is what lets an episode abort mid-sweep and
             * still come back through a summary that proves every domain. */
            g_era_host_peer_storage_relation.arbitration_flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_ARB_FLAG_ROUND_VERIFY_ALL;
            /* **The round-end re-read retired here at D2, and it retired
             * because its case stopped existing.** It covered a domain proven
             * while the peer already held newer content for it: the peer's bit
             * stayed raised, so the advertised *mask* never moved, no further
             * news arrived, and the close had just dropped that domain from the
             * in-hand set -- claimed by the peer and held by nobody. Editing the
             * same key twice on the responder, the second edit landing inside
             * the first one's episode, reached it, and the loss was silent.
             *
             * A news counter cannot enter that state. The peer's newer content
             * came from a settled capture, a settled capture is what increments
             * the counter, and any increment is news. The condition the re-read
             * detected -- a claim that cannot move -- is unreachable when the
             * carrier only ever moves forward.
             *
             * What went with it is the bound it needed: one re-arm per domain
             * per advertised value, sized against the forced-set injection
             * image that measured 573 storage episodes in one window with
             * `xfer=0` (2026-07-29). A carrier that cannot lie about a level
             * needs no budget against a peer that lies about one. */
        }
    }

    return false;
}

static bool era_host_peer_storage_get_target_manifest(era_split_eeprom_sync_domain_t domain, era_host_peer_storage_manifest_entry_t *entry) {
    if (!g_era_host_peer_storage_local.initialized || !era_host_peer_storage_domain_valid(domain) || entry == NULL) {
        return false;
    }

    *entry = g_era_host_peer_storage_manifest[domain];
    return entry->schema == ERA_HOST_PEER_STORAGE_SCHEMA_V1 &&
           entry->image_size == g_era_host_peer_storage_domains[domain].size &&
           (g_era_host_peer_storage_local.dirty_domain_mask & era_host_peer_storage_domain_mask(domain)) == 0;
}

void era_host_peer_storage_get_foundation_snapshot(era_host_peer_storage_foundation_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    __DMB();
    *snapshot = (era_host_peer_storage_foundation_snapshot_t){
        .settled_news_value    = g_era_host_peer_storage_relation.settled_news_value,
        .probe_pending_mask    = g_era_host_peer_storage_relation.probe_pending_mask,
        .push_pending_mask     = g_era_host_peer_storage_relation.push_pending_mask,
        .conflict_pending_mask = g_era_host_peer_storage_relation.conflict_pending_mask,
        .peer_changed_mask     = g_era_host_peer_storage_relation.peer_changed_mask,
        .arbitration_flags     = g_era_host_peer_storage_relation.arbitration_flags,
    };
    __DMB();
}

uint8_t era_host_peer_storage_settled_news_value(void) {
    return g_era_host_peer_storage_local.initialized ? g_era_host_peer_storage_relation.settled_news_value : 0;
}

void era_host_peer_storage_get_recency_snapshot(era_host_peer_storage_recency_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));

    era_host_peer_storage_baseline_record_t record;
    bool valid                     = era_host_peer_storage_read_baseline_record(&record);
    snapshot->baseline_record_valid = valid ? 1 : 0;
    for (uint8_t domain = 0; domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT; domain++) {
        snapshot->divergence_counter[domain] = era_host_peer_storage_read_divergence_counter((era_split_eeprom_sync_domain_t)domain);
        bool changed = !valid || !g_era_host_peer_storage_local.initialized ||
                       g_era_host_peer_storage_manifest[domain].image_crc32 != record.baseline_crc32[domain];
        if (changed) {
            snapshot->changed_mask |= (uint8_t)(1U << domain);
        }
    }
}

static uint16_t era_host_peer_storage_next_nonzero_u16(uint16_t value) {
    value++;
    return value == 0 ? 1 : value;
}

static uint8_t era_host_peer_storage_chunk_length(uint16_t image_size, uint8_t chunk_id) {
    uint32_t offset = (uint32_t)chunk_id * ERA_HOST_PEER_STORAGE_CHUNK_BYTES;
    if (offset >= image_size) {
        return 0;
    }
    uint16_t remaining = (uint16_t)(image_size - offset);
    return (uint8_t)(remaining > ERA_HOST_PEER_STORAGE_CHUNK_BYTES ? ERA_HOST_PEER_STORAGE_CHUNK_BYTES : remaining);
}

/* Relation admission: the one place the engine consults the relation mode.
 * These three helpers are the engine's whole relation coupling — everything
 * below them is generation-based and relation-neutral — so a relation that
 * gains storage service is added here and nowhere else.
 *
 * Slice 10.5 is the first slice to take that claim up, and it held: adding
 * DUAL-HOST needed these three arms and no fourth place. The relation pairs
 * differ only in which peer fact confirms the role — HOST-PEER's initiator
 * faces a host-open peer and its responder a no-host peer, while in
 * DUAL-HOST both halves are host-open and the physical side decides, which
 * the mode value already carries. `context_peer`/`context_host` below then
 * cross-check `local_initiator`, so a mode that disagrees with the settled
 * wire role admits neither side. */
static bool era_host_peer_storage_relation_serviced(const era_host_peer_storage_runtime_context_t *context) {
    return context->mode == ERA_SPLIT_MODE_HOST_PEER_PEER || context->mode == ERA_SPLIT_MODE_HOST_PEER_HOST ||
           context->mode == ERA_SPLIT_MODE_DUAL_HOST_LEFT || context->mode == ERA_SPLIT_MODE_DUAL_HOST_RIGHT;
}

static bool era_host_peer_storage_relation_admits_responder(const era_host_peer_storage_runtime_context_t *context) {
    return (context->mode == ERA_SPLIT_MODE_HOST_PEER_HOST && context->peer_no_host) ||
           (context->mode == ERA_SPLIT_MODE_DUAL_HOST_RIGHT && context->peer_host_open);
}

/* The initiator side alone consults the local requested policy: a
 * policy-off responder still pins and answers POLICY_CLOSED in its admitted
 * slot, which is why no policy term appears in the responder arm. */
static bool era_host_peer_storage_relation_admits_initiator(const era_host_peer_storage_runtime_context_t *context) {
    return (context->mode == ERA_SPLIT_MODE_HOST_PEER_PEER || context->mode == ERA_SPLIT_MODE_DUAL_HOST_LEFT) &&
           context->peer_host_open && context->local_policy_requested;
}

static bool era_host_peer_storage_context_host(const era_host_peer_storage_runtime_context_t *context) {
    return context != NULL && era_host_peer_storage_relation_admits_responder(context) && context->owner_ready &&
           !context->local_initiator && context->peer_known &&
           context->local_bulk_page_supported && context->peer_bulk_page_supported;
}

static bool era_host_peer_storage_context_peer(const era_host_peer_storage_runtime_context_t *context) {
    return context != NULL && era_host_peer_storage_relation_admits_initiator(context) && context->owner_ready &&
           context->local_initiator && context->peer_known &&
           context->local_bulk_page_supported && context->peer_bulk_page_supported;
}

static void era_host_peer_storage_note_reject_status(uint8_t status) {
    switch ((era_split_eeprom_sync_status_t)status) {
        case ERA_SPLIT_EEPROM_SYNC_STATUS_UNSUPPORTED_SCHEMA:
        case ERA_SPLIT_EEPROM_SYNC_STATUS_SIZE_MISMATCH:
            g_era_host_peer_storage_diagnostics.version_reject_count++;
            break;
        case ERA_SPLIT_EEPROM_SYNC_STATUS_UNSUPPORTED_DOMAIN:
            g_era_host_peer_storage_diagnostics.domain_reject_count++;
            break;
        case ERA_SPLIT_EEPROM_SYNC_STATUS_INTEGRITY_FAIL:
            g_era_host_peer_storage_diagnostics.integrity_reject_count++;
            break;
        case ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED:
            if (g_era_host_peer_storage_diagnostics.source_changed_count < UINT16_MAX) {
                g_era_host_peer_storage_diagnostics.source_changed_count++;
            }
            break;
        case ERA_SPLIT_EEPROM_SYNC_STATUS_STALE:
        case ERA_SPLIT_EEPROM_SYNC_STATUS_ROLE_CHANGED:
            g_era_host_peer_storage_diagnostics.stale_count++;
            break;
        default:
            break;
    }
}

static uint32_t era_host_peer_storage_probe_pacing_ms(uint8_t episode_domain, bool aborted) {
    if (episode_domain >= ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) {
        return ERA_HOST_PEER_STORAGE_RETRY_MS;
    }
    if (!aborted) {
        if (g_era_host_peer_storage_relation.probe_backoff_domain == episode_domain) {
            g_era_host_peer_storage_relation.probe_backoff_streak = 0;
        }
        if (g_era_host_peer_storage_relation.delta_full_fetch_domain == episode_domain) {
            g_era_host_peer_storage_relation.delta_full_fetch_domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
        }
        return ERA_HOST_PEER_STORAGE_RETRY_MS;
    }
    if (g_era_host_peer_storage_relation.probe_backoff_domain != episode_domain ||
        g_era_host_peer_storage_relation.probe_backoff_streak == 0) {
        g_era_host_peer_storage_relation.probe_backoff_domain = episode_domain;
        g_era_host_peer_storage_relation.probe_backoff_streak = 1;
    } else if (g_era_host_peer_storage_relation.probe_backoff_streak < ERA_HOST_PEER_STORAGE_BACKOFF_MAX_SHIFT) {
        g_era_host_peer_storage_relation.probe_backoff_streak++;
    }
    uint32_t delay_ms = (uint32_t)ERA_HOST_PEER_STORAGE_RETRY_MS << g_era_host_peer_storage_relation.probe_backoff_streak;
    return delay_ms > ERA_HOST_PEER_STORAGE_BACKOFF_MAX_MS ? ERA_HOST_PEER_STORAGE_BACKOFF_MAX_MS : delay_ms;
}

static void era_host_peer_storage_reset_peer_episode(uint32_t now_ms, bool aborted, bool revalidate) {
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    if (aborted) {
        era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_ABORT, g_era_host_peer_storage_runtime.last_status);
    }
#endif
    uint8_t  episode_domain         = g_era_host_peer_storage_runtime.domain;
    uint16_t transaction_generation = g_era_host_peer_storage_runtime.transaction_generation;
    uint16_t request_generation     = g_era_host_peer_storage_runtime.request_generation;
    uint16_t relation_generation    = g_era_host_peer_storage_runtime.relation_generation;
    uint16_t policy_generation      = g_era_host_peer_storage_runtime.policy_generation;
    uint8_t  runtime_flags          = g_era_host_peer_storage_runtime.flags;
    bool     route_was_exclusive    = (runtime_flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE) != 0;
    /* The flush-at-close keys on "this episode ran an exclusive transfer",
     * which since R4 is TRANSFER_ACTIVE rather than the exclusivity flag:
     * exclusivity ends at transfer-verified, so at a successful close it is
     * already clear while the flush is still owed. An abort's wire-priority
     * exclusivity keeps reaching the flush through route_was_exclusive. */
    bool     transfer_ran           = (runtime_flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TRANSFER_ACTIVE) != 0;
    memset(&g_era_host_peer_storage_runtime, 0, sizeof(g_era_host_peer_storage_runtime));
    g_era_host_peer_storage_runtime.transaction_generation = transaction_generation;
    g_era_host_peer_storage_runtime.request_generation     = request_generation;
    g_era_host_peer_storage_runtime.relation_generation    = relation_generation;
    g_era_host_peer_storage_runtime.policy_generation      = policy_generation;
    g_era_host_peer_storage_runtime.role                   = ERA_HOST_PEER_STORAGE_ROLE_PEER;
    g_era_host_peer_storage_runtime.state                  = ERA_HOST_PEER_STORAGE_RUNTIME_IDLE;
    g_era_host_peer_storage_runtime.domain                 = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    g_era_host_peer_storage_relation.active_due               = 0;
    if (g_era_host_peer_storage_relation.deferred_probe_domain >= ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) {
        /* Probe pacing only; an armed SOURCE_CHANGED defer keeps its full
         * trailing-quiet deadline. A failed episode stretches the pacing by
         * the failing domain's consecutive-failure backoff. */
        g_era_host_peer_storage_relation.idle_due_deadline_ms = now_ms + era_host_peer_storage_probe_pacing_ms(episode_domain, aborted);
    } else {
        (void)era_host_peer_storage_probe_pacing_ms(episode_domain, aborted);
    }
    if (aborted) {
        /* The pending bit was consumed when the episode started, so an
         * aborted domain must come back or it silently drops out of the
         * mandatory seven-domain sweep until the next relation event. What
         * comes back is the *need to arbitrate*, not the direction.
         *
         * This used to re-arm a direction, recovered from the episode state
         * machine — which the abort path has already overwritten with
         * PEER_ABORT, so every push that aborted came back as a pull. On
         * 2026-07-29 that pull executed against a policy-closed responder's
         * older content and destroyed the initiator's keymap edit: the exact
         * loss DUAL-HOST storage exists to prevent.
         *
         * Recovering the direction correctly would not have been enough.
         * A decision is valid only for the arbitration round that produced
         * it, and an abort may run for seconds during which either half can
         * change. So the domain returns through a fresh summary and the
         * direction is derived again from both halves' facts. The contract
         * clause this replaces — "the aborted push re-arms as push, the
         * responder's later settled hint arrives as a pull signal, and the
         * collision forces the counter exchange" — was a repair that
         * depended on a second event arriving; a summary needs none.
         *
         * Pacing is unchanged: the failing domain's backoff was applied
         * above and gates the grant loop, so the retry rate does not move.
         *
         * A terminal refusal is not retried at all. Its reason cannot change
         * without an event that would itself re-trigger, so re-arming only
         * spins — 155 aborts in one measured window, which is also what made
         * the storage indicator flash. */
        if (episode_domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT &&
            (runtime_flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TERMINAL_ABORT) == 0) {
            era_host_peer_storage_arm_summary_refresh();
        } else if (episode_domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) {
            /* Terminal refusal: the reason cannot change without an event
             * that would itself re-trigger the domain, so the pair can do
             * nothing further with this divergence and the indicator must
             * not wear it as unfinished work — a policy-closed peer would
             * otherwise hold the lamp red forever. Display shadow only: the
             * persisted baseline still differs, so a later re-trigger (the
             * policy enable is news by construction) re-derives the
             * divergence from the durable facts and the lamp re-lights with
             * the real work. */
            era_host_peer_storage_note_changed_shadow((era_split_eeprom_sync_domain_t)episode_domain, false);
        }
        g_era_host_peer_storage_diagnostics.abort_count++;
    } else {
        /* **Nothing hint-shaped retires here since D2.** A close used to drop
         * the domain from an in-hand set, so the peer's next assertion of it
         * read as news; that set existed only because the hint named domains
         * and armed probes for them. The hint arms a summary now, and a summary
         * re-derives every domain from both halves' current facts, so there is
         * nothing left for a close to keep in step. The storm this retirement
         * amplified -- every replay finding the domain freshly absent and
         * arming another probe -- has no path left either. */
        g_era_host_peer_storage_diagnostics.close_count++;
    }
    if (route_was_exclusive || transfer_ran || revalidate) {
        era_split_transport_scheduler_force_storage_recovery(revalidate);
    }
}

static void era_host_peer_storage_defer_peer_domain(uint32_t now_ms, uint8_t domain, bool aborted) {
    era_host_peer_storage_reset_peer_episode(now_ms, aborted, false);
    if (domain >= ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) {
        return;
    }
    g_era_host_peer_storage_relation.idle_due              = 0;
    g_era_host_peer_storage_relation.idle_due_domain       = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    g_era_host_peer_storage_relation.deferred_probe_domain = domain;
    /* The deferred slot owns this domain's retry and carries its full trailing
     * quiet deadline, so drop any pending bit (including an abort re-arm) that
     * would let the ascending sweep scan re-issue it early. The park no longer
     * needs a second guard against the peer's advertisement re-arming it (D2):
     * that advertisement arms a summary rather than this domain's probe bit,
     * and a summary landing on a parked domain classifies it without disturbing
     * the slot that owns its retry. */
    g_era_host_peer_storage_relation.probe_pending_mask &= (uint8_t)~era_host_peer_storage_domain_mask(domain);
    g_era_host_peer_storage_relation.idle_due_deadline_ms  = now_ms + ERA_HOST_PEER_STORAGE_DIRTY_QUIET_MS;
}

static void era_host_peer_storage_begin_relation_audit(uint32_t now_ms) {
    /* Mandatory arbitration open on every relation (re)establishment: one
     * whole-family SYNC_STATUS summary exchange precedes the per-domain
     * episodes, and its classification fills the cell work queues that the
     * ascending drain then services at the storage retry cadence. Every
     * domain lands in exactly one queue, so the seven-domain bounded
     * completion the old blanket sweep guaranteed is preserved — the sweep
     * is now cell-shaped instead of always-pull. */
    g_era_host_peer_storage_relation.probe_pending_mask    = 0;
    g_era_host_peer_storage_relation.push_pending_mask     = 0;
    g_era_host_peer_storage_relation.conflict_pending_mask = 0;
    g_era_host_peer_storage_relation.peer_changed_mask     = 0;
    /* Verify-all: this summary's job is the mandatory sweep, so every domain
     * lands in a queue and the bounded seven-domain completion is preserved.
     * An in-session refresh arms without this flag and classifies only what
     * the two halves declare changed. */
    g_era_host_peer_storage_relation.arbitration_flags     = ERA_HOST_PEER_STORAGE_ARB_FLAG_SUMMARY_PENDING |
                                                         ERA_HOST_PEER_STORAGE_ARB_FLAG_ROUND_VERIFY_ALL;
    g_era_host_peer_storage_relation.deferred_probe_domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    g_era_host_peer_storage_relation.idle_due              = 0;
    g_era_host_peer_storage_relation.idle_due_domain       = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    g_era_host_peer_storage_relation.idle_due_kind         = ERA_HOST_PEER_STORAGE_TOKEN_PROBE;
    g_era_host_peer_storage_relation.idle_due_deadline_ms  = now_ms;
    /* Forget the peer's last claim with the relation (D2). The audit above has
     * already armed the summary this hint would arm, so the peer's first
     * advertisement of the new relation costs nothing whatever it says; what
     * the clear buys is that the *next* value after it reads as news, which a
     * record carried across a relation would not guarantee. */
    g_era_host_peer_storage_relation.peer_news_value = 0;
}

/* The counterpart of the audit above: the one place a half that cannot drain
 * the initiator's work gives it up (Slice 11.7).
 *
 * Everything that audit arms is consumed by `era_host_peer_storage_peer_task`
 * alone, and a responder never selects a storage route at all. So a half that
 * queued work as a HOST-PEER PEER and then became a DUAL-HOST responder holds
 * a probe mask, a conflict cell and an idle token with nothing left that can
 * execute them — and it keeps arming more, because a settled capture is
 * role-independent while the token it raises is not.
 *
 * Nothing is lost. Every route from responder back to initiator is a mode
 * change, which rotates the relation generation, which is what makes
 * `era_host_peer_storage_peer_task` open with a fresh verify-all audit. That
 * audit re-derives each domain's direction from both halves' *current* facts,
 * which is strictly better than executing a decision taken in a relation that
 * no longer exists — the Arbitration rule that a decision is valid only for
 * the round that produced it.
 *
 * What stays is what is a fact rather than queued work: `settled_news_value`
 * -- `settled_news` until D2 replaced the seven-bit per-domain mask with
 * a forward-only news counter, the same seat carrying a different fact --
 * because maintaining it is role-independent by contract and it is precisely
 * what a responder advertises; the per-domain failure backoff, which persists
 * across relation events by contract; and the delta full-fetch repair mark,
 * which steers this domain's next transfer whichever role runs it.
 *
 * The `arb` record bits stay too, and that is the Slice 10.6 reading rather
 * than an oversight: a leftover bit is worth keeping for the one thing it is
 * good for, which is saying what the previous era did. Only the two that
 * describe a round still owed are cleared, because a half that cannot run one
 * must not report one pending. */
static void era_host_peer_storage_release_initiator_queues(void) {
    uint8_t queued = (uint8_t)(g_era_host_peer_storage_relation.probe_pending_mask |
                               g_era_host_peer_storage_relation.push_pending_mask |
                               g_era_host_peer_storage_relation.conflict_pending_mask |
                               g_era_host_peer_storage_relation.peer_changed_mask |
                               g_era_host_peer_storage_relation.idle_due |
                               (uint8_t)(g_era_host_peer_storage_relation.arbitration_flags &
                                         ERA_HOST_PEER_STORAGE_ARB_ROUND_OWED_FLAGS));
    if (queued == 0 && g_era_host_peer_storage_relation.deferred_probe_domain >= ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) {
        return;
    }
    g_era_host_peer_storage_relation.probe_pending_mask    = 0;
    g_era_host_peer_storage_relation.push_pending_mask     = 0;
    g_era_host_peer_storage_relation.conflict_pending_mask = 0;
    g_era_host_peer_storage_relation.peer_changed_mask     = 0;
    g_era_host_peer_storage_relation.deferred_probe_domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    g_era_host_peer_storage_relation.idle_due              = 0;
    g_era_host_peer_storage_relation.idle_due_domain       = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    g_era_host_peer_storage_relation.idle_due_kind         = ERA_HOST_PEER_STORAGE_TOKEN_PROBE;
    g_era_host_peer_storage_relation.arbitration_flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_ARB_ROUND_OWED_FLAGS;
}

static void era_host_peer_storage_host_close(uint32_t now_ms, bool aborted, bool revalidate) {
    (void)now_ms;
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    if (aborted) {
        era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_ABORT, g_era_host_peer_storage_runtime.last_status);
    }
#endif
    /* No hint retirement here, and since D2 there is none anywhere -- the news
     * value only ever steps forward. The rule this note was written for still
     * governs what the retirement left behind: the durable agreement belongs
     * to the convergence itself, not to one role's close, so the baseline is
     * written from `era_host_peer_storage_note_domain_converged` on whichever
     * half converged the domain. This close is reached by aborts too, which is
     * why nothing durable was ever allowed to hang off it. */
    bool route_was_exclusive = (g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE) != 0;
    /* Same R4 rule as the initiator's close: the flush keys on the episode
     * having run an exclusive transfer, which TRANSFER_ACTIVE carries to the
     * close now that exclusivity itself ends at transfer-verified. */
    bool transfer_ran        = (g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TRANSFER_ACTIVE) != 0;
    g_era_host_peer_storage_runtime.state = ERA_HOST_PEER_STORAGE_RUNTIME_HOST_CLOSED;
    g_era_host_peer_storage_runtime.flags &= (uint8_t)~(ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING |
                                                        ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE |
                                                        ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TRANSFER_ACTIVE);
    g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
    g_era_host_peer_storage_relation.active_due = 0;
    if (aborted) {
        g_era_host_peer_storage_diagnostics.abort_count++;
    } else {
        g_era_host_peer_storage_diagnostics.close_count++;
    }
    if (route_was_exclusive || transfer_ran || revalidate) {
        era_split_transport_scheduler_force_storage_recovery(revalidate);
    }
}

static bool era_host_peer_storage_publish_current_image(era_split_eeprom_sync_domain_t domain, uint32_t image_crc32) {
    if (!era_host_peer_storage_domain_valid(domain) || g_era_host_peer_storage_local.image_publication_seq >= UINT32_MAX - 1U) {
        return false;
    }
    const era_host_peer_storage_domain_descriptor_t *descriptor = &g_era_host_peer_storage_domains[domain];
    uint32_t source_revision = era_host_peer_storage_next_revision();
    if (source_revision == 0) {
        return false;
    }

    uint32_t odd_seq = era_host_peer_storage_image_publish_open();

    era_host_peer_storage_image_publish_record(domain, descriptor, image_crc32, source_revision);
    /* Inside the window, unlike the capture path's, which clears the same two
       masks after the close and only if the domain did not re-dirty during its
       read. There is no read here to re-dirty against. */
    g_era_host_peer_storage_local.dirty_domain_mask &= ~era_host_peer_storage_domain_mask(domain);
    g_era_host_peer_storage_local.dirty_deadline_valid_mask &= ~era_host_peer_storage_domain_mask(domain);
    era_host_peer_storage_image_publish_close(odd_seq);
    return true;
}

static bool era_host_peer_storage_submit_peer_request(const era_host_peer_storage_runtime_context_t *context) {
    bool summary_episode = g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_SYNC_STATUS;
    if (context == NULL || context->general_initiator_pending || context->status_revalidation_due ||
        (g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING) != 0 ||
        (!summary_episode &&
         !era_host_peer_storage_domain_valid((era_split_eeprom_sync_domain_t)g_era_host_peer_storage_runtime.domain))) {
        return false;
    }

    uint16_t next_request_generation = era_host_peer_storage_next_nonzero_u16(g_era_host_peer_storage_runtime.request_generation);
    if (next_request_generation == 1 && g_era_host_peer_storage_runtime.request_generation == UINT16_MAX) {
        g_era_host_peer_storage_diagnostics.stale_count++;
        uint8_t domain = g_era_host_peer_storage_runtime.domain;
        if (!era_split_transport_scheduler_rotate_storage_relation()) {
            g_era_host_peer_storage_diagnostics.quiesce_fail_count++;
            g_era_host_peer_storage_runtime.retry_deadline_ms = context->now_ms + ERA_HOST_PEER_STORAGE_RETRY_MS;
            return false;
        }
        memset(&g_era_host_peer_storage_runtime, 0, sizeof(g_era_host_peer_storage_runtime));
        g_era_host_peer_storage_runtime.domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
        g_era_host_peer_storage_relation.active_due = 0;
        if (domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) {
            g_era_host_peer_storage_relation.idle_due        = 1;
            g_era_host_peer_storage_relation.idle_due_domain = domain;
        }
        return false;
    }

    era_split_eeprom_sync_domain_t domain = (era_split_eeprom_sync_domain_t)g_era_host_peer_storage_runtime.domain;
    const era_host_peer_storage_domain_descriptor_t *descriptor =
        summary_episode ? NULL : &g_era_host_peer_storage_domains[domain];
    era_split_communication_core_storage_initiator_request_t request;
    memset(&request, 0, sizeof(request));
    request.not_after_us           = timer_hw->timerawl + 5000U;
    request.owner_epoch            = context->owner_epoch;
    request.relation_generation    = context->relation_generation;
    request.request_generation     = next_request_generation;
    request.policy_generation      = context->policy_generation;
    request.transaction_generation = g_era_host_peer_storage_runtime.transaction_generation;
    request.image_size             = summary_episode ? 0 : descriptor->size;
    request.response_window_ms     = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONSE_WINDOW_MS;
    request.domain                 = (uint8_t)domain;
    request.schema                 = summary_episode ? ERA_HOST_PEER_STORAGE_SCHEMA_V1 : descriptor->schema;

    switch ((era_host_peer_storage_runtime_state_t)g_era_host_peer_storage_runtime.state) {
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PROBE:
            request.operation   = ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ;
            request.image_crc32 = g_era_host_peer_storage_manifest[domain].image_crc32;
            break;
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_TRANSFER:
            request.operation       = ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ;
            request.source_revision = g_era_host_peer_storage_runtime.source_revision;
            request.chunk_id        = g_era_host_peer_storage_runtime.next_chunk;
            request.detail          = era_host_peer_storage_chunk_length(descriptor->size, request.chunk_id);
            if (request.detail == 0) {
                return false;
            }
            /* Request-embedded chunk-CRC delta: pre-stage the local chunk
             * bytes into the unreceived staging slice and advertise their
             * 24-bit CRC as the hint. Zero (no hint) always fetches full
             * data; an integrity-marked domain fetches full until its next
             * successful close. */
            request.image_crc32 = 0;
            if (g_era_host_peer_storage_relation.delta_full_fetch_domain != (uint8_t)domain) {
                uint32_t chunk_offset = (uint32_t)request.chunk_id * ERA_HOST_PEER_STORAGE_CHUNK_BYTES;
                eeprom_read_block(&g_era_host_peer_storage_image[chunk_offset],
                                  (const void *)(uintptr_t)(descriptor->address + chunk_offset), request.detail);
                request.image_crc32 =
                    era_split_wire_crc32(&g_era_host_peer_storage_image[chunk_offset], request.detail) & 0xFFFFFFUL;
            }
            break;
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_APPLY:
            request.operation       = ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ;
            request.source_revision = g_era_host_peer_storage_runtime.source_revision;
            request.image_crc32     = g_era_host_peer_storage_runtime.expected_crc32;
            break;
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_COMPLETE:
            request.operation       = ERA_SPLIT_EEPROM_SYNC_OP_COMPLETE_REQ;
            request.source_revision = g_era_host_peer_storage_runtime.source_revision;
            request.image_crc32     = g_era_host_peer_storage_runtime.expected_crc32;
            break;
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_ABORT:
            request.operation       = ERA_SPLIT_EEPROM_SYNC_OP_ABORT_REQ;
            request.source_revision = g_era_host_peer_storage_runtime.source_revision;
            request.detail          = g_era_host_peer_storage_runtime.last_status;
            break;
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_SYNC_STATUS:
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_CONFLICT_STATUS: {
            /* Both arbitration forms carry this half's cold recency facts:
             * the changed mask/flag in detail, baseline validity in
             * chunk_id, and (per-conflict only) the 16-bit divergence
             * counter in the crc32 seat. */
            era_host_peer_storage_recency_snapshot_t recency;
            era_host_peer_storage_get_recency_snapshot(&recency);
            request.operation = ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ;
            request.chunk_id  = recency.baseline_record_valid;
            if (summary_episode) {
                request.detail = (uint8_t)(recency.changed_mask & ERA_HOST_PEER_STORAGE_ALL_DOMAINS_MASK);
            } else {
                request.detail      = (recency.changed_mask & (uint8_t)(1U << (uint8_t)domain)) != 0 ? 1 : 0;
                request.image_crc32 = recency.divergence_counter[(uint8_t)domain];
            }
            break;
        }
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_OPEN:
            request.operation       = ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ;
            request.detail          = (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_OPEN;
            request.source_revision = g_era_host_peer_storage_runtime.source_revision;
            request.image_crc32     = g_era_host_peer_storage_runtime.expected_crc32;
            break;
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_CHUNKS:
            /* The chunk bytes come from this half's own publication: refuse
             * to submit against a superseded or stale image and abort so the
             * re-arbitrated episode pushes the newer content instead. */
            if (!g_era_host_peer_storage_local.image_valid || g_era_host_peer_storage_local.image_stale ||
                g_era_host_peer_storage_local.image_domain != (uint8_t)domain ||
                g_era_host_peer_storage_local.image_source_revision != g_era_host_peer_storage_runtime.source_revision) {
                era_host_peer_storage_peer_begin_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED, context->now_ms);
                return false;
            }
            request.operation                     = ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ;
            request.source_revision               = g_era_host_peer_storage_runtime.source_revision;
            request.chunk_id                      = g_era_host_peer_storage_runtime.next_chunk;
            request.detail                        = era_host_peer_storage_chunk_length(descriptor->size, request.chunk_id);
            request.image_address                 = (uint32_t)(uintptr_t)g_era_host_peer_storage_image;
            request.image_publication_seq_address = (uint32_t)(uintptr_t)&g_era_host_peer_storage_local.image_publication_seq;
            if (request.detail == 0) {
                return false;
            }
            break;
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_APPLY:
            request.operation       = ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ;
            request.detail          = (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_APPLY;
            request.source_revision = g_era_host_peer_storage_runtime.source_revision;
            request.image_crc32     = g_era_host_peer_storage_runtime.expected_crc32;
            break;
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_COMPLETE:
            request.operation       = ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ;
            request.detail          = (uint8_t)ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_COMPLETE;
            request.source_revision = g_era_host_peer_storage_runtime.source_revision;
            request.image_crc32     = g_era_host_peer_storage_runtime.expected_crc32;
            break;
        default:
            return false;
    }

    if (!era_split_communication_core_storage_reserve_initiator_result(request.request_generation)) {
        g_era_host_peer_storage_diagnostics.initiator_full_count++;
        return false;
    }
    if (!era_split_communication_core_storage_publish_initiator_request(&request)) {
        era_split_communication_core_storage_cancel_initiator_result(request.request_generation);
        g_era_host_peer_storage_diagnostics.initiator_full_count++;
        return false;
    }

    g_era_host_peer_storage_runtime.owner_epoch         = context->owner_epoch;
    g_era_host_peer_storage_runtime.relation_generation = context->relation_generation;
    g_era_host_peer_storage_runtime.request_generation  = request.request_generation;
    g_era_host_peer_storage_runtime.policy_generation   = context->policy_generation;
    g_era_host_peer_storage_runtime.pending_operation   = request.operation;
    g_era_host_peer_storage_runtime.retry_deadline_ms   = 0;
    g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING;
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    if (request.operation == ERA_SPLIT_EEPROM_SYNC_OP_COMPLETE_REQ) {
        era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_COMPLETE_SUBMIT, 0);
    }
#endif
    era_split_communication_core_wake();
    return true;
}

static bool era_host_peer_storage_peer_result_identity_matches(const era_split_communication_core_storage_initiator_result_t *result) {
    return result != NULL && result->owner_epoch == g_era_host_peer_storage_runtime.owner_epoch &&
           result->relation_generation == g_era_host_peer_storage_runtime.relation_generation &&
           result->request_generation == g_era_host_peer_storage_runtime.request_generation &&
           result->policy_generation == g_era_host_peer_storage_runtime.policy_generation &&
           result->transaction_generation == g_era_host_peer_storage_runtime.transaction_generation &&
           result->domain == g_era_host_peer_storage_runtime.domain &&
           result->schema == ERA_HOST_PEER_STORAGE_SCHEMA_V1;
}

static void era_host_peer_storage_peer_begin_abort(uint8_t status, uint32_t now_ms) {
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_ABORT, status);
#endif
    g_era_host_peer_storage_runtime.state             = ERA_HOST_PEER_STORAGE_RUNTIME_PEER_ABORT;
    g_era_host_peer_storage_runtime.last_status       = status;
    g_era_host_peer_storage_runtime.retry_count       = 0;
    g_era_host_peer_storage_runtime.retry_deadline_ms = now_ms;
    /* Route exclusivity here is wire priority for the ABORT_REQ, not data
     * movement, so TRANSFER_ACTIVE is deliberately not raised with it. */
    g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE;
    switch ((era_split_eeprom_sync_status_t)status) {
        /* Terminal refusals: the responder's answer cannot change without an
         * event that would itself re-trigger this domain, so the episode
         * closes instead of re-arming. INTEGRITY_FAIL is deliberately absent
         * — it retries with hints disabled, which is a repair, not a
         * refusal. */
        case ERA_SPLIT_EEPROM_SYNC_STATUS_POLICY_CLOSED:
        case ERA_SPLIT_EEPROM_SYNC_STATUS_UNSUPPORTED_DOMAIN:
        case ERA_SPLIT_EEPROM_SYNC_STATUS_UNSUPPORTED_SCHEMA:
        case ERA_SPLIT_EEPROM_SYNC_STATUS_SIZE_MISMATCH:
            g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TERMINAL_ABORT;
            break;
        default:
            break;
    }
    if (status == ERA_SPLIT_EEPROM_SYNC_STATUS_INTEGRITY_FAIL &&
        g_era_host_peer_storage_runtime.domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) {
        /* A 24-bit hint false match surfaces here; retry the domain with
         * hints disabled so recovery is deterministic. */
        g_era_host_peer_storage_relation.delta_full_fetch_domain = g_era_host_peer_storage_runtime.domain;
    }
    era_host_peer_storage_note_reject_status(status);
}

static void era_host_peer_storage_peer_retry_or_abort(uint32_t now_ms, bool timeout) {
    if (timeout) {
        g_era_host_peer_storage_diagnostics.timeout_count++;
    }
    g_era_host_peer_storage_diagnostics.retry_count++;
    if (g_era_host_peer_storage_runtime.retry_count < UINT8_MAX) {
        g_era_host_peer_storage_runtime.retry_count++;
    }
    if (g_era_host_peer_storage_runtime.retry_count < ERA_HOST_PEER_STORAGE_MAX_FAILURES) {
        g_era_host_peer_storage_runtime.retry_deadline_ms = now_ms + ERA_HOST_PEER_STORAGE_RETRY_MS;
        return;
    }
    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_ABORT) {
        era_host_peer_storage_reset_peer_episode(now_ms, true, true);
        return;
    }
    era_host_peer_storage_peer_begin_abort(timeout ? ERA_SPLIT_EEPROM_SYNC_STATUS_TIMEOUT : ERA_SPLIT_EEPROM_SYNC_STATUS_STALE, now_ms);
}

static bool era_host_peer_storage_apply_begin(const era_host_peer_storage_runtime_context_t *context) {
    era_split_eeprom_sync_domain_t domain = (era_split_eeprom_sync_domain_t)g_era_host_peer_storage_runtime.domain;
    if (context == NULL || !era_host_peer_storage_domain_valid(domain) ||
        g_era_host_peer_storage_runtime.staged_bytes != g_era_host_peer_storage_runtime.image_size ||
        era_split_wire_crc32(g_era_host_peer_storage_image, g_era_host_peer_storage_runtime.image_size) != g_era_host_peer_storage_runtime.expected_crc32 ||
        (domain == ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG && !era_host_peer_storage_reserved_era_config_is_zero()) ||
        (g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TARGET_DIRTY) != 0) {
        g_era_host_peer_storage_diagnostics.integrity_reject_count++;
        return false;
    }

#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_APPLY_BEGIN, 0);
    era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_EEPROM_BEGIN, 0);
#endif
    /* Live-wire sliced apply: Core1 keeps running and the write proceeds in
     * bounded slices from the cold cadence.
     *
     * **What feeds the peer's silence watch is core1's standing-exchange
     * liveness beat, in both serviced relations** -- DUAL-HOST since Slice 11.7
     * and HOST-PEER since R2.
     *
     * This comment carries its own history because it has now been wrong twice
     * in the same place, each time by naming a mechanism that had moved. It
     * first said the session-status keepalive fed the watch: a core0 frame
     * emitted from inside the write that is precisely what stops core0
     * emitting, measured at one frame in a 1728 ms window and deleted in Slice
     * 11.7. It then said HOST-PEER had no standing grant and that this window
     * was an open hole -- true when written, and closed by R2 giving that
     * relation the same grant.
     *
     * **The HOST-PEER half is mechanism, not measurement.** Only DUAL-HOST has
     * run the Storage Apply-Liveness Gate's liveness leg; the HOST-PEER leg is
     * owed (era_performance_gates.md). Take the beat as present on this path
     * and do not take the path as proven. */
    g_era_host_peer_storage_runtime.state              = ERA_HOST_PEER_STORAGE_RUNTIME_PEER_APPLY_WRITE;
    g_era_host_peer_storage_runtime.apply_offset       = 0;
    g_era_host_peer_storage_runtime.apply_abort_status = 0;
    g_era_host_peer_storage_runtime.retry_count        = 0;
    g_era_host_peer_storage_runtime.retry_deadline_ms  = 0;
    g_era_host_peer_storage_runtime.flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING;
    return true;
}

static void era_host_peer_storage_apply_write_latch_abort(uint8_t status) {
    if (g_era_host_peer_storage_runtime.apply_abort_status == 0) {
        g_era_host_peer_storage_runtime.apply_abort_status = status;
    }
}

/* The episode deadline's one re-arm point, and it is deliberately one (Slice
 * 11.7). It fires when the episode advances a phase and never when a request is
 * merely repeated, which is what makes the bound mean "this episode stopped
 * advancing" instead of "this episode took a while".
 *
 * That distinction is the whole correction. The deadline used to be armed once
 * at episode start and cover every phase, so one 5000 ms was charged for the
 * transfer *and* the peer's durable apply, on both halves at once -- and the
 * waiting half worst of all, since it can neither see the apply nor influence
 * it. Measured 2026-08-02: a 16.2 KiB macro pull spent 394 ms transferring and
 * then 2095 ms applying, both against the same budget on each half.
 *
 * A phase is the state plus the pinned operation because the two halves express
 * an advance differently: the initiator walks states, and a responder waiting
 * through the initiator's apply stays in `HOST_PINNED` and moves only its
 * pinned operation.
 *
 * Doing it here rather than at each boundary is not tidiness. A per-boundary
 * list drifts, and one of its entries is actively wrong: re-arming on the
 * initiator's repeated `APPLY_READY` poll during a push reads an answer core1
 * serves from a published snapshot, so it proves the responder's core1 is alive
 * and says nothing about its core0. A responder whose core0 died mid-write
 * would hold the initiator open forever on that reading. */
static void era_host_peer_storage_note_episode_phase(uint32_t now_ms) {
    if (g_era_host_peer_storage_runtime.state == g_era_host_peer_storage_runtime.deadline_state &&
        g_era_host_peer_storage_runtime.pending_operation == g_era_host_peer_storage_runtime.deadline_operation) {
        return;
    }
    g_era_host_peer_storage_runtime.deadline_state      = g_era_host_peer_storage_runtime.state;
    g_era_host_peer_storage_runtime.deadline_operation  = g_era_host_peer_storage_runtime.pending_operation;
    g_era_host_peer_storage_runtime.episode_deadline_ms = now_ms + ERA_HOST_PEER_STORAGE_EPISODE_MS;
}

static void era_host_peer_storage_apply_write_slice(void) {
    const era_host_peer_storage_domain_descriptor_t *descriptor = &g_era_host_peer_storage_domains[g_era_host_peer_storage_runtime.domain];
    uint16_t offset    = g_era_host_peer_storage_runtime.apply_offset;
    uint16_t remaining = (uint16_t)(descriptor->size - offset);
    uint16_t length    = remaining > ERA_HOST_PEER_STORAGE_APPLY_SLICE_BYTES ? ERA_HOST_PEER_STORAGE_APPLY_SLICE_BYTES : remaining;
    /* Dirty-note suppression covers only the apply's own slice write so
     * foreign local writes keep normal dirty tracking between slices. */
    g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_APPLY_WRITE;
    eeprom_update_block(&g_era_host_peer_storage_image[offset], (void *)(uintptr_t)(descriptor->address + offset), length);
    g_era_host_peer_storage_runtime.flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_APPLY_WRITE;
    g_era_host_peer_storage_runtime.apply_offset = (uint16_t)(offset + length);
}

static bool era_host_peer_storage_apply_write_finish(const era_host_peer_storage_runtime_context_t *context) {
    era_split_eeprom_sync_domain_t domain = (era_split_eeprom_sync_domain_t)g_era_host_peer_storage_runtime.domain;
    const era_host_peer_storage_domain_descriptor_t *descriptor = &g_era_host_peer_storage_domains[domain];
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_EEPROM_END, 0);
#endif
    eeprom_read_block(g_era_host_peer_storage_image, (const void *)(uintptr_t)descriptor->address, descriptor->size);
    uint32_t readback_crc32  = era_split_wire_crc32(g_era_host_peer_storage_image, descriptor->size);
    bool     content_durable = readback_crc32 == g_era_host_peer_storage_runtime.expected_crc32;
    if (content_durable) {
        /* Reload even on the deferred-abort path: the read-back matched the
         * validated image, so the runtime never keeps diverging state. */
        era_split_eeprom_sync_reload_domain_kb(domain);
    } else {
        g_era_host_peer_storage_local.image_valid             = 0;
        g_era_host_peer_storage_local.image_stale             = 1;
        g_era_host_peer_storage_relation.delta_full_fetch_domain = (uint8_t)domain;
        g_era_host_peer_storage_diagnostics.integrity_reject_count++;
    }

    uint8_t deferred_abort = g_era_host_peer_storage_runtime.apply_abort_status;
    if (!content_durable) {
        era_host_peer_storage_peer_begin_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_INTEGRITY_FAIL, context->now_ms);
        return false;
    }
    if (deferred_abort != 0) {
        /* Write-through completed under a deferred abort: content is durable
         * and reloaded, but the episode identity is gone. Leave the manifest
         * stale so the reopen audit re-proves the domain (expected MATCH). */
        g_era_host_peer_storage_local.image_valid = 0;
        g_era_host_peer_storage_local.image_stale = 1;
        era_host_peer_storage_peer_begin_abort(deferred_abort, context->now_ms);
        return false;
    }

    bool durable = era_host_peer_storage_publish_current_image(domain, readback_crc32);
    /* The brief terminal rotation is the retained identity anchor: capacity
     * flush, a new owner epoch, and forced session revalidation in a few
     * milliseconds of wire silence, far under the responder stale limit. */
    bool rotated = durable && era_split_transport_scheduler_rotate_storage_relation();
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_CORE1_RESTART, rotated ? 1 : 0);
#endif
    if (!rotated) {
        g_era_host_peer_storage_diagnostics.quiesce_fail_count++;
        era_host_peer_storage_peer_begin_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_STALE, context->now_ms);
        return false;
    }
    g_era_host_peer_storage_diagnostics.restart_count++;
    g_era_host_peer_storage_diagnostics.apply_count++;
    g_era_host_peer_storage_runtime.state             = ERA_HOST_PEER_STORAGE_RUNTIME_PEER_REVALIDATE;
    g_era_host_peer_storage_runtime.retry_count       = 0;
    g_era_host_peer_storage_runtime.retry_deadline_ms = context->now_ms + ERA_HOST_PEER_STORAGE_RETRY_MS;
    return true;
}

static bool era_host_peer_storage_push_apply_finish(const era_host_peer_storage_runtime_context_t *context) {
    /* The responder-side durable apply tail: the same read-back, reload, and
     * write-through discipline as the initiator's apply_write_finish, minus
     * its wire revalidation — for push the durable declaration travels as
     * the COMPLETE poll answer, and the identity rotation runs only after
     * the initiator has been told. */
    era_split_eeprom_sync_domain_t domain = (era_split_eeprom_sync_domain_t)g_era_host_peer_storage_runtime.domain;
    const era_host_peer_storage_domain_descriptor_t *descriptor = &g_era_host_peer_storage_domains[domain];
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_EEPROM_END, 0);
#endif
    eeprom_read_block(g_era_host_peer_storage_image, (const void *)(uintptr_t)descriptor->address, descriptor->size);
    uint32_t readback_crc32  = era_split_wire_crc32(g_era_host_peer_storage_image, descriptor->size);
    bool     content_durable = readback_crc32 == g_era_host_peer_storage_runtime.expected_crc32;
    if (content_durable) {
        /* Reload even on the deferred-abort path: the read-back matched the
         * validated image, so the runtime never keeps diverging state. */
        era_split_eeprom_sync_reload_domain_kb(domain);
    } else {
        g_era_host_peer_storage_local.image_valid = 0;
        g_era_host_peer_storage_local.image_stale = 1;
        g_era_host_peer_storage_diagnostics.integrity_reject_count++;
    }

    uint8_t deferred_abort = g_era_host_peer_storage_runtime.apply_abort_status;
    if (!content_durable) {
        era_host_peer_storage_host_close(context->now_ms, true, true);
        return false;
    }
    if (deferred_abort != 0) {
        /* Write-through completed under a deferred abort: content is durable
         * and reloaded, but the episode identity is gone. Leave the manifest
         * stale so the reopen audit re-proves the domain (expected MATCH). */
        g_era_host_peer_storage_local.image_valid = 0;
        g_era_host_peer_storage_local.image_stale = 1;
        g_era_host_peer_storage_runtime.last_status = deferred_abort;
        era_host_peer_storage_host_close(context->now_ms, true, true);
        return false;
    }
    if (!era_host_peer_storage_publish_current_image(domain, readback_crc32)) {
        era_host_peer_storage_host_close(context->now_ms, true, true);
        return false;
    }
    era_host_peer_storage_note_domain_converged(domain, g_era_host_peer_storage_runtime.expected_crc32);
    g_era_host_peer_storage_diagnostics.apply_count++;
    g_era_host_peer_storage_runtime.state             = ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_DURABLE;
    g_era_host_peer_storage_runtime.pending_operation = ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ;
    g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
    return true;
}

static void era_host_peer_storage_apply_write_run_to_completion(const era_host_peer_storage_runtime_context_t *context) {
    /* Terminal role/mode exit with the cursor open: finish the local write of
     * the already-validated image synchronously so no torn durable state can
     * feed a standalone runtime, then let the caller reset the episode. */
    era_host_peer_storage_apply_write_latch_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_STALE);
    while (g_era_host_peer_storage_runtime.apply_offset < g_era_host_peer_storage_runtime.image_size) {
        era_host_peer_storage_apply_write_slice();
    }
    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WRITE) {
        (void)era_host_peer_storage_push_apply_finish(context);
    } else {
        (void)era_host_peer_storage_apply_write_finish(context);
    }
}

static bool era_host_peer_storage_process_peer_result_record(const era_host_peer_storage_runtime_context_t *context, const era_split_communication_core_storage_initiator_result_t *result) {
    g_era_host_peer_storage_runtime.flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING;
    if (!era_host_peer_storage_peer_result_identity_matches(result)) {
        g_era_host_peer_storage_diagnostics.stale_count++;
        return false;
    }
    if (result->result != ERA_SPLIT_TRANSACTION_RESULT_OK || result->failure != ERA_SPLIT_TRANSACTION_FAILURE_NONE) {
        era_host_peer_storage_peer_retry_or_abort(context->now_ms,
                                                  result->result == ERA_SPLIT_TRANSACTION_RESULT_MISS ||
                                                      result->failure == ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT);
        return false;
    }
    if (result->operation == ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP &&
        g_era_host_peer_storage_runtime.pending_operation == ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ &&
        result->status == ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED &&
        result->source_revision == g_era_host_peer_storage_runtime.source_revision) {
        uint8_t domain = g_era_host_peer_storage_runtime.domain;
        g_era_host_peer_storage_runtime.last_status = result->status;
        era_host_peer_storage_note_reject_status(result->status);
        era_host_peer_storage_defer_peer_domain(context->now_ms, domain, true);
        return false;
    }
    if (result->operation != era_split_eeprom_sync_response_operation(g_era_host_peer_storage_runtime.pending_operation)) {
        g_era_host_peer_storage_diagnostics.stale_count++;
        era_host_peer_storage_peer_retry_or_abort(context->now_ms, false);
        return false;
    }

    g_era_host_peer_storage_runtime.retry_count = 0;
    switch ((era_split_eeprom_sync_op_t)result->operation) {
        case ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP:
            g_era_host_peer_storage_diagnostics.proof_count++;
            g_era_host_peer_storage_runtime.last_status = result->status;
            if (result->status == ERA_SPLIT_EEPROM_SYNC_STATUS_MATCH) {
                g_era_host_peer_storage_diagnostics.match_count++;
                /* MATCH proves the probe's cached CRC is the agreement; a
                 * target-dirty local write since the probe aborts instead of
                 * reaching this arm, so the manifest CRC is still that value. */
                uint8_t match_domain = g_era_host_peer_storage_runtime.domain;
                if (match_domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) {
                    era_host_peer_storage_note_domain_converged((era_split_eeprom_sync_domain_t)match_domain,
                                                                g_era_host_peer_storage_manifest[match_domain].image_crc32);
                }
                era_host_peer_storage_reset_peer_episode(context->now_ms, false, false);
            } else if (result->status == ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER && result->source_revision != 0) {
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
                era_host_peer_storage_cause_timeline_begin(ERA_HOST_PEER_STORAGE_ROLE_PEER,
                                                           g_era_host_peer_storage_runtime.domain,
                                                           g_era_host_peer_storage_runtime.transaction_generation);
#endif
                g_era_host_peer_storage_runtime.source_revision = result->source_revision;
                g_era_host_peer_storage_runtime.expected_crc32  = result->image_crc32;
                g_era_host_peer_storage_runtime.image_size      = g_era_host_peer_storage_domains[result->domain].size;
                g_era_host_peer_storage_runtime.staged_bytes    = 0;
                g_era_host_peer_storage_runtime.next_chunk      = 0;
                g_era_host_peer_storage_runtime.state           = ERA_HOST_PEER_STORAGE_RUNTIME_PEER_TRANSFER;
                g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE |
                                                         ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TRANSFER_ACTIVE;
                g_era_host_peer_storage_local.image_domain = result->domain;
                g_era_host_peer_storage_local.image_valid  = 0;
                g_era_host_peer_storage_local.image_stale  = 0;
                era_host_peer_storage_invalidate_image_publication();
                g_era_host_peer_storage_diagnostics.transfer_count++;
            } else if (result->status == ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY) {
                era_host_peer_storage_peer_retry_or_abort(context->now_ms, false);
            } else if (result->status == ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED) {
                uint8_t domain = g_era_host_peer_storage_runtime.domain;
                era_host_peer_storage_note_reject_status(result->status);
                era_host_peer_storage_defer_peer_domain(context->now_ms, domain, true);
            } else {
                era_host_peer_storage_peer_begin_abort(result->status, context->now_ms);
            }
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_RSP: {
            uint32_t offset = (uint32_t)result->chunk_id * ERA_HOST_PEER_STORAGE_CHUNK_BYTES;
            uint8_t expected_length = era_host_peer_storage_chunk_length(g_era_host_peer_storage_runtime.image_size, g_era_host_peer_storage_runtime.next_chunk);
            bool hint_match = result->data_length == 0;
            if (hint_match) {
                /* The zero-length content-match form is legal only when this
                 * request advertised a nonzero hint; the pre-staged local
                 * bytes it was computed from are still in place. */
                uint8_t domain = g_era_host_peer_storage_runtime.domain;
                if (g_era_host_peer_storage_relation.delta_full_fetch_domain == domain || expected_length == 0 ||
                    (era_split_wire_crc32(&g_era_host_peer_storage_image[offset], expected_length) & 0xFFFFFFUL) == 0) {
                    era_host_peer_storage_peer_begin_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_INTEGRITY_FAIL, context->now_ms);
                    break;
                }
            }
            if (result->source_revision != g_era_host_peer_storage_runtime.source_revision ||
                result->chunk_id != g_era_host_peer_storage_runtime.next_chunk ||
                (!hint_match && result->data_length != expected_length) ||
                offset != g_era_host_peer_storage_runtime.staged_bytes ||
                expected_length > g_era_host_peer_storage_runtime.image_size - offset) {
                era_host_peer_storage_peer_begin_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_INTEGRITY_FAIL, context->now_ms);
                break;
            }
            if (!hint_match) {
                memcpy(&g_era_host_peer_storage_image[offset], result->data, result->data_length);
            }
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
            era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_CHUNK_RESULT, result->chunk_id);
#endif
            g_era_host_peer_storage_runtime.staged_bytes = (uint16_t)(offset + expected_length);
            g_era_host_peer_storage_runtime.next_chunk++;
            g_era_host_peer_storage_diagnostics.chunk_count++;
            if (g_era_host_peer_storage_runtime.staged_bytes == g_era_host_peer_storage_runtime.image_size) {
                if (era_split_wire_crc32(g_era_host_peer_storage_image, g_era_host_peer_storage_runtime.image_size) !=
                    g_era_host_peer_storage_runtime.expected_crc32) {
                    era_host_peer_storage_peer_begin_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_INTEGRITY_FAIL, context->now_ms);
                } else {
                    g_era_host_peer_storage_runtime.state = ERA_HOST_PEER_STORAGE_RUNTIME_PEER_APPLY;
                    /* Wire exclusivity ends at transfer-verified (R4): the
                     * staged image just validated against the proof CRC, and
                     * everything after this is a local flash operation plus
                     * compact control exchanges that need no lock. Normal
                     * routes are admitted between the apply's EEPROM
                     * operations — the matrix crosses in those windows —
                     * never inside a sliced erase's gap, which runs the
                     * keyboard pass and not the wire (era_invariants.md).
                     * TRANSFER_ACTIVE deliberately stays: the episode is
                     * still moving data as the indicator means it. */
                    g_era_host_peer_storage_runtime.flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE;
                }
            }
            break;
        }
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_RSP: {
            if (result->status == ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED) {
                uint8_t domain = g_era_host_peer_storage_runtime.domain;
                g_era_host_peer_storage_runtime.last_status = result->status;
                era_host_peer_storage_note_reject_status(result->status);
                era_host_peer_storage_defer_peer_domain(context->now_ms, domain, true);
                break;
            }
            bool result_matches = result->status == ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY &&
                                  result->source_revision == g_era_host_peer_storage_runtime.source_revision &&
                                  result->image_crc32 == g_era_host_peer_storage_runtime.expected_crc32;
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
            if (result_matches) {
                era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_APPLY_READY, result->status);
            }
#endif
            uint16_t request_generation = result->request_generation;
            bool result_released = era_split_communication_core_storage_release_initiator_result(request_generation);
            if (!result_matches || !result_released ||
                !era_host_peer_storage_apply_begin(context)) {
                era_host_peer_storage_peer_begin_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_INTEGRITY_FAIL, context->now_ms);
            }
            return result_released;
        }
        case ERA_SPLIT_EEPROM_SYNC_OP_CLOSE_RSP:
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
            era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_COMPLETE_RESULT, result->status);
#endif
            if (result->status == ERA_SPLIT_EEPROM_SYNC_STATUS_COMPLETE &&
                result->source_revision == g_era_host_peer_storage_runtime.source_revision &&
                result->image_crc32 == g_era_host_peer_storage_runtime.expected_crc32) {
                g_era_host_peer_storage_diagnostics.complete_count++;
                era_host_peer_storage_note_domain_converged((era_split_eeprom_sync_domain_t)g_era_host_peer_storage_runtime.domain,
                                                            g_era_host_peer_storage_runtime.expected_crc32);
                era_host_peer_storage_reset_peer_episode(context->now_ms, false, false);
            } else {
                era_host_peer_storage_peer_begin_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_STALE, context->now_ms);
            }
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP:
            era_host_peer_storage_reset_peer_episode(context->now_ms, true, true);
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_RSP:
            if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_SYNC_STATUS) {
                /* Whole-family classification. Converged baselines are
                 * identical on both halves, so one summary decides three of
                 * the four cells outright: an unchanged half probes (a
                 * MATCH verify or the pull the proof turns it into), a
                 * one-sided local change pushes, and both-changed queues
                 * the counter exchange. An invalid record on either half
                 * already reads as all-changed, which lands everything in
                 * the conflict cell — the conservative degradation. */
                era_host_peer_storage_recency_snapshot_t recency;
                era_host_peer_storage_get_recency_snapshot(&recency);
                uint8_t mine = (uint8_t)(recency.changed_mask & ERA_HOST_PEER_STORAGE_ALL_DOMAINS_MASK);
                uint8_t peer = (uint8_t)(result->data[0] & ERA_HOST_PEER_STORAGE_ALL_DOMAINS_MASK);
                bool    verify_all = (g_era_host_peer_storage_relation.arbitration_flags &
                                   ERA_HOST_PEER_STORAGE_ARB_FLAG_ROUND_VERIFY_ALL) != 0;
                g_era_host_peer_storage_relation.peer_changed_mask = peer;
                /* The verify cell (neither half changed) is the mandatory
                 * sweep's business and nobody else's. At relation open every
                 * unchanged domain is probed so the seven-domain bounded
                 * completion holds. In session the two halves already agreed
                 * at their last convergence, so probing what neither declares
                 * changed would cost six MATCH episodes to move one edit -
                 * a hint that behaves like a poll once it fires. */
                g_era_host_peer_storage_relation.probe_pending_mask |=
                    verify_all ? (uint8_t)(~mine & ERA_HOST_PEER_STORAGE_ALL_DOMAINS_MASK)
                               : (uint8_t)(peer & (uint8_t)~mine);
                g_era_host_peer_storage_relation.push_pending_mask |= (uint8_t)(mine & (uint8_t)~peer);
                g_era_host_peer_storage_relation.conflict_pending_mask |= (uint8_t)(mine & peer);
                /* The round's scope outlives the token that consumed it, so
                 * an episode aborting mid-sweep returns through a summary
                 * that still proves every domain. The drain clears it.
                 *
                 * Whether this summary found work is no longer recorded: it
                 * bounded a round-end whole-family level re-read, which Slice
                 * 11.7's per-domain carrier replaced with a per-domain re-arm
                 * carrying its own bound. D2 then retired that re-arm and its
                 * budget along with the re-read they descended from, so there
                 * is nothing left for this fact to bound -- a value that only
                 * moves forward cannot claim a level it will not lower, which
                 * is the lie every one of those bounds was sized against. */
                g_era_host_peer_storage_relation.arbitration_flags =
                    (uint8_t)((g_era_host_peer_storage_relation.arbitration_flags &
                               (uint8_t)~ERA_HOST_PEER_STORAGE_ARB_FLAG_SUMMARY_PENDING) |
                              ERA_HOST_PEER_STORAGE_ARB_FLAG_SUMMARY_DONE);
                era_host_peer_storage_reset_peer_episode(context->now_ms, false, false);
            } else if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_CONFLICT_STATUS) {
                /* The conflict cell's single rule: larger divergence count
                 * wins, tie to Left — the same rule in-session and at
                 * reopen. The winner's direction is queued; the close keeps
                 * the domain in hand and skips the backoff reset by closing
                 * under the domainless identity. */
                era_host_peer_storage_recency_snapshot_t recency;
                era_host_peer_storage_get_recency_snapshot(&recency);
                uint8_t  conflict_domain = g_era_host_peer_storage_runtime.domain;
                uint8_t  conflict_bit    = (uint8_t)(1U << conflict_domain);
                uint16_t mine_count      = recency.divergence_counter[conflict_domain];
                uint16_t peer_count      = era_split_wire_get16(&result->data[2]);
                bool     mine_wins       = mine_count > peer_count ||
                                       (mine_count == peer_count && context->local_left != 0);
                if (mine_wins) {
                    g_era_host_peer_storage_relation.push_pending_mask |= conflict_bit;
                } else {
                    g_era_host_peer_storage_relation.probe_pending_mask |= conflict_bit;
                }
                g_era_host_peer_storage_runtime.domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
                era_host_peer_storage_reset_peer_episode(context->now_ms, false, false);
            } else {
                g_era_host_peer_storage_diagnostics.stale_count++;
            }
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_RSP:
            switch ((era_split_eeprom_sync_status_t)result->status) {
                case ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY:
                    era_host_peer_storage_peer_retry_or_abort(context->now_ms, false);
                    break;
                case ERA_SPLIT_EEPROM_SYNC_STATUS_MATCH:
                    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_OPEN) {
                        g_era_host_peer_storage_diagnostics.match_count++;
                        era_host_peer_storage_note_domain_converged((era_split_eeprom_sync_domain_t)g_era_host_peer_storage_runtime.domain,
                                                                    g_era_host_peer_storage_runtime.expected_crc32);
                        era_host_peer_storage_reset_peer_episode(context->now_ms, false, false);
                    } else {
                        era_host_peer_storage_peer_retry_or_abort(context->now_ms, false);
                    }
                    break;
                case ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER:
                    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_OPEN) {
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
                        era_host_peer_storage_cause_timeline_begin(ERA_HOST_PEER_STORAGE_ROLE_PEER,
                                                                   g_era_host_peer_storage_runtime.domain,
                                                                   g_era_host_peer_storage_runtime.transaction_generation);
#endif
                        g_era_host_peer_storage_runtime.state      = ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_CHUNKS;
                        g_era_host_peer_storage_runtime.next_chunk = 0;
                        g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE |
                                                                 ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TRANSFER_ACTIVE;
                        g_era_host_peer_storage_diagnostics.transfer_count++;
                    } else if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_CHUNKS &&
                               result->chunk_id == g_era_host_peer_storage_runtime.next_chunk) {
                        g_era_host_peer_storage_runtime.next_chunk++;
                        g_era_host_peer_storage_diagnostics.chunk_count++;
                        if (era_host_peer_storage_chunk_length(g_era_host_peer_storage_runtime.image_size,
                                                               g_era_host_peer_storage_runtime.next_chunk) == 0) {
                            g_era_host_peer_storage_runtime.state = ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_APPLY;
                            /* The push initiator's transfer ends at its last
                             * acknowledged chunk (R4): what follows is the
                             * apply trigger and complete polls — compact
                             * exchanges that interleave with normal routes.
                             * The responder's own CRC validation is its
                             * boundary, on its half. */
                            g_era_host_peer_storage_runtime.flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE;
                        }
                    } else if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_CHUNKS) {
                        g_era_host_peer_storage_diagnostics.duplicate_count++;
                    } else {
                        era_host_peer_storage_peer_retry_or_abort(context->now_ms, false);
                    }
                    break;
                case ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY:
                    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_APPLY ||
                        g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_COMPLETE) {
                        /* Still applying on the responder: keep polling at the
                         * retry cadence. The valid answer already reset the
                         * failure streak, so a long sliced write cannot trip
                         * the three-strike abort.
                         *
                         * **The repeated poll must not re-arm the episode
                         * deadline**, and this is the one place in this family
                         * where that is not obvious. Core1 answers these polls
                         * from the responder's published snapshot for the whole
                         * write, so an answer proves the responder's *core1* is
                         * alive -- not that its core0 is still writing. Feeding
                         * the deadline from it would make a responder whose
                         * core0 died mid-write answer `APPLY_READY` forever
                         * from a stale snapshot and hold this half open with
                         * it, which is the same shape as the defect the whole
                         * lane exists to remove.
                         *
                         * The state transition below re-arms once, which is
                         * correct: the responder entering its durable write is
                         * a phase advance. Everything after it is a repeat. */
                        g_era_host_peer_storage_runtime.state = ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_COMPLETE;
                        g_era_host_peer_storage_runtime.retry_deadline_ms = context->now_ms + ERA_HOST_PEER_STORAGE_RETRY_MS;
                    } else {
                        era_host_peer_storage_peer_retry_or_abort(context->now_ms, false);
                    }
                    break;
                case ERA_SPLIT_EEPROM_SYNC_STATUS_COMPLETE:
                    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_COMPLETE ||
                        g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_APPLY) {
                        g_era_host_peer_storage_diagnostics.complete_count++;
                        era_host_peer_storage_note_domain_converged((era_split_eeprom_sync_domain_t)g_era_host_peer_storage_runtime.domain,
                                                                    g_era_host_peer_storage_runtime.expected_crc32);
                        era_host_peer_storage_reset_peer_episode(context->now_ms, false, false);
                    } else {
                        era_host_peer_storage_peer_retry_or_abort(context->now_ms, false);
                    }
                    break;
                case ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED:
                    /* The responder's own content moved under the episode:
                     * both halves have now changed, so the abort re-arm
                     * (push for a push episode) plus the responder's
                     * dirty-pin refusal and its later settled hint drive the
                     * pair into the conflict exchange rather than losing
                     * either side's edits. */
                    g_era_host_peer_storage_runtime.last_status = result->status;
                    era_host_peer_storage_note_reject_status(result->status);
                    era_host_peer_storage_reset_peer_episode(context->now_ms, true, false);
                    break;
                default:
                    era_host_peer_storage_peer_begin_abort(result->status, context->now_ms);
                    break;
            }
            break;
        default:
            era_host_peer_storage_peer_retry_or_abort(context->now_ms, false);
            break;
    }
    return false;
}

static void era_host_peer_storage_process_peer_result(const era_host_peer_storage_runtime_context_t *context) {
    const era_split_communication_core_storage_initiator_result_t *result;
    if (!era_split_communication_core_storage_acquire_initiator_result(&result)) {
        return;
    }
    uint16_t request_generation = result->request_generation;
    if (!era_host_peer_storage_process_peer_result_record(context, result)) {
        if (!era_split_communication_core_storage_release_initiator_result(request_generation)) {
            era_split_communication_core_storage_cancel_initiator_result(request_generation);
            g_era_host_peer_storage_diagnostics.stale_count++;
        }
    }
}

static bool era_host_peer_storage_start_peer_episode(const era_host_peer_storage_runtime_context_t *context) {
    uint8_t token_kind = g_era_host_peer_storage_relation.idle_due_kind;
    bool    summary    = token_kind == ERA_HOST_PEER_STORAGE_TOKEN_SUMMARY;
    if (context == NULL || !g_era_host_peer_storage_relation.idle_due || context->general_initiator_pending ||
        context->status_revalidation_due ||
        (!summary && !era_host_peer_storage_domain_valid((era_split_eeprom_sync_domain_t)g_era_host_peer_storage_relation.idle_due_domain))) {
        return false;
    }
    era_split_eeprom_sync_domain_t domain = (era_split_eeprom_sync_domain_t)g_era_host_peer_storage_relation.idle_due_domain;
    era_host_peer_storage_manifest_entry_t manifest;
    memset(&manifest, 0, sizeof(manifest));
    if (!summary && !era_host_peer_storage_get_target_manifest(domain, &manifest)) {
        return false;
    }
    if (token_kind == ERA_HOST_PEER_STORAGE_TOKEN_PUSH) {
        uint8_t domain_bit = (uint8_t)era_host_peer_storage_domain_mask(domain);
        if ((g_era_host_peer_storage_local.dirty_domain_mask & (1UL << (uint8_t)domain)) != 0) {
            /* Dirtied between grant and start: park the token back and let
             * the trailing-quiet capture re-issue it with settled content. */
            g_era_host_peer_storage_relation.idle_due = 0;
            g_era_host_peer_storage_relation.push_pending_mask |= domain_bit;
            g_era_host_peer_storage_relation.idle_due_deadline_ms = context->now_ms + ERA_HOST_PEER_STORAGE_RETRY_MS;
            return false;
        }
        /* A push serves from this half's own published image: capture the
         * domain fresh so the publication core1 reads is the settled
         * content the episode's CRC names. */
        if (!era_host_peer_storage_capture_domain(domain)) {
            g_era_host_peer_storage_relation.idle_due = 0;
            g_era_host_peer_storage_relation.push_pending_mask |= domain_bit;
            g_era_host_peer_storage_relation.idle_due_deadline_ms = context->now_ms + ERA_HOST_PEER_STORAGE_RETRY_MS;
            return false;
        }
        if (!era_host_peer_storage_get_target_manifest(domain, &manifest)) {
            return false;
        }
    }
    if (g_era_host_peer_storage_runtime.transaction_generation == UINT16_MAX) {
        g_era_host_peer_storage_diagnostics.stale_count++;
        if (!era_split_transport_scheduler_rotate_storage_relation()) {
            g_era_host_peer_storage_diagnostics.quiesce_fail_count++;
            return false;
        }
        g_era_host_peer_storage_runtime.transaction_generation = 0;
        g_era_host_peer_storage_runtime.request_generation     = 0;
        return false;
    }

    g_era_host_peer_storage_runtime.transaction_generation = era_host_peer_storage_next_nonzero_u16(g_era_host_peer_storage_runtime.transaction_generation);
    g_era_host_peer_storage_runtime.owner_epoch             = context->owner_epoch;
    g_era_host_peer_storage_runtime.relation_generation     = context->relation_generation;
    g_era_host_peer_storage_runtime.policy_generation       = context->policy_generation;
    g_era_host_peer_storage_runtime.peer_usb_epoch          = context->peer_usb_epoch;
    g_era_host_peer_storage_runtime.peer_host_open_generation  = context->peer_host_open_generation;
    g_era_host_peer_storage_runtime.peer_host_close_generation = context->peer_host_close_generation;
    g_era_host_peer_storage_runtime.episode_deadline_ms      = context->now_ms + ERA_HOST_PEER_STORAGE_EPISODE_MS;
    g_era_host_peer_storage_runtime.retry_deadline_ms        = context->now_ms;
    g_era_host_peer_storage_runtime.source_revision          = token_kind == ERA_HOST_PEER_STORAGE_TOKEN_PUSH ? manifest.source_revision : 0;
    g_era_host_peer_storage_runtime.expected_crc32           = manifest.image_crc32;
    g_era_host_peer_storage_runtime.image_size               = manifest.image_size;
    g_era_host_peer_storage_runtime.staged_bytes             = 0;
    g_era_host_peer_storage_runtime.state                    = token_kind == ERA_HOST_PEER_STORAGE_TOKEN_SUMMARY    ? ERA_HOST_PEER_STORAGE_RUNTIME_PEER_SYNC_STATUS :
                                                               token_kind == ERA_HOST_PEER_STORAGE_TOKEN_CONFLICT   ? ERA_HOST_PEER_STORAGE_RUNTIME_PEER_CONFLICT_STATUS :
                                                               token_kind == ERA_HOST_PEER_STORAGE_TOKEN_PUSH       ? ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_OPEN :
                                                                                                                      ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PROBE;
    g_era_host_peer_storage_runtime.role                     = ERA_HOST_PEER_STORAGE_ROLE_PEER;
    g_era_host_peer_storage_runtime.domain                   = summary ? ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE : (uint8_t)domain;
    g_era_host_peer_storage_runtime.next_chunk               = 0;
    g_era_host_peer_storage_runtime.retry_count              = 0;
    g_era_host_peer_storage_runtime.last_status              = ERA_SPLIT_EEPROM_SYNC_STATUS_MATCH;
    g_era_host_peer_storage_runtime.flags                    = 0;
    /* Decided content movement holds the indicator through this episode's
     * pre-transfer exchanges: a push or a conflict exchange is one by its
     * cell, a probe is one exactly when the peer declared its domain
     * changed (the pull-expected subset — peer_changed_mask is not consumed
     * at grant, so the test still reads at start). A verify probe and the
     * whole-family summary set nothing: the audit sweep stays dark, and the
     * summary episode is already covered by SUMMARY_PENDING, which clears
     * at its response rather than at its grant. */
    if (token_kind == ERA_HOST_PEER_STORAGE_TOKEN_PUSH || token_kind == ERA_HOST_PEER_STORAGE_TOKEN_CONFLICT ||
        (token_kind == ERA_HOST_PEER_STORAGE_TOKEN_PROBE && !summary &&
         (g_era_host_peer_storage_relation.peer_changed_mask & (uint8_t)era_host_peer_storage_domain_mask(domain)) != 0)) {
        g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_CONTENT_EXPECTED;
    }
    g_era_host_peer_storage_relation.idle_due                   = 0;
    g_era_host_peer_storage_relation.active_due                 = 1;
    if (!summary) {
        uint8_t started_bit = (uint8_t)era_host_peer_storage_domain_mask(domain);
        g_era_host_peer_storage_relation.probe_pending_mask &= (uint8_t)~started_bit;
        g_era_host_peer_storage_relation.push_pending_mask &= (uint8_t)~started_bit;
        g_era_host_peer_storage_relation.conflict_pending_mask &= (uint8_t)~started_bit;
    }
    g_era_host_peer_storage_diagnostics.open_count++;
    return true;
}

static void era_host_peer_storage_peer_task(const era_host_peer_storage_runtime_context_t *context) {
    if (g_era_host_peer_storage_runtime.role != ERA_HOST_PEER_STORAGE_ROLE_PEER) {
        uint16_t transaction_generation = g_era_host_peer_storage_runtime.transaction_generation;
        uint16_t request_generation = g_era_host_peer_storage_runtime.request_generation;
        memset(&g_era_host_peer_storage_runtime, 0, sizeof(g_era_host_peer_storage_runtime));
        g_era_host_peer_storage_runtime.transaction_generation = transaction_generation;
        g_era_host_peer_storage_runtime.request_generation     = request_generation;
        g_era_host_peer_storage_runtime.role   = ERA_HOST_PEER_STORAGE_ROLE_PEER;
        g_era_host_peer_storage_runtime.state  = ERA_HOST_PEER_STORAGE_RUNTIME_IDLE;
        g_era_host_peer_storage_runtime.domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    }

    /* Relation (re)establishment detection: episode resets preserve the
     * relation/policy generations, and every owner-epoch rotation also
     * rotates the relation, so a generation difference while IDLE is a new
     * confirmed relation and starts the mandatory audit sweep. */
    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_IDLE &&
        (g_era_host_peer_storage_runtime.relation_generation != context->relation_generation ||
         g_era_host_peer_storage_runtime.policy_generation != context->policy_generation)) {
        if (era_split_link_runtime_settled()) {
            g_era_host_peer_storage_runtime.relation_generation = context->relation_generation;
            g_era_host_peer_storage_runtime.policy_generation   = context->policy_generation;
            era_host_peer_storage_begin_relation_audit(context->now_ms);
        }
    }

    /* No responder-changed branch here since Slice 11.7, and since D2 there is
     * nothing to translate at all: the hint arms a summary through
     * `era_host_peer_storage_note_host_news()` and the summary is what
     * decides directions, on the same path a local settled capture uses. */

    bool peer_identity_changed = g_era_host_peer_storage_relation.active_due &&
                                 (context->policy_generation != g_era_host_peer_storage_runtime.policy_generation ||
                                  context->peer_usb_epoch != g_era_host_peer_storage_runtime.peer_usb_epoch ||
                                  context->peer_host_open_generation != g_era_host_peer_storage_runtime.peer_host_open_generation ||
                                  context->peer_host_close_generation != g_era_host_peer_storage_runtime.peer_host_close_generation ||
                                  (g_era_host_peer_storage_runtime.state != ERA_HOST_PEER_STORAGE_RUNTIME_PEER_REVALIDATE &&
                                   (context->owner_epoch != g_era_host_peer_storage_runtime.owner_epoch ||
                                    context->relation_generation != g_era_host_peer_storage_runtime.relation_generation)));
    if (peer_identity_changed) {
        g_era_host_peer_storage_diagnostics.stale_count++;
        if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_APPLY_WRITE) {
            /* Deferred abort: the sliced write of the validated image runs
             * through before the episode resets (no torn durable state). */
            era_host_peer_storage_apply_write_latch_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_STALE);
        } else {
            era_host_peer_storage_reset_peer_episode(context->now_ms, true, true);
        }
    }

    if ((g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TARGET_DIRTY) != 0) {
        if (era_split_communication_core_storage_initiator_result_ready()) {
            const era_split_communication_core_storage_initiator_result_t *discarded;
            if (era_split_communication_core_storage_acquire_initiator_result(&discarded)) {
                uint16_t request_generation = discarded->request_generation;
                if (!era_split_communication_core_storage_release_initiator_result(request_generation)) {
                    era_split_communication_core_storage_cancel_initiator_result(request_generation);
                }
                g_era_host_peer_storage_runtime.flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING;
                g_era_host_peer_storage_diagnostics.stale_count++;
            }
        }
        if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_APPLY_WRITE) {
            g_era_host_peer_storage_runtime.flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TARGET_DIRTY;
            era_host_peer_storage_apply_write_latch_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED);
        } else {
            if ((g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING) != 0) {
                return;
            }
            g_era_host_peer_storage_runtime.flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TARGET_DIRTY;
            era_host_peer_storage_peer_begin_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED, context->now_ms);
        }
    }

    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_APPLY_WRITE) {
        /* **No episode deadline here** (Slice 11.7). There was one, and it had
         * no case it could ever be right in.
         *
         * It could not prevent: the write-through rule outranks it, so it
         * latched an abort and the sliced write finished anyway. It could not
         * detect: `eeprom_update_block()` is synchronous on core0, so a wedged
         * write wedges the core that runs this check and the check never
         * executes. What was left was the one case where firing is wrong -- an
         * apply that is slow but progressing -- and there it converted a
         * successful convergence into a reported abort, costing a re-probe, a
         * summary re-arm and an indicator flash for nothing.
         *
         * What bounds this phase instead is its own shape:
         * `apply_write_slice()` advances `apply_offset` unconditionally, so the
         * apply is strictly monotonic and terminates in
         * ceil(image_size / ERA_HOST_PEER_STORAGE_APPLY_SLICE_BYTES) ticks. The
         * peer is held across it by core1's liveness beat, which is what made
         * a long apply survivable in the first place. Measured 2026-08-02: a
         * 16.2 KiB macro domain applied in 2095 ms against a 5000 ms budget it
         * was also sharing with the transfer that preceded it. */
        if (g_era_host_peer_storage_runtime.apply_offset < g_era_host_peer_storage_runtime.image_size) {
            era_host_peer_storage_apply_write_slice();
            if (g_era_host_peer_storage_runtime.apply_offset < g_era_host_peer_storage_runtime.image_size) {
                return;
            }
        }
        (void)era_host_peer_storage_apply_write_finish(context);
        return;
    }

    if (era_split_communication_core_storage_initiator_result_ready()) {
        era_host_peer_storage_process_peer_result(context);
    }
    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_IDLE) {
        (void)era_host_peer_storage_start_peer_episode(context);
    }
    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_IDLE) {
        return;
    }

    era_host_peer_storage_note_episode_phase(context->now_ms);
    if (timer_expired32(context->now_ms, g_era_host_peer_storage_runtime.episode_deadline_ms)) {
        if ((g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING) != 0) {
            if (!era_split_transport_scheduler_rotate_storage_relation()) {
                g_era_host_peer_storage_diagnostics.quiesce_fail_count++;
                g_era_host_peer_storage_runtime.episode_deadline_ms = context->now_ms + ERA_HOST_PEER_STORAGE_RETRY_MS;
                return;
            }
            g_era_host_peer_storage_diagnostics.timeout_count++;
            g_era_host_peer_storage_runtime.flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING;
            era_host_peer_storage_reset_peer_episode(context->now_ms, true, true);
            return;
        }
        g_era_host_peer_storage_diagnostics.timeout_count++;
        if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_ABORT) {
            era_host_peer_storage_reset_peer_episode(context->now_ms, true, true);
            return;
        }
        era_host_peer_storage_peer_begin_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_TIMEOUT, context->now_ms);
    }

    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_REVALIDATE) {
        if (g_era_host_peer_storage_runtime.retry_deadline_ms != 0 &&
            !timer_expired32(context->now_ms, g_era_host_peer_storage_runtime.retry_deadline_ms)) {
            return;
        }
        bool same_peer = context->peer_usb_epoch == g_era_host_peer_storage_runtime.peer_usb_epoch &&
                         context->peer_host_open_generation == g_era_host_peer_storage_runtime.peer_host_open_generation &&
                         context->peer_host_close_generation == g_era_host_peer_storage_runtime.peer_host_close_generation;
        if (!same_peer || context->policy_generation != g_era_host_peer_storage_runtime.policy_generation) {
            era_host_peer_storage_reset_peer_episode(context->now_ms, true, true);
            return;
        }
        if (!context->status_revalidation_due && !context->general_initiator_pending) {
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
            era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_REVALIDATED, 0);
#endif
            g_era_host_peer_storage_runtime.owner_epoch         = context->owner_epoch;
            g_era_host_peer_storage_runtime.relation_generation = context->relation_generation;
            g_era_host_peer_storage_runtime.state               = ERA_HOST_PEER_STORAGE_RUNTIME_PEER_COMPLETE;
            g_era_host_peer_storage_runtime.retry_deadline_ms   = context->now_ms;
        } else {
            g_era_host_peer_storage_runtime.retry_deadline_ms = context->now_ms + ERA_HOST_PEER_STORAGE_RETRY_MS;
        }
    }

    if ((g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING) == 0 &&
        g_era_host_peer_storage_runtime.state != ERA_HOST_PEER_STORAGE_RUNTIME_PEER_REVALIDATE &&
        (g_era_host_peer_storage_runtime.retry_deadline_ms == 0 || timer_expired32(context->now_ms, g_era_host_peer_storage_runtime.retry_deadline_ms))) {
        (void)era_host_peer_storage_submit_peer_request(context);
    }
}

static bool era_host_peer_storage_host_snapshot_image_valid(void) {
    uint8_t domain = g_era_host_peer_storage_runtime.domain;
    return domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT && g_era_host_peer_storage_local.image_valid &&
           !g_era_host_peer_storage_local.image_stale && g_era_host_peer_storage_local.image_domain == domain &&
           g_era_host_peer_storage_manifest[domain].source_revision != 0;
}

static void era_host_peer_storage_process_host_result(const era_host_peer_storage_runtime_context_t *context) {
    era_split_communication_core_storage_responder_result_t result;
    if (!era_split_communication_core_storage_drain_responder_result(&result)) {
        return;
    }
    if (context == NULL || result.owner_epoch != context->owner_epoch || result.relation_generation != context->relation_generation ||
        result.snapshot_generation != g_era_host_peer_storage_runtime.responder_snapshot_generation ||
        result.policy_generation != context->policy_generation) {
        g_era_host_peer_storage_diagnostics.stale_count++;
        return;
    }
    if (!result.response_sent || result.result != ERA_SPLIT_TRANSACTION_RESULT_OK || result.failure != ERA_SPLIT_TRANSACTION_FAILURE_NONE) {
        return;
    }
    g_era_host_peer_storage_runtime.last_status = result.status;

    /* **No DUAL-HOST bulk retirement here** (D1). Serving the whole-family
     * summary used to clear this half's entire `settled_news`, on the
     * reasoning that the summary hands the initiator per-domain recency truth
     * that supersedes a one-bit level -- which was the honest description of a
     * carrier that could not express its own fall, and stopped being one when
     * the mask became per-domain in both relations at Slice 11.7.
     *
     * It was deleted because it was harmful, not because it was dead. The mask
     * named domains this half had settled and not yet converged, and a summary
     * exchange converges nothing: clearing it erased claims for domains the
     * round had not touched. A truthful carrier then transmitted that lie
     * faithfully -- the fall reached the initiator, which dropped the domains
     * from its in-hand set and never heard about them again -- so the very
     * change that made the carrier work is what made this unsafe to keep. It
     * was also the accomplice that froze the initiator's cache at the boot
     * conservative 0x7F, by making the advertised value oscillate against a
     * receiver that could only latch it.
     *
     * **D2 then deleted every noun that paragraph is built from.** There is no
     * mask, no in-hand set and no boot conservative 0x7F, the section carries a
     * forward-only news value, and nothing retires that value in either role.
     * So the rule this deletion unified the two relations on -- a bit retires
     * at its own domain's convergence close and nowhere else (Slice 11.6, one
     * mechanism per boundary) -- has no bit left to govern, and the round-end
     * re-read that used to cover a domain the peer still claimed retired with
     * the case it covered. What survives both is the reason the absence is
     * still written here: serving a summary is not a convergence, so it may
     * not touch this half's claim, and a carrier that only steps forward is
     * how that stopped needing to be remembered at each site. */
    if (result.operation == ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP && result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY) {
        if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PINNED) {
            g_era_host_peer_storage_diagnostics.stale_count++;
            return;
        }
        if (result.domain >= ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) {
            g_era_host_peer_storage_diagnostics.domain_reject_count++;
            return;
        }
        g_era_host_peer_storage_relation.active_due = 0;
        uint32_t domain_mask = era_host_peer_storage_domain_mask((era_split_eeprom_sync_domain_t)result.domain);
        bool source_ready = (g_era_host_peer_storage_local.dirty_domain_mask & domain_mask) == 0;
        if (source_ready &&
            (!g_era_host_peer_storage_local.image_valid || g_era_host_peer_storage_local.image_stale ||
             g_era_host_peer_storage_local.image_domain != result.domain ||
             g_era_host_peer_storage_manifest[result.domain].source_revision == 0)) {
            source_ready = era_host_peer_storage_capture_domain((era_split_eeprom_sync_domain_t)result.domain);
        }
        g_era_host_peer_storage_runtime.transaction_generation = result.transaction_generation;
        g_era_host_peer_storage_runtime.domain                 = result.domain;
        g_era_host_peer_storage_runtime.image_size             = g_era_host_peer_storage_domains[result.domain].size;
        g_era_host_peer_storage_runtime.source_revision        = source_ready ? g_era_host_peer_storage_manifest[result.domain].source_revision : 0;
        g_era_host_peer_storage_runtime.expected_crc32         = source_ready ? g_era_host_peer_storage_manifest[result.domain].image_crc32 : 0;
        g_era_host_peer_storage_runtime.state                  = ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PINNED;
        g_era_host_peer_storage_runtime.pending_operation      = ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ;
        g_era_host_peer_storage_runtime.next_chunk             = 0;
        g_era_host_peer_storage_runtime.episode_deadline_ms    = context->now_ms + ERA_HOST_PEER_STORAGE_EPISODE_MS;
        /* Pinning alone must not close normal HOST matrix response admission. */
        g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
        g_era_host_peer_storage_relation.active_due               = 1;
        g_era_host_peer_storage_diagnostics.open_count++;
        return;
    }

    if (result.operation == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_RSP && result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY) {
        /* The push open's one-shot pin handoff, mirroring the probe's: this
         * half becomes the apply target. The capture pins its own current
         * content so the open retry can short-circuit MATCH; the runtime
         * keeps the initiator's push revision and the episode's full-image
         * CRC (the durable validation target), while the manifest keeps
         * this half's own content CRC for the open compare. A dirty domain
         * pins invalid exactly like the probe path, and the retried open
         * then answers SOURCE_CHANGED. */
        if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PINNED ||
            era_host_peer_storage_state_is_host_push(g_era_host_peer_storage_runtime.state)) {
            g_era_host_peer_storage_diagnostics.stale_count++;
            return;
        }
        if (result.domain >= ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) {
            g_era_host_peer_storage_diagnostics.domain_reject_count++;
            return;
        }
        if (result.request_source_revision == 0) {
            g_era_host_peer_storage_diagnostics.stale_count++;
            return;
        }
        g_era_host_peer_storage_relation.active_due = 0;
        uint32_t domain_mask = era_host_peer_storage_domain_mask((era_split_eeprom_sync_domain_t)result.domain);
        bool source_ready = (g_era_host_peer_storage_local.dirty_domain_mask & domain_mask) == 0;
        if (source_ready &&
            (!g_era_host_peer_storage_local.image_valid || g_era_host_peer_storage_local.image_stale ||
             g_era_host_peer_storage_local.image_domain != result.domain ||
             g_era_host_peer_storage_manifest[result.domain].source_revision == 0)) {
            source_ready = era_host_peer_storage_capture_domain((era_split_eeprom_sync_domain_t)result.domain);
        }
        g_era_host_peer_storage_runtime.transaction_generation = result.transaction_generation;
        g_era_host_peer_storage_runtime.domain                 = result.domain;
        g_era_host_peer_storage_runtime.image_size             = g_era_host_peer_storage_domains[result.domain].size;
        g_era_host_peer_storage_runtime.source_revision        = source_ready ? result.request_source_revision : 0;
        g_era_host_peer_storage_runtime.expected_crc32         = source_ready ? result.request_image_crc32 : 0;
        g_era_host_peer_storage_runtime.state                  = ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_OPEN;
        g_era_host_peer_storage_runtime.pending_operation      = ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ;
        g_era_host_peer_storage_runtime.next_chunk             = 0;
        g_era_host_peer_storage_runtime.apply_offset           = 0;
        g_era_host_peer_storage_runtime.apply_abort_status     = 0;
        g_era_host_peer_storage_runtime.episode_deadline_ms    = context->now_ms + ERA_HOST_PEER_STORAGE_EPISODE_MS;
        g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
        g_era_host_peer_storage_relation.active_due               = 1;
        g_era_host_peer_storage_diagnostics.open_count++;
        return;
    }

    if (result.operation == ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_RSP) {
        /* A sync-status answer binds to no episode on this half, by design:
         * the responder answers it from the published snapshot's recency
         * seat inside an admitted response slot and opens nothing, which is
         * why a converged relation reads `open=7` here against the
         * initiator's 8. It therefore cannot carry the transaction
         * generation and domain the gate below demands, and there is nothing
         * for core0 to do with the drained result — the operation switch
         * ignores it too.
         *
         * Dropping it is correct. Counting it as `stale` was not: it made a
         * healthy pair report one failure per relation open, and seven more
         * whenever conservative degradation put every domain in the conflict
         * cell. `stale` is one of the counters the Active-Cable Common Gate
         * requires to stay zero, so a counter that rises on specified
         * traffic cannot also report a real staleness event. */
        return;
    }

    if (result.transaction_generation != g_era_host_peer_storage_runtime.transaction_generation ||
        result.domain != g_era_host_peer_storage_runtime.domain) {
        g_era_host_peer_storage_diagnostics.stale_count++;
        return;
    }
    switch ((era_split_eeprom_sync_op_t)result.operation) {
        case ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP:
            g_era_host_peer_storage_diagnostics.proof_count++;
            g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
            if (g_era_host_peer_storage_runtime.state != ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PINNED) {
                g_era_host_peer_storage_runtime.state             = ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PINNED;
                g_era_host_peer_storage_runtime.episode_deadline_ms = context->now_ms + ERA_HOST_PEER_STORAGE_EPISODE_MS;
                g_era_host_peer_storage_relation.active_due           = 1;
                g_era_host_peer_storage_diagnostics.open_count++;
            }
            if (result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER) {
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
                era_host_peer_storage_cause_timeline_begin(ERA_HOST_PEER_STORAGE_ROLE_HOST,
                                                           g_era_host_peer_storage_runtime.domain,
                                                           g_era_host_peer_storage_runtime.transaction_generation);
#endif
                g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE |
                                                         ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TRANSFER_ACTIVE;
                g_era_host_peer_storage_runtime.pending_operation = ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ;
                g_era_host_peer_storage_runtime.next_chunk        = 0;
                g_era_host_peer_storage_diagnostics.transfer_count++;
            } else if (result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_MATCH) {
                g_era_host_peer_storage_diagnostics.match_count++;
                /* The pinned source CRC is what the responder proved equal. */
                era_host_peer_storage_note_domain_converged((era_split_eeprom_sync_domain_t)g_era_host_peer_storage_runtime.domain,
                                                            g_era_host_peer_storage_runtime.expected_crc32);
                era_host_peer_storage_host_close(context->now_ms, false, false);
            } else {
                era_host_peer_storage_note_reject_status(result.status);
                era_host_peer_storage_host_close(context->now_ms, true,
                                                 result.status != ERA_SPLIT_EEPROM_SYNC_STATUS_POLICY_CLOSED &&
                                                     result.status != ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED);
            }
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_RSP:
            if (result.chunk_id != g_era_host_peer_storage_runtime.next_chunk) {
                if (!result.replayed) {
                    g_era_host_peer_storage_diagnostics.duplicate_count++;
                }
                break;
            }
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
            era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_CHUNK_RESULT, result.chunk_id);
#endif
            g_era_host_peer_storage_runtime.next_chunk++;
            g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
            g_era_host_peer_storage_diagnostics.chunk_count++;
            if (era_host_peer_storage_chunk_length(g_era_host_peer_storage_runtime.image_size,
                                                   g_era_host_peer_storage_runtime.next_chunk) == 0) {
                g_era_host_peer_storage_runtime.pending_operation = ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ;
            }
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_APPLY_RSP:
            if (result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY) {
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
                era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_APPLY_READY, result.status);
#endif
                /* Answering `APPLY_RSP` is the moment the initiator disappears
                 * into its durable write, and from here this half waits for a
                 * `COMPLETE_REQ` with no wire traffic of its own to mark time
                 * against. The pinned operation moving is what the phase
                 * re-arm reads, since this half's `state` does not move here.
                 *
                 * It is also this role's transfer-verified boundary (R4): the
                 * APPLY_REQ this answered exists only because the initiator
                 * validated the staged image, so the lock this half held for
                 * the chunk stream has nothing left to protect. Matrix
                 * admission and the response plan reopen here, which is what
                 * lets the PEER's keys cross while its own apply runs. */
                g_era_host_peer_storage_runtime.pending_operation = ERA_SPLIT_EEPROM_SYNC_OP_COMPLETE_REQ;
                g_era_host_peer_storage_runtime.flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE;
                g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
            } else if (result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED) {
                era_host_peer_storage_note_reject_status(result.status);
                era_host_peer_storage_host_close(context->now_ms, true, false);
            } else {
                era_host_peer_storage_host_close(context->now_ms, true, true);
            }
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_CLOSE_RSP:
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
            era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_COMPLETE_RESULT, result.status);
#endif
            if (result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_COMPLETE) {
                g_era_host_peer_storage_diagnostics.complete_count++;
                /* The PEER durably applied this pinned source; it is the new
                 * agreement on this half too. */
                era_host_peer_storage_note_domain_converged((era_split_eeprom_sync_domain_t)g_era_host_peer_storage_runtime.domain,
                                                            g_era_host_peer_storage_runtime.expected_crc32);
                era_host_peer_storage_host_close(context->now_ms, false, false);
            } else {
                era_host_peer_storage_host_close(context->now_ms, true, true);
            }
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_PUSH_RSP:
            switch ((era_split_eeprom_sync_status_t)result.status) {
                case ERA_SPLIT_EEPROM_SYNC_STATUS_MATCH:
                    /* The open compare matched: converged with nothing to
                     * move, the pull-symmetric short circuit. */
                    if (g_era_host_peer_storage_runtime.state != ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_OPEN) {
                        g_era_host_peer_storage_diagnostics.stale_count++;
                        break;
                    }
                    g_era_host_peer_storage_diagnostics.match_count++;
                    era_host_peer_storage_note_domain_converged((era_split_eeprom_sync_domain_t)g_era_host_peer_storage_runtime.domain,
                                                                g_era_host_peer_storage_runtime.expected_crc32);
                    era_host_peer_storage_host_close(context->now_ms, false, false);
                    break;
                case ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER:
                    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_OPEN) {
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
                        era_host_peer_storage_cause_timeline_begin(ERA_HOST_PEER_STORAGE_ROLE_HOST,
                                                                   g_era_host_peer_storage_runtime.domain,
                                                                   g_era_host_peer_storage_runtime.transaction_generation);
#endif
                        g_era_host_peer_storage_runtime.state             = ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_STAGING;
                        g_era_host_peer_storage_runtime.pending_operation = ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ;
                        g_era_host_peer_storage_runtime.next_chunk        = 0;
                        g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE |
                                                                 ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TRANSFER_ACTIVE |
                                                                 ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
                        g_era_host_peer_storage_diagnostics.transfer_count++;
                    } else if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_STAGING &&
                               result.chunk_id == g_era_host_peer_storage_runtime.next_chunk) {
                        /* One staged chunk acknowledged: advance the expected
                         * chunk; past the last one, expect the apply trigger
                         * and swap the snapshot CRC seat to the episode CRC. */
                        g_era_host_peer_storage_runtime.next_chunk++;
                        g_era_host_peer_storage_diagnostics.chunk_count++;
                        if (era_host_peer_storage_chunk_length(g_era_host_peer_storage_runtime.image_size,
                                                               g_era_host_peer_storage_runtime.next_chunk) == 0) {
                            g_era_host_peer_storage_runtime.state             = ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WAIT;
                            g_era_host_peer_storage_runtime.pending_operation = ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ;
                        }
                        g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
                    } else if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_STAGING) {
                        if (!result.replayed) {
                            g_era_host_peer_storage_diagnostics.duplicate_count++;
                        }
                    }
                    break;
                case ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY:
                    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WAIT) {
                        /* Core1 accepted the apply trigger. The staged image is
                         * validated against the episode CRC here, before any
                         * durable write — the same authority order the pull
                         * apply uses. */
                        if (era_split_wire_crc32(g_era_host_peer_storage_image,
                                                        g_era_host_peer_storage_runtime.image_size) ==
                            g_era_host_peer_storage_runtime.expected_crc32) {
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
                            era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_APPLY_BEGIN, 0);
                            era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_EEPROM_BEGIN, 0);
#endif
                            g_era_host_peer_storage_runtime.state              = ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WRITE;
                            g_era_host_peer_storage_runtime.apply_offset       = 0;
                            g_era_host_peer_storage_runtime.apply_abort_status = 0;
                            /* The push responder's transfer-verified boundary
                             * (R4): the staged image just validated against
                             * the episode CRC, and the sliced write that
                             * follows is local. Normal admission and response
                             * content reopen for the width of the apply. */
                            g_era_host_peer_storage_runtime.flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE;
                            g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
                        } else {
                            g_era_host_peer_storage_diagnostics.integrity_reject_count++;
                            era_host_peer_storage_host_close(context->now_ms, true, true);
                        }
                    }
                    /* APPLY_READY answered to a complete poll mid-apply moves
                     * nothing on this side. */
                    break;
                case ERA_SPLIT_EEPROM_SYNC_STATUS_COMPLETE:
                    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_DURABLE) {
                        g_era_host_peer_storage_diagnostics.complete_count++;
                        /* The baseline was written at the durable
                         * declaration in `push_apply_finish`, which is where
                         * this half's content became the agreement, and
                         * nothing was retired there: the news value is
                         * forward-only and there is no settled bit anywhere
                         * in this engine to retire. This answer only tells
                         * the initiator. */
                        era_host_peer_storage_host_close(context->now_ms, false, false);
                        /* Durable-declare-then-rotate: the initiator has been
                         * told, so this half runs its identity anchor now. */
                        if (!era_split_transport_scheduler_rotate_storage_relation()) {
                            g_era_host_peer_storage_diagnostics.quiesce_fail_count++;
                        } else {
                            g_era_host_peer_storage_diagnostics.restart_count++;
                        }
                    }
                    break;
                default:
                    era_host_peer_storage_note_reject_status(result.status);
                    era_host_peer_storage_host_close(context->now_ms, true,
                                                     result.status != ERA_SPLIT_EEPROM_SYNC_STATUS_POLICY_CLOSED &&
                                                         result.status != ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED);
                    break;
            }
            break;
        case ERA_SPLIT_EEPROM_SYNC_OP_ABORT_RSP:
            era_host_peer_storage_note_reject_status(result.status);
            era_host_peer_storage_host_close(context->now_ms, true,
                                             result.status != ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED);
            break;
        default:
            break;
    }
}

static bool era_host_peer_storage_publish_host_snapshot(const era_host_peer_storage_runtime_context_t *context) {
    if (context == NULL || context->owner_epoch == 0 || context->relation_generation == 0) {
        return false;
    }
    if (g_era_host_peer_storage_runtime.responder_snapshot_generation != 0 &&
        (g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY) == 0) {
        return true;
    }
    uint8_t domain = g_era_host_peer_storage_runtime.domain;
    if (domain >= ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT) {
        domain = g_era_host_peer_storage_local.image_domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT ?
                     g_era_host_peer_storage_local.image_domain :
                     ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG;
    }
    uint16_t snapshot_generation = era_host_peer_storage_next_nonzero_u16(g_era_host_peer_storage_runtime.responder_snapshot_generation);
    if (snapshot_generation == 1 && g_era_host_peer_storage_runtime.responder_snapshot_generation == UINT16_MAX) {
        g_era_host_peer_storage_diagnostics.stale_count++;
        if (!era_split_transport_scheduler_rotate_storage_relation()) {
            g_era_host_peer_storage_diagnostics.quiesce_fail_count++;
        } else {
            memset(&g_era_host_peer_storage_runtime, 0, sizeof(g_era_host_peer_storage_runtime));
            g_era_host_peer_storage_runtime.domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
            g_era_host_peer_storage_relation.active_due = 0;
        }
        return false;
    }

    bool image_valid = era_host_peer_storage_host_snapshot_image_valid() && domain == g_era_host_peer_storage_runtime.domain;
    era_host_peer_storage_manifest_entry_t *manifest = &g_era_host_peer_storage_manifest[domain];
    uint16_t transaction_generation = g_era_host_peer_storage_runtime.transaction_generation;
    if (transaction_generation == 0) {
        transaction_generation = 1;
    }
    uint8_t expected_operation = g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PINNED ?
                                     g_era_host_peer_storage_runtime.pending_operation :
                                     ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ;
    era_split_communication_core_storage_responder_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.source_revision             = image_valid ? manifest->source_revision : 0;
    snapshot.image_crc32                 = image_valid ? manifest->image_crc32 : 0;
    snapshot.image_publication_seq       = g_era_host_peer_storage_local.image_publication_seq;
    snapshot.image_address               = (uint32_t)(uintptr_t)g_era_host_peer_storage_image;
    snapshot.image_publication_seq_address = (uint32_t)(uintptr_t)&g_era_host_peer_storage_local.image_publication_seq;
    snapshot.owner_epoch                 = context->owner_epoch;
    snapshot.relation_generation         = context->relation_generation;
    snapshot.snapshot_generation         = snapshot_generation;
    snapshot.policy_generation           = context->policy_generation;
    snapshot.transaction_generation      = transaction_generation;
    snapshot.image_size                  = g_era_host_peer_storage_domains[domain].size;
    snapshot.domain                      = domain;
    snapshot.schema                      = g_era_host_peer_storage_domains[domain].schema;
    snapshot.expected_operation          = expected_operation;
    snapshot.expected_chunk_id           = expected_operation == ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ ?
                                               g_era_host_peer_storage_runtime.next_chunk :
                                               0;
    snapshot.allowed                     = context->local_policy_requested ? 1 : 0;
    snapshot.valid                       = image_valid ? 1 : 0;
    snapshot.pinned                      = g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PINNED ? 1 : 0;
    if (era_host_peer_storage_state_is_host_push(g_era_host_peer_storage_runtime.state)) {
        /* Push pin seats: the wire identity is the initiator's push
         * revision; the CRC seat holds this half's own content CRC while
         * the open compare and staging run, then the episode's full-image
         * CRC once every chunk is in — which is what the apply and
         * complete phases check against. */
        uint8_t push_runtime_state = g_era_host_peer_storage_runtime.state;
        snapshot.source_revision    = g_era_host_peer_storage_runtime.source_revision;
        snapshot.image_crc32        = (push_runtime_state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_OPEN ||
                                       push_runtime_state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_STAGING) ?
                                          g_era_host_peer_storage_manifest[domain].image_crc32 :
                                          g_era_host_peer_storage_runtime.expected_crc32;
        snapshot.valid              = g_era_host_peer_storage_runtime.source_revision != 0 ? 1 : 0;
        snapshot.pinned             = 1;
        snapshot.expected_operation = g_era_host_peer_storage_runtime.pending_operation;
        snapshot.expected_chunk_id  = g_era_host_peer_storage_runtime.pending_operation == ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ ?
                                          g_era_host_peer_storage_runtime.next_chunk :
                                          0;
        snapshot.push_state         = push_runtime_state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WRITE ?
                                          ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUSH_STATE_APPLYING :
                                      push_runtime_state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_DURABLE ?
                                          ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUSH_STATE_DURABLE :
                                          ERA_SPLIT_COMMUNICATION_CORE_STORAGE_PUSH_STATE_STAGING;
    }
    {
        /* The recency seat: the cold view core1 answers SYNC_STATUS and
         * per-conflict counter requests from. Read through the wear-level
         * cache at this publish boundary; core1 itself never reads EEPROM. */
        era_host_peer_storage_recency_snapshot_t recency;
        era_host_peer_storage_get_recency_snapshot(&recency);
        snapshot.recency_changed_mask   = (uint8_t)(recency.changed_mask & ERA_HOST_PEER_STORAGE_ALL_DOMAINS_MASK);
        snapshot.recency_baseline_valid = recency.baseline_record_valid;
        for (uint8_t recency_domain = 0; recency_domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT; recency_domain++) {
            snapshot.recency_counter[(uint16_t)recency_domain * 2U]      = (uint8_t)(recency.divergence_counter[recency_domain] & 0xFFU);
            snapshot.recency_counter[(uint16_t)recency_domain * 2U + 1U] = (uint8_t)(recency.divergence_counter[recency_domain] >> 8);
        }
    }
    if (!era_split_communication_core_storage_publish_responder_snapshot(&snapshot)) {
        return false;
    }
    g_era_host_peer_storage_runtime.owner_epoch                  = context->owner_epoch;
    g_era_host_peer_storage_runtime.relation_generation          = context->relation_generation;
    g_era_host_peer_storage_runtime.policy_generation            = context->policy_generation;
    g_era_host_peer_storage_runtime.responder_snapshot_generation = snapshot_generation;
    g_era_host_peer_storage_runtime.flags &= (uint8_t)~ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
    return true;
}

static void era_host_peer_storage_host_task(const era_host_peer_storage_runtime_context_t *context) {
    bool relation_changed = g_era_host_peer_storage_runtime.role != ERA_HOST_PEER_STORAGE_ROLE_HOST ||
                            g_era_host_peer_storage_runtime.owner_epoch != context->owner_epoch ||
                            g_era_host_peer_storage_runtime.relation_generation != context->relation_generation ||
                            g_era_host_peer_storage_runtime.policy_generation != context->policy_generation;
    if (relation_changed &&
        g_era_host_peer_storage_runtime.role == ERA_HOST_PEER_STORAGE_ROLE_HOST &&
        g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WRITE) {
        /* Deferred abort: the sliced write of the validated image runs
         * through before the episode resets (no torn durable state); the
         * relation facts re-anchor on the reset that follows the finish. */
        era_host_peer_storage_apply_write_latch_abort(ERA_SPLIT_EEPROM_SYNC_STATUS_STALE);
        relation_changed = false;
    }
    if (relation_changed) {
        bool was_active = g_era_host_peer_storage_relation.active_due != 0;
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
        if (was_active) {
            era_host_peer_storage_cause_timeline_note(ERA_HOST_PEER_STORAGE_CAUSE_EVENT_ABORT,
                                                      ERA_SPLIT_EEPROM_SYNC_STATUS_ROLE_CHANGED);
        }
#endif
        memset(&g_era_host_peer_storage_runtime, 0, sizeof(g_era_host_peer_storage_runtime));
        g_era_host_peer_storage_runtime.role              = ERA_HOST_PEER_STORAGE_ROLE_HOST;
        g_era_host_peer_storage_runtime.state             = ERA_HOST_PEER_STORAGE_RUNTIME_HOST_READY;
        g_era_host_peer_storage_runtime.domain            = g_era_host_peer_storage_local.image_domain;
        g_era_host_peer_storage_runtime.owner_epoch       = context->owner_epoch;
        g_era_host_peer_storage_runtime.relation_generation = context->relation_generation;
        g_era_host_peer_storage_runtime.policy_generation = context->policy_generation;
        g_era_host_peer_storage_runtime.flags             = ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY;
        g_era_host_peer_storage_relation.active_due          = 0;
        if (was_active) {
            g_era_host_peer_storage_diagnostics.abort_count++;
            era_split_transport_scheduler_force_storage_recovery(true);
        }
    }

    if (era_split_communication_core_storage_responder_result_ready()) {
        era_host_peer_storage_process_host_result(context);
    }
    if (g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WRITE) {
        /* One bounded slice per entry, pumped back-to-back by the
         * housekeeping gate's `apply_write_active()` term — the drain above
         * runs in every one of those same passes, so core1 keeps answering
         * the initiator's COMPLETE polls from the published snapshot for the
         * whole write, and the pump makes that window ~34 ms rather than the
         * ~3.0 s it was while only the pull role was pumped. */
        era_host_peer_storage_apply_write_slice();
        if (g_era_host_peer_storage_runtime.apply_offset >= g_era_host_peer_storage_runtime.image_size) {
            (void)era_host_peer_storage_push_apply_finish(context);
        }
    }
    /* The deadline skips `HOST_PUSH_APPLY_WRITE` entirely, for the reason
       written at the initiator's own apply: in that state it can neither
       prevent nor detect, and the only case it can fire in is one where firing
       is wrong. The state is bounded by its own monotonic slice cursor. */
    era_host_peer_storage_note_episode_phase(context->now_ms);
    if ((g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PINNED ||
         era_host_peer_storage_state_is_host_push(g_era_host_peer_storage_runtime.state)) &&
        g_era_host_peer_storage_runtime.state != ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WRITE &&
        timer_expired32(context->now_ms, g_era_host_peer_storage_runtime.episode_deadline_ms)) {
        g_era_host_peer_storage_diagnostics.timeout_count++;
        era_host_peer_storage_host_close(context->now_ms, true, true);
    }
    (void)era_host_peer_storage_publish_host_snapshot(context);
}

bool era_host_peer_storage_runtime_task(const era_host_peer_storage_runtime_context_t *context, bool *result_watch_active) {
    if (result_watch_active != NULL) {
        *result_watch_active = false;
    }
    if (!g_era_host_peer_storage_local.initialized || context == NULL) {
        g_era_host_peer_storage_relation.runtime_service_active = 0;
        return false;
    }
    if (g_era_host_peer_storage_local.revision_wrap_pending) {
        return false;
    }

    bool runtime_active = g_era_host_peer_storage_relation.runtime_service_active != 0;
    if (!runtime_active && !era_host_peer_storage_relation_serviced(context)) {
        return false;
    }

    /* **The DUAL-HOST policy-open re-arm retired at D2, and it retired because
     * the carrier started doing its job for free.** It re-armed the whole mask
     * on the local EEPROM-policy enable edge, because the advertisement is
     * gated on that policy: while the policy was closed the level read zero
     * whatever this half held, and an initiator edit sat terminally refused
     * with nothing left to re-trigger it. That was a genuine hole and this was
     * a correct patch for it — it also carried its own `ARB_FLAG_LOCAL_POLICY_
     * OPEN` edge detector, and a 2026-07-29 objection that had to be refuted
     * before it could land.
     *
     * The news value needs none of it. Settled captures step it whether the
     * policy is open or closed — the policy gates the *advertisement*, never
     * the local capture (`era_host_peer_storage_contract.md`) — so on the
     * enable edge the value this half has been carrying all along simply
     * differs from the zero the peer last saw, and the ordinary news test arms
     * the summary. The enable edge is news by construction rather than by a
     * mechanism that had to notice it. */

    uint8_t previous_state = g_era_host_peer_storage_runtime.state;
    if (g_era_host_peer_storage_runtime.role == ERA_HOST_PEER_STORAGE_ROLE_PEER &&
        g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_APPLY_WRITE &&
        !era_host_peer_storage_context_peer(context)) {
        era_host_peer_storage_apply_write_run_to_completion(context);
    }
    if (g_era_host_peer_storage_runtime.role == ERA_HOST_PEER_STORAGE_ROLE_HOST &&
        g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WRITE &&
        !era_host_peer_storage_context_host(context)) {
        era_host_peer_storage_apply_write_run_to_completion(context);
    }
    if (era_host_peer_storage_context_peer(context)) {
        g_era_host_peer_storage_relation.runtime_service_active = 1;
        /* The indicator gate is serviceability AND this half's own policy,
           cached here because the lamp reads at render cadence where no
           context exists. Policy-off keeps the lamp dark on this half: its
           content does not sync, so there is no pair process to announce. */
        g_era_host_peer_storage_relation.indicator_bits =
            (uint8_t)((g_era_host_peer_storage_relation.indicator_bits & (uint8_t)~ERA_HOST_PEER_STORAGE_INDICATOR_GATE) |
                      (context->local_policy_requested ? ERA_HOST_PEER_STORAGE_INDICATOR_GATE : 0));
        era_host_peer_storage_peer_task(context);
    } else if (era_host_peer_storage_context_host(context)) {
        g_era_host_peer_storage_relation.runtime_service_active = 1;
        g_era_host_peer_storage_relation.indicator_bits =
            (uint8_t)((g_era_host_peer_storage_relation.indicator_bits & (uint8_t)~ERA_HOST_PEER_STORAGE_INDICATOR_GATE) |
                      (context->local_policy_requested ? ERA_HOST_PEER_STORAGE_INDICATOR_GATE : 0));
        /* Before the responder's own work, not after: this half selects no
           storage route, so anything the initiator drain owns is work it
           cannot drain. It runs every pass rather than on the transition
           because a settled capture keeps arming an idle token here, and it
           costs one early-out when there is nothing to release. */
        era_host_peer_storage_release_initiator_queues();
        era_host_peer_storage_host_task(context);
    } else {
        if (!runtime_active) {
            return false;
        }
        if (g_era_host_peer_storage_relation.active_due) {
            if (g_era_host_peer_storage_runtime.role == ERA_HOST_PEER_STORAGE_ROLE_PEER) {
                era_host_peer_storage_reset_peer_episode(context->now_ms, true, true);
            } else {
                g_era_host_peer_storage_diagnostics.abort_count++;
                era_split_transport_scheduler_force_storage_recovery(true);
            }
        }
        uint16_t transaction_generation = g_era_host_peer_storage_runtime.transaction_generation;
        uint16_t request_generation = g_era_host_peer_storage_runtime.request_generation;
        memset(&g_era_host_peer_storage_runtime, 0, sizeof(g_era_host_peer_storage_runtime));
        g_era_host_peer_storage_runtime.transaction_generation = transaction_generation;
        g_era_host_peer_storage_runtime.request_generation     = request_generation;
        g_era_host_peer_storage_runtime.domain                 = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
        g_era_host_peer_storage_relation.active_due               = 0;
        g_era_host_peer_storage_relation.runtime_service_active    = 0;
        /* Leaving service is the one place the indicator's cold facts die:
           the peer that advertised the mirror is gone or re-roled, and the
           gate's serviceability term is false. A cable pull therefore drops
           both lamps rather than freezing one red; a mid-process identity
           rotation never reaches here — the rotation stays inside the
           serviced world — which is what keeps the lamp continuous across
           the durable-apply reopen. */
        g_era_host_peer_storage_relation.indicator_bits           = 0;
    }
    if (result_watch_active != NULL) {
        *result_watch_active = g_era_host_peer_storage_relation.runtime_service_active != 0 &&
                               !g_era_host_peer_storage_local.revision_wrap_pending;
    }
    return previous_state != g_era_host_peer_storage_runtime.state;
}

bool era_host_peer_storage_route_exclusive(void) {
    return (g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE) != 0;
}

/* "Data is moving": the episode span whose boundaries are two-sided
 * exchanges. It is a separate fact from route exclusivity, which answers
 * "who owns the wire" — an abort raises exclusivity to get its ABORT_REQ out
 * and moves nothing, and reading that as a transfer made a retry loop look
 * identical to one (device-shown 2026-07-29 as a 1 Hz flash against a
 * policy-closed responder). The TRANSFER flag alone is not the span either:
 * it clears at each role's transfer-verified boundary, and the post-move
 * states carry the rest — apply, revalidation, completion — including the
 * pull responder waiting through the initiator's durable apply inside
 * HOST_PINNED with only its pinned operation advancing. Since the 2026-08-14
 * redesign this is one term of era_host_peer_storage_indicator_pending()
 * rather than the whole sync arm; the cell queues and the changed shadow
 * carry the lamp between episodes, which is the job the retired 1500 ms
 * bridge used to do with a clock. */
static bool era_host_peer_storage_moving_span(void) {
    if ((g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TRANSFER_ACTIVE) != 0) {
        return true;
    }
    switch ((era_host_peer_storage_runtime_state_t)g_era_host_peer_storage_runtime.state) {
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_APPLY:
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_APPLY_WRITE:
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_REVALIDATE:
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_COMPLETE:
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_APPLY:
        case ERA_HOST_PEER_STORAGE_RUNTIME_PEER_PUSH_COMPLETE:
            /* Initiator, post-move phases of an episode that transferred. */
            return true;
        case ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_STAGING:
        case ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WAIT:
        case ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WRITE:
        case ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_DURABLE:
            /* Push responder, staging through durable declaration. */
            return true;
        case ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PINNED:
            /* Pull responder across the initiator's apply: pinned, with the
               served operation advanced past the data phase. */
            return g_era_host_peer_storage_runtime.pending_operation == ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ ||
                   g_era_host_peer_storage_runtime.pending_operation == ERA_SPLIT_EEPROM_SYNC_OP_COMPLETE_REQ;
        default:
            return false;
    }
}

/* The lamp's local arm and the advertised fact, one value with one name:
 * each half's own unfinished pair work, minus only the mirror (a half never
 * echoes the peer's claim back at it). The initiator's standing plan
 * carries it as the STORAGE_PENDING push section, the responder's standing
 * answer as STORAGE_NEWS bit7 — and with the lamp reading mirror OR this,
 * era_host_peer_storage_indicator_pending() is literally the design
 * sentence: own advertised ∪ peer's advertised. One expression, O(1)
 * aligned RAM reads, callable from the render path: no EEPROM, no CRC, no
 * context.
 *
 * The terms, in falling order of earliness:
 * - dirty content inside its quiet interval (the edited half's save phase —
 *   crossed to the peer too, so an initiator-edited operation lights both
 *   halves together from the first write);
 * - settled content departed from its baseline (the changed shadow — the
 *   term that survives the relation identity rotation while the queues drop
 *   and re-derive, and the term whose absence the retired bridge padded
 *   over with time);
 * - decided content-moving cells (push, conflict, and the pull-expected
 *   subset of the probe queue — peer_changed is not consumed at grant, so
 *   the intersection empties exactly as episodes start);
 * - an armed in-session summary (a settle or news armed it, so movement is
 *   plausibly coming; the relation-open verify-all round deliberately does
 *   not count, which keeps the audit sweep dark);
 * - a decided episode's own span (CONTENT_EXPECTED across its pre-transfer
 *   exchanges, then the moving span through close).
 *
 * All of it behind the cold-cached gate: serviceability and this half's own
 * sync policy. A standalone half and a policy-off half stay dark whatever
 * they hold.
 *
 * The dirty term was excluded at first, preserving the old "edited half
 * alone lights first during its save" doctrine, and the first device
 * sitting (2026-08-14) refuted that choice with the owner's own UX ruling:
 * a VIA save is a multi-second write stream, so joining at settle put the
 * peer seconds behind the edited half's rise and dipped it dark between
 * one domain's convergence and the next one's settle. Entry and exit are
 * both the pair's process; both synchronize. The response direction ran
 * settle-bound for one more sitting — the response mask had no free
 * marker — until the fourth sitting measured the bound at 4.1 s on an
 * R-side load and the owner ruled it closed; the news byte's reserved bit7
 * was the free carrier. */
static bool era_host_peer_storage_live_pair_work(void) {
    if (g_era_host_peer_storage_local.dirty_domain_mask != 0) {
        return true;
    }
    if ((uint8_t)(g_era_host_peer_storage_relation.push_pending_mask |
                  g_era_host_peer_storage_relation.conflict_pending_mask |
                  (uint8_t)(g_era_host_peer_storage_relation.probe_pending_mask &
                            g_era_host_peer_storage_relation.peer_changed_mask)) != 0) {
        return true;
    }
    if ((g_era_host_peer_storage_relation.arbitration_flags & ERA_HOST_PEER_STORAGE_ARB_FLAG_SUMMARY_PENDING) != 0 &&
        (g_era_host_peer_storage_relation.arbitration_flags & ERA_HOST_PEER_STORAGE_ARB_FLAG_ROUND_VERIFY_ALL) == 0) {
        return true;
    }
    if (g_era_host_peer_storage_relation.active_due &&
        (g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_CONTENT_EXPECTED) != 0) {
        return true;
    }
    return era_host_peer_storage_moving_span();
}

bool era_host_peer_storage_advertised_pending(void) {
    if (!g_era_host_peer_storage_local.initialized ||
        (g_era_host_peer_storage_relation.indicator_bits & ERA_HOST_PEER_STORAGE_INDICATOR_GATE) == 0) {
        return false;
    }
    if (g_era_host_peer_storage_local.changed_domain_mask != 0) {
        return true;
    }
    return era_host_peer_storage_live_pair_work();
}

bool era_host_peer_storage_restart_should_wait(void) {
    if (!g_era_host_peer_storage_local.initialized ||
        (g_era_host_peer_storage_relation.indicator_bits & ERA_HOST_PEER_STORAGE_INDICATOR_GATE) == 0) {
        return false;
    }
    return era_host_peer_storage_live_pair_work();
}

bool era_host_peer_storage_indicator_pending(void) {
    if (!g_era_host_peer_storage_local.initialized) {
        return false;
    }
    /* The mirror rides outside the gate: it is the peer's claim under the
     * peer's own gate, retired by its carrier's zero crossing (the
     * STORAGE_PENDING push section or STORAGE_NEWS bit7, whichever direction
     * feeds this half) or by this half leaving service — never by this
     * half's policy. */
    if ((g_era_host_peer_storage_relation.indicator_bits & ERA_HOST_PEER_STORAGE_INDICATOR_PEER_PENDING) != 0) {
        return true;
    }
    return era_host_peer_storage_advertised_pending();
}

void era_host_peer_storage_note_peer_pending(bool pending) {
    if (!g_era_host_peer_storage_local.initialized) {
        return;
    }
    if (pending) {
        g_era_host_peer_storage_relation.indicator_bits |= ERA_HOST_PEER_STORAGE_INDICATOR_PEER_PENDING;
    } else {
        g_era_host_peer_storage_relation.indicator_bits &= (uint8_t)~ERA_HOST_PEER_STORAGE_INDICATOR_PEER_PENDING;
    }
}

uint8_t era_host_peer_storage_indicator_diag(void) {
    uint8_t bits = 0;
    if (era_host_peer_storage_advertised_pending()) {
        bits |= 0x01;
    }
    if ((g_era_host_peer_storage_relation.indicator_bits & ERA_HOST_PEER_STORAGE_INDICATOR_PEER_PENDING) != 0) {
        bits |= 0x02;
    }
    if ((g_era_host_peer_storage_relation.indicator_bits & ERA_HOST_PEER_STORAGE_INDICATOR_GATE) != 0) {
        bits |= 0x04;
    }
    return bits;
}

bool era_host_peer_storage_apply_write_active(void) {
    /* One question, asked about the activity rather than the role: is core0
       inside a sliced durable apply. Both states carry the identical walk —
       one bounded 32 B slice per entry, `apply_offset` advancing
       unconditionally, so `IMAGE_BYTES / APPLY_SLICE_BYTES` entries whatever
       the content — and the role term this predicate used to lead with was
       inert, because `PEER_APPLY_WRITE` occurs in no other role. What it
       excluded instead was the responder's `HOST_PUSH_APPLY_WRITE`, which
       therefore advanced only when some unrelated scheduler deadline happened
       to wake housekeeping. Measured cost of that omission before this
       change: the initiator's completion polls stood at 117-129 per
       initiator-edited operation against zero on the responder-edited
       direction, flat across a 443-block and an 8-block apply, and the walk
       they were counting ran ~3.0 s instead of the pumped ~34 ms. */
    return g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_PEER_APPLY_WRITE ||
           g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WRITE;
}

void era_host_peer_storage_get_diagnostics_snapshot(era_host_peer_storage_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }
    __DMB();
    *snapshot            = g_era_host_peer_storage_diagnostics;
    era_split_communication_core_storage_lane_diagnostics_t lane;
    era_split_communication_core_storage_get_lane_diagnostics(&lane);
    snapshot->responder_full_count = lane.responder_full_count;
    snapshot->duplicate_count += lane.replay_count;
    snapshot->state      = g_era_host_peer_storage_runtime.state;
    snapshot->role       = g_era_host_peer_storage_runtime.role;
    snapshot->domain     = g_era_host_peer_storage_runtime.domain;
    snapshot->status     = g_era_host_peer_storage_runtime.last_status;
    snapshot->active     = g_era_host_peer_storage_relation.active_due;
    snapshot->exclusive  = era_host_peer_storage_route_exclusive() ? 1 : 0;
    __DMB();
}
