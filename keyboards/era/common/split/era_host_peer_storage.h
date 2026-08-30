// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "matrix.h"

#include "era_split_eeprom_sync.h"

#define ERA_HOST_PEER_STORAGE_SCHEMA_V1 1U
#define ERA_HOST_PEER_STORAGE_IMAGE_BYTES 16384U

/* Schema-1 domain sizes, defined once because both cores need them. Core0
 * builds its descriptor table from the real EEPROM layout —
 * `sizeof(rgb_config_t)`, `VIA_EEPROM_LAYOUT_OPTIONS_SIZE`, and the rest —
 * which core1 must not include, since those headers are the live QMK/VIA
 * surface core1 is forbidden to touch. So each size here is a plain integer
 * or an expression over the geometry macros core1 already compiles against:
 * `matrix.h` is constants rather than live state, and every core1 TU has
 * carried it through `era_split_wire_protocol.h` since the wire layer began
 * deriving its own widths from it.
 *
 * These macros are the core-boundary binding. Core1's size table reads them
 * directly; core0 asserts each layout expression against the matching macro
 * (`era_host_peer_storage.c`), so a layout change fails the build at the
 * assert, the macro here is what gets corrected, and core1 follows without
 * anyone remembering it exists. Two tables that merely happened to agree
 * became one number with a compile-time proof on each side.
 *
 * The dynamic-keymap size is geometry-parameterised (owner decision
 * 2026-08-13): schema 1 is the formula, and the resolved number is the
 * board's. The identical-image rule is what makes per-board resolution
 * sound — both halves of a pair always run one image, so no wire peer can
 * disagree about a size, and the PROBE/PROOF identity check refuses a
 * mismatch before any byte moves. The layer count stays a plain integer
 * here, deliberately: `DYNAMIC_KEYMAP_LAYER_COUNT`'s defaulting chain lives
 * in the live-surface headers core1 must not include, so core0 binds this
 * literal to it with a `_Static_assert` instead
 * (`era_host_peer_storage.c`).
 *
 * The exact addresses and the portable-image membership rules stay
 * canonical in `era_host_peer_storage_contract.md`; only the sizes are
 * here, because only the sizes cross the core boundary. */
#define ERA_HOST_PEER_STORAGE_SCHEMA_DYNAMIC_KEYMAP_LAYERS 4U
#define ERA_HOST_PEER_STORAGE_DOMAIN_ERA_CONFIG_BYTES 176U
#define ERA_HOST_PEER_STORAGE_DOMAIN_DYNAMIC_KEYMAP_BYTES (ERA_HOST_PEER_STORAGE_SCHEMA_DYNAMIC_KEYMAP_LAYERS * MATRIX_ROWS * MATRIX_COLS * 2U)
#define ERA_HOST_PEER_STORAGE_DOMAIN_DYNAMIC_MACRO_BYTES ERA_HOST_PEER_STORAGE_IMAGE_BYTES
#define ERA_HOST_PEER_STORAGE_DOMAIN_QMK_RGB_MATRIX_BYTES 8U
#define ERA_HOST_PEER_STORAGE_DOMAIN_QMK_KEYMAP_CONFIG_BYTES 2U
#define ERA_HOST_PEER_STORAGE_DOMAIN_QMK_DEFAULT_LAYER_BYTES 1U
#define ERA_HOST_PEER_STORAGE_DOMAIN_VIA_LAYOUT_OPTIONS_BYTES 1U
#define ERA_HOST_PEER_STORAGE_MANIFEST_ENTRY_BYTES 16U
#define ERA_HOST_PEER_STORAGE_DIRTY_QUIET_MS 1000U
#define ERA_HOST_PEER_STORAGE_CHUNK_BYTES 252U
#define ERA_HOST_PEER_STORAGE_MAX_CHUNKS 66U
/* Raised 18432 -> 18436 at the Slice 10 relation-neutral split (four bytes
 * of struct padding, 69 -> 72 and 15 -> 16), 18436 -> 18444 for the push
 * lane's initiator request record (32 -> 40: the image and publication-seq
 * addresses the bulk chunk TX reads from), and 18444 -> 18448 for the
 * arbitration bookkeeping (the cell work-queue masks, the peer summary,
 * and the token kind: 16 -> 20), and 18448 -> 18452 for Slice 11.7's
 * advertised and re-arm hint masks (20 -> 24). The zero-headroom cap moves as
 * deliberate arithmetic rather than absorbing growth silently.
 *
 * **Then it shrank twice and the cap owed a move for both.** D2 took the
 * measured aggregate to 18448 -- the hint stopped naming domains, so the core0
 * state returned the four bytes Slice 11.7 bought
 * (`ERA_HOST_PEER_STORAGE_CORE0_STATE_BYTES` 148 -> 144, narrated below) -- and
 * the 2026-08-10 cleanup took it to 18444 when the write-only `subtype` byte
 * left the shared wire frame record, carrying four bytes of its `ALIGN4` with
 * it. That second step is profile-independent, because the `ALIGN4` term sits
 * outside every diagnostics gate.
 *
 * **It shrank a third time on 2026-08-11**, 18444 -> 18436, when the local
 * truth record's write-only `capture_count` and `image_schema` left the core0
 * state (144 -> 136); the visual snapshot's constant-true validity flag left
 * five records in the same change, and its own bytes fall outside this term.
 *
 * 18452 -> 18444 was that owed move, taken rather than left as eight bytes
 * of headroom no step spent. It restores the property the paragraph above
 * claims for this constant and had stopped having: the cap is the measured
 * aggregate, so the next growth fails a build instead of being absorbed.
 *
 * 18436 -> 18444 on 2026-08-14 for the indicator redesign's two facts: the
 * local truth record's RAM changed-vs-baseline mask (64 -> 68 with padding)
 * and the relation record's indicator bits — the peer's advertised pending
 * mirror and the cached serviceability gate — (20 -> 24 with padding),
 * CORE0_STATE_BYTES 136 -> 144 below. Then 18444 -> 18436 when the wire-reset
 * edge record lost its two `hbmiss` counters with the heartbeat lane, which
 * the wire-diagnostics equality in
 * `communication_core/era_split_communication_core_storage.c` took and this
 * constant did not.
 *
 * **Eight bytes of headroom is what that left, and headroom is the one thing
 * this constant may not carry.** For as long as the cap sat above the measured
 * aggregate, the next eight bytes of growth were absorbed instead of failing
 * the build — the `==`-by-construction design defeated by exactly the gap. The
 * cause variant's `<=` against `ERA_HOST_PEER_STORAGE_PROFILE_BUDGET_BYTES` is
 * an exact equality again at 18436 + 224 + 1328 = 19988, which is why that
 * variant is built rather than reasoned about.
 *
 * **18436 -> 18460 for retained Core1 failure detail.** The diagnostic record
 * and the paced printer's snapshot each grow by 12 bytes; release images
 * compile both copies out. The bytes retain the failed boundary after later
 * success overwrites `last`, so a device capture can distinguish expiry from
 * encode, wire and contract failures.
 *
 * **18460 -> 18536 for the queue-residence discriminator.** The diagnostic
 * request gains its actual publication timestamp (4 bytes), and the retained
 * failure context plus the paced printer's snapshot each gain 36 bytes. The
 * context reuses the transaction timing already present in diagnostic builds;
 * release images keep the 40-byte request and compile all 76 bytes out.
 *
 * **18536 -> 18524 at the ERA NVM cutover.** The old flash-write edge record
 * was four bytes in the scheduler and four in its published diagnostics copy.
 * A/B replacement has no recursive sliced-erase interlock for that record to
 * measure, so both copies are gone. Two one-byte deferred-abort/internal-read
 * fields and their alignment also retire with the raw/public facade, accounting
 * for the remaining four bytes beyond the slice-swap scratch/cursor itself. */
#define ERA_HOST_PEER_STORAGE_STATIC_BUDGET_BYTES 18524U
#define ERA_HOST_PEER_STORAGE_EPISODE_MS 5000U
#define ERA_HOST_PEER_STORAGE_RETRY_MS 25U
/* The consecutive-failure abort bound is derived, not chosen. The widest
 * window in which a healthy peer legitimately answers no storage request is
 * its own sliced wear-leveling consolidation erase: the slices run back to
 * back, their gaps run the keyboard pass and not the wire, and fpark holds
 * the response plan -- measured 391-494 ms across this family's captures
 * (era_performance_gates.md's consolidation band; 439/474 ms re-read
 * 2026-08-13). Doubling that ceiling is the same slack convention the
 * initiator-silence asserts use (era_split_transport_scheduler_timing.c).
 * At the 25 ms retry cadence the streak therefore spans >= 1 s of continuous
 * peer silence before it aborts. Because the streak advances once per
 * deadline evaluation from the cold cadence, a stall of THIS half's own
 * core0 -- its own consolidation -- contributes one failure whatever its
 * width: a local flash window is evidence about this half, never about the
 * peer. The relation's 100 ms silence watch keeps its no-exemptions stance
 * untouched: it reads core1's accepted frames, which an apply never stops;
 * this bound reads storage answers, which an apply legitimately does.
 *
 * Three-at-75 ms was the old bound. It sat inside the peer's own erase
 * window, so every consolidation-bearing apply aborted a healthy episode
 * into SESSION_STATUS revalidation and a reopen MATCH re-proof --
 * device-measured 2026-08-13: one VIA layout load cost abort=2, timeout=12,
 * dup=170 and 19 status rounds, all self-healed, none necessary. */
#define ERA_HOST_PEER_STORAGE_PEER_STALL_WORST_MS 500U
#define ERA_HOST_PEER_STORAGE_PEER_SILENCE_MS (2U * ERA_HOST_PEER_STORAGE_PEER_STALL_WORST_MS)
#define ERA_HOST_PEER_STORAGE_MAX_FAILURES (ERA_HOST_PEER_STORAGE_PEER_SILENCE_MS / ERA_HOST_PEER_STORAGE_RETRY_MS)
_Static_assert(ERA_HOST_PEER_STORAGE_PEER_SILENCE_MS % ERA_HOST_PEER_STORAGE_RETRY_MS == 0U,
               "The peer-silence bound must be a whole number of retry periods, or the streak quantizes short of it.");
_Static_assert(ERA_HOST_PEER_STORAGE_MAX_FAILURES >= 3U && ERA_HOST_PEER_STORAGE_MAX_FAILURES < 255U,
               "The failure streak must fit its uint8 counter and never fall below the old three-strike floor.");
_Static_assert(2U * ERA_HOST_PEER_STORAGE_PEER_SILENCE_MS <= ERA_HOST_PEER_STORAGE_EPISODE_MS,
               "The peer-silence bound must stay well inside the per-phase episode deadline, or the deadline fires first and this derivation is dead text.");
#define ERA_HOST_PEER_STORAGE_BACKOFF_MAX_SHIFT 6U
#define ERA_HOST_PEER_STORAGE_BACKOFF_MAX_MS 1000U
#define ERA_HOST_PEER_STORAGE_DIAGNOSTICS_BYTES 88U
/* Recency layer (Slice 10). The baseline record layout version is XORed into
 * the record guard, so changing the layout invalidates every stored guard
 * and degrades conservatively instead of misreading old baselines. */
#define ERA_HOST_PEER_STORAGE_BASELINE_GUARD_XOR 0x45524331UL
#define ERA_HOST_PEER_STORAGE_DIVERGENCE_COUNTER_MAX UINT16_MAX

/* Core0 cold transaction/due state footprint, shared with the communication-core
   aggregate budget. The struct types stay private to era_host_peer_storage.c
   (publishing them would hand the core1 wire translation unit the ability to name
   core0 live-state types), so this macro is the single crossing point and
   era_host_peer_storage.c asserts it with `==`, not `<=`: an inequality silently
   absorbs struct growth, which is exactly how the peer literal in the aggregate
   drifted four bytes behind when 8.3 added the sliced-apply cursor. 136 -> 140
   at the Slice 10 relation-neutral split (local truth 72 + relation-scoped 16 +
   episode 52, four bytes of struct padding), then 140 -> 144 when the
   relation-scoped struct took the arbitration bookkeeping (16 -> 20), then
   144 -> 148 when Slice 11.7 gave the settled-dirty hint its own advertised
   and re-arm masks (20 -> 24, two bytes plus two of padding). Those two
   retired one arbitration flag pair and the whole round-end level re-read, so
   the mechanism shrank while the record grew.

   **144 -> 136 on 2026-08-11**, the second time it has come down and the first
   for a reason that is not a carrier change: the local truth record's
   `capture_count` and `image_schema` were written on every settled capture and
   read by nothing, and taking them took four bytes plus four of padding
   (72 -> 64).

   **148 -> 144 at D2, and it was the first time this figure had come down.**
   The hint stopped naming domains and stopped arming probes, so the in-hand
   set and the re-arm budget went with the round-end re-read they bounded, and
   the advertised mask became one news value (24 -> 20, two bytes plus two of
   padding). Every prior entry here bought a compensation for a carrier that
   could not express its own state; this one is what the compensations were
   for, removed.

   **136 -> 144 at the 2026-08-14 indicator redesign**, two facts and their
   padding. The local truth record gained the RAM changed-vs-baseline mask
   (64 -> 68): one byte maintained at exactly the three sites that already
   read the persisted baseline record, so the lamp predicate can ask "does
   un-converged content exist" without an EEPROM read and without a term the
   relation rotation drops. The relation record gained the indicator bits
   (20 -> 24): the peer's advertised storage-pending mirror plus the cached
   serviceability-and-policy gate, the two facts the lamp reads that only the
   cold pass can produce. What these bytes retire is the fixed 1500 ms lamp
   bridge — a time constant standing in for a wire fact — so the record grew
   where a timer died.

   ERA NVM replacement reduces this record to **140 bytes**. There is no
   staged-old slice, write cursor, rollback phase, or raw/public facade: the
   NVM engine keeps its mounted public image old for the entire synchronous
   commit and publishes the candidate only after the durable commit record.
   Compared with the old 180-byte state, the full 40-byte reduction includes
   the two retired deferred-abort/internal-read bytes plus their alignment. */
#define ERA_HOST_PEER_STORAGE_CORE0_STATE_BYTES 140U

#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
#    ifndef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
#        error ERA storage cause timeline requires wire diagnostics
#    endif
/* Both numbers were sized for an episode that fits inside a keymap transfer,
   and a 16 KiB macro is not one. Device-run 2026-08-02: the ring filled at
   t=113 ms of an episode whose bulk stream alone runs ~480 ms, so nothing past
   the first fourteen chunks was recorded and no abort, forget or stale event
   ever reached it. The byte timestamp was the harder wall of the two --
   clamped at 254 ms, it cannot express a 461 ms flash window whatever the ring
   size is. Capacity 32 and a 16-bit millisecond stamp, with `CHUNK_RESULT`
   sampled below so the stream stops eating the ring. This is a selector-gated
   temporary instrument and is sized to answer one question, not to be
   permanent. */
#    define ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_EVENT_CAPACITY 32U
#    define ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_RECORD_BYTES 112U
#    define ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_STATIC_BYTES (ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_RECORD_BYTES * 2U)
#    define ERA_HOST_PEER_STORAGE_CAUSE_EDGE_EVENT_CAPACITY 64U
#    define ERA_HOST_PEER_STORAGE_CAUSE_EDGE_RECORD_BYTES 696U
#    define ERA_HOST_PEER_STORAGE_CAUSE_EDGE_STATIC_BYTES (ERA_HOST_PEER_STORAGE_CAUSE_EDGE_RECORD_BYTES * 2U)
/* The cause records are a selector-gated diagnostic surface and never a
   production build (`era_performance_gates.md`), so they sit on top of the
   production cap instead of inside it. Expressing that as a profile budget
   keeps the cap assert armed in every profile; the previous shape compiled the
   cap check out of the one build that exceeded it and left an equality against
   an over-cap constant as the only surviving check. */
#    define ERA_HOST_PEER_STORAGE_PROFILE_BUDGET_BYTES (ERA_HOST_PEER_STORAGE_STATIC_BUDGET_BYTES + ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_STATIC_BYTES + ERA_HOST_PEER_STORAGE_CAUSE_EDGE_STATIC_BYTES)

typedef enum {
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_NONE = 0,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_CHUNK_RESULT,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_APPLY_READY,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_APPLY_BEGIN,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_EEPROM_BEGIN,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_EEPROM_END,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_CORE1_RESTART,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_SESSION_SUBMIT,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_SESSION_RESULT,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_SESSION_RX,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_RESPONDER_STALE,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_SESSION_FORGET,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_REVALIDATED,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_COMPLETE_SUBMIT,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_COMPLETE_RESULT,
    ERA_HOST_PEER_STORAGE_CAUSE_EVENT_ABORT,
} era_host_peer_storage_cause_event_t;

typedef struct {
    uint32_t anchor_ms;
    uint16_t transaction_generation;
    uint16_t stale_limit_ms;
    uint16_t stale_watch_age_ms;
    uint8_t  role;
    uint8_t  domain;
    uint8_t  event_count;
    uint8_t  overflow;
    uint8_t  event[ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_EVENT_CAPACITY];
    /* Milliseconds, and 16-bit because the episode this exists to explain is
       measured in hundreds of them. */
    uint16_t elapsed_ms[ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_EVENT_CAPACITY];
} era_host_peer_storage_cause_timeline_t;

/* Interval-scoped edge recorder for the two questions the episode-local cause
 * timeline cannot answer: which pending arm fell at an indicator blink, and
 * whether a fragmented dynamic-macro write stream repeatedly crossed the
 * one-second settle boundary. WIRE_DIAG snapshots and then resets it, so a
 * baseline press followed by one operator action yields one bounded record. */
typedef enum {
    ERA_HOST_PEER_STORAGE_CAUSE_EDGE_NONE = 0,
    ERA_HOST_PEER_STORAGE_CAUSE_EDGE_ADVERTISED_RISE,
    ERA_HOST_PEER_STORAGE_CAUSE_EDGE_ADVERTISED_FALL,
    ERA_HOST_PEER_STORAGE_CAUSE_EDGE_MIRROR_RISE,
    ERA_HOST_PEER_STORAGE_CAUSE_EDGE_MIRROR_FALL,
    ERA_HOST_PEER_STORAGE_CAUSE_EDGE_INDICATOR_RISE,
    ERA_HOST_PEER_STORAGE_CAUSE_EDGE_INDICATOR_FALL,
    ERA_HOST_PEER_STORAGE_CAUSE_EDGE_SERVICE_LEAVE,
    ERA_HOST_PEER_STORAGE_CAUSE_EDGE_CHANGED_RISE,
    ERA_HOST_PEER_STORAGE_CAUSE_EDGE_CHANGED_FALL,
    ERA_HOST_PEER_STORAGE_CAUSE_EDGE_DIRTY_RISE,
    ERA_HOST_PEER_STORAGE_CAUSE_EDGE_DIRTY_FALL,
} era_host_peer_storage_cause_edge_event_t;

/* Cause-build-only path probe for the DUAL-HOST initiator's STORAGE_PENDING
 * latest-state carrier. The four stages are ownership boundaries rather than
 * another interpretation of storage state: Core0 standing-plan publication,
 * Core1 confirmed wire TX, responder Core1 decode, and responder Core0 mirror
 * apply. A large interval therefore names the layer that owns it. */
typedef enum {
    ERA_HOST_PEER_STORAGE_CAUSE_PENDING_PLAN_PUBLISH = 0,
    ERA_HOST_PEER_STORAGE_CAUSE_PENDING_INITIATOR_TX,
    ERA_HOST_PEER_STORAGE_CAUSE_PENDING_RESPONDER_RX,
    ERA_HOST_PEER_STORAGE_CAUSE_PENDING_MIRROR_APPLY,
    ERA_HOST_PEER_STORAGE_CAUSE_PENDING_STAGE_COUNT,
} era_host_peer_storage_cause_pending_stage_t;

enum {
    ERA_HOST_PEER_STORAGE_CAUSE_PENDING_PUBLISH_ENABLED        = 1U << 0,
    ERA_HOST_PEER_STORAGE_CAUSE_PENDING_PUBLISH_STATUS_PENDING = 1U << 1,
    ERA_HOST_PEER_STORAGE_CAUSE_PENDING_PUBLISH_EXCLUSIVE      = 1U << 2,
};

enum {
    ERA_HOST_PEER_STORAGE_CAUSE_ARM_DIRTY            = 1U << 0,
    ERA_HOST_PEER_STORAGE_CAUSE_ARM_CHANGED          = 1U << 1,
    ERA_HOST_PEER_STORAGE_CAUSE_ARM_CELL             = 1U << 2,
    ERA_HOST_PEER_STORAGE_CAUSE_ARM_SUMMARY          = 1U << 3,
    ERA_HOST_PEER_STORAGE_CAUSE_ARM_CONTENT_EXPECTED = 1U << 4,
    ERA_HOST_PEER_STORAGE_CAUSE_ARM_MOVING           = 1U << 5,
    ERA_HOST_PEER_STORAGE_CAUSE_ARM_MIRROR           = 1U << 6,
    ERA_HOST_PEER_STORAGE_CAUSE_ARM_GATE             = 1U << 7,
};

typedef struct {
    uint32_t interval_start_ms;
    /* Raw RP2040 timer epoch for the four-stage pending-path probe. Two stages
     * run on bare Core1, so this timeline may not call QMK/ChibiOS timer APIs:
     * timer_hw->timerawl is the one counter both cores can read directly. */
    uint32_t pending_path_interval_start_us;
    uint32_t macro_dirty_count;
    uint16_t macro_first_elapsed_ms;
    uint16_t macro_last_elapsed_ms;
    uint16_t macro_gap_last_ms;
    uint16_t macro_gap_max_ms;
    uint16_t macro_gap_over_quiet_count;
    uint8_t  event_count;
    uint8_t  overflow;
    uint8_t  advertised_valid;
    uint8_t  advertised_pending;
    uint8_t  indicator_valid;
    uint8_t  indicator_pending;
    uint8_t  event[ERA_HOST_PEER_STORAGE_CAUSE_EDGE_EVENT_CAPACITY];
    uint8_t  arms[ERA_HOST_PEER_STORAGE_CAUSE_EDGE_EVENT_CAPACITY];
    uint8_t  state[ERA_HOST_PEER_STORAGE_CAUSE_EDGE_EVENT_CAPACITY];
    uint8_t  domain[ERA_HOST_PEER_STORAGE_CAUSE_EDGE_EVENT_CAPACITY];
    uint8_t  dirty_mask[ERA_HOST_PEER_STORAGE_CAUSE_EDGE_EVENT_CAPACITY];
    uint8_t  changed_mask[ERA_HOST_PEER_STORAGE_CAUSE_EDGE_EVENT_CAPACITY];
    uint16_t transaction_generation[ERA_HOST_PEER_STORAGE_CAUSE_EDGE_EVENT_CAPACITY];
    uint16_t elapsed_ms[ERA_HOST_PEER_STORAGE_CAUSE_EDGE_EVENT_CAPACITY];
    /* `WIRE_DIAG` interval-relative timestamps. UINT16_MAX means that edge did
     * not occur. Each stage has exactly one writer (Core0, Core1, Core1, Core0
     * in enum order), so validity and level are stage-local bytes rather than
     * shared read-modify-write masks. An initial observed zero is a baseline
     * rather than a fabricated fall. Only PLAN_PUBLISH uses the contexts. */
    uint16_t pending_path_rise_ms[ERA_HOST_PEER_STORAGE_CAUSE_PENDING_STAGE_COUNT];
    uint16_t pending_path_fall_ms[ERA_HOST_PEER_STORAGE_CAUSE_PENDING_STAGE_COUNT];
    uint8_t  pending_path_valid[ERA_HOST_PEER_STORAGE_CAUSE_PENDING_STAGE_COUNT];
    uint8_t  pending_path_level[ERA_HOST_PEER_STORAGE_CAUSE_PENDING_STAGE_COUNT];
    uint8_t  pending_publish_rise_context;
    uint8_t  pending_publish_fall_context;
} era_host_peer_storage_cause_edge_t;

_Static_assert(sizeof(era_host_peer_storage_cause_timeline_t) == ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_RECORD_BYTES,
               "ERA storage cause timeline record budget changed.");
_Static_assert(sizeof(era_host_peer_storage_cause_edge_t) == ERA_HOST_PEER_STORAGE_CAUSE_EDGE_RECORD_BYTES,
               "ERA storage cause edge record budget changed.");
#else
#    define ERA_HOST_PEER_STORAGE_PROFILE_BUDGET_BYTES ERA_HOST_PEER_STORAGE_STATIC_BUDGET_BYTES
#endif

typedef struct {
    uint32_t image_crc32;
    uint32_t source_revision;
    uint32_t dirty_generation;
    uint16_t image_size;
    uint8_t  domain;
    uint8_t  schema;
} era_host_peer_storage_manifest_entry_t;

/* Cold read-only view of the relation-scoped hint and arbitration state, taken
 * between fences at print time.
 *
 * It carried fifteen more members — the local store's dirty mask, capture and
 * revision counters, and the pinned image's identity, size, schema and validity
 * flags — from the Slice 10 family split until every printer that read them was
 * retired without the builder following. A snapshot member with no reader is
 * not free the way an unused struct field usually is: this one is filled on
 * every call, so it costs the copy and the stack on each of the two call sites,
 * and it costs the next session the belief that something observes the value.
 * The pinned image's own live state is not weakened by their removal — it is
 * read directly where it is used, and `image_publication_seq` in particular is
 * a `volatile` seqlock counter core1 reads, which this copy never was. */
typedef struct {
    uint8_t settled_news_value;
    uint8_t probe_pending_mask;
    /* Arbitration cell queues and the last peer summary (Slice 10):
     * which domains wait on a pull probe, a push, or the counter
     * exchange, and what the peer last declared changed. */
    uint8_t push_pending_mask;
    uint8_t conflict_pending_mask;
    uint8_t peer_changed_mask;
    uint8_t arbitration_flags;
    /* Display-only provenance: boot-invalid local changed bits, relation cells
     * derived from baseline uncertainty, and the actual-transfer/fault latch
     * that promotes the current initiator round. Diagnostics only. */
    uint8_t provisional_changed_mask;
    uint8_t provisional_cell_mask;
    uint8_t indicator_round_confirmed;
} era_host_peer_storage_foundation_snapshot_t;

/* Cold read-only view of the persisted recency layer, built at call time
 * from EEPROM plus the cached manifests; no static shadow exists.
 * `changed_mask` bit d means the domain's current manifest CRC differs from
 * its persisted baseline; with an invalid guard every bit is set, which is
 * the conservative degradation the contract requires.
 *
 * The baseline CRCs themselves do not cross into this view. Every consumer
 * wants the comparison's answer rather than its input, and the builder does
 * the comparison against the record it just read, so carrying them here only
 * put seven words of stored truth on five callers' stacks for nothing to
 * read. The persisted record keeps them. */
typedef struct {
    uint16_t divergence_counter[ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT];
    uint8_t  baseline_record_valid;
    uint8_t  changed_mask;
} era_host_peer_storage_recency_snapshot_t;

typedef struct {
    uint32_t now_ms;
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint16_t policy_generation;
    uint16_t peer_usb_epoch;
    uint16_t peer_host_open_generation;
    uint16_t peer_host_close_generation;
    uint8_t  mode;
    uint8_t  owner_ready;
    uint8_t  local_initiator;
    uint8_t  general_initiator_pending;
    uint8_t  status_revalidation_due;
    uint8_t  indicator_fast_recovery_active;
    uint8_t  local_policy_requested;
    uint8_t  local_bulk_page_supported;
    uint8_t  peer_known;
    uint8_t  peer_host_open;
    uint8_t  peer_no_host;
    uint8_t  peer_bulk_page_supported;
    /* Arbitration tie-break input (Slice 10): conflicts resolve by
     * divergence count, tie to Left, so the engine needs the physical side
     * the reducer latched. */
    uint8_t  local_left;
} era_host_peer_storage_runtime_context_t;

typedef struct {
    uint32_t open_count;
    uint32_t close_count;
    uint32_t abort_count;
    uint32_t restart_count;
    uint32_t proof_count;
    uint32_t match_count;
    uint32_t transfer_count;
    uint32_t chunk_count;
    uint32_t duplicate_count;
    uint32_t retry_count;
    uint32_t timeout_count;
    uint32_t apply_count;
    uint32_t complete_count;
    uint32_t stale_count;
    uint32_t initiator_full_count;
    uint32_t responder_full_count;
    uint32_t integrity_reject_count;
    uint32_t version_reject_count;
    uint32_t domain_reject_count;
    uint32_t quiesce_fail_count;
    uint8_t  state;
    uint8_t  role;
    uint8_t  domain;
    uint8_t  status;
    uint8_t  active;
    uint8_t  exclusive;
    uint16_t source_changed_count;
} era_host_peer_storage_diagnostics_t;

_Static_assert(sizeof(era_host_peer_storage_manifest_entry_t) == ERA_HOST_PEER_STORAGE_MANIFEST_ENTRY_BYTES,
               "ERA HOST-PEER storage manifest entry must stay 16 bytes.");
_Static_assert(ERA_HOST_PEER_STORAGE_CHUNK_BYTES * ERA_HOST_PEER_STORAGE_MAX_CHUNKS >= ERA_HOST_PEER_STORAGE_IMAGE_BYTES,
               "ERA HOST-PEER storage chunk count does not cover the maximum image.");
_Static_assert(ERA_HOST_PEER_STORAGE_CHUNK_BYTES * (ERA_HOST_PEER_STORAGE_MAX_CHUNKS - 1U) < ERA_HOST_PEER_STORAGE_IMAGE_BYTES,
               "ERA HOST-PEER storage chunk count is not minimal.");
_Static_assert(sizeof(era_host_peer_storage_diagnostics_t) == ERA_HOST_PEER_STORAGE_DIAGNOSTICS_BYTES,
               "ERA HOST-PEER storage diagnostics budget changed.");

void era_host_peer_storage_init(void);
bool era_host_peer_storage_task(uint32_t now_ms);
bool era_host_peer_storage_runtime_task(const era_host_peer_storage_runtime_context_t *context);
/* Core0's cached admission fact. True from a successful storage publication
 * until that result is consumed or cancelled; no shared record is read. */
bool era_host_peer_storage_initiator_request_pending(void);
bool era_host_peer_storage_route_exclusive(void);
/* The EEPROM SYNC indicator's one fact (2026-08-14 redesign, baseline-
 * provenance refinement): this half knows of unfinished *visible* pair-level
 * storage work. It is the union of the local arm — dirty content inside its
 * quiet interval, non-provisional settled divergence, visible content-moving
 * cells, an in-session summary, or a visible episode span — gated on cached
 * serviceability and this half's own sync policy, with the peer's advertised
 * pending mirror
 * (the last applied value of the peer's carrier — the STORAGE_PENDING push
 * section toward a responder, STORAGE_NEWS bit7 toward an initiator). O(1)
 * RAM reads, no EEPROM, no timer: the lamp holds exactly while this returns
 * true, plus the minimum-visible floor era_split_eeprom_sync.c anchors at
 * the rise. Every term falls on a two-sided exchange or on the fact that
 * produced it, so both halves' lamps end within one poll of the initiator's
 * last close — the fixed trailing bridge this replaces stood in for the
 * mirror term. A short scheduler recovery that temporarily classifies the
 * relation LOCAL_NO_LINK does not retire that mirror: the pair operation still
 * exists and the fast bootstrap recovery is trying to re-establish its carrier.
 * If discovery reaches its backoff boundary, that continuity ends and the
 * ordinary service-leave retirement applies. A missing boot baseline remains fully conservative for
 * arbitration and restart safety but is display-provisional: MATCH-only audit
 * work stays dark, while TRANSFER or a real retry fault immediately promotes
 * the round. Doctrines preserved: an episode that moves nothing shows nothing
 * (probe, verify summary, MATCH, refusals and the relation-open audit sweep
 * stay dark), and a terminal refusal retires its domain's lamp claim. Entry
 * is synchronized in both edit directions — each half's
 * advertised fact includes its dirty phase and crosses on its own carrier,
 * so the pair rises together from either half's first write (owner rulings,
 * 2026-08-14 first and fourth sittings). */
bool era_host_peer_storage_indicator_pending(void);
/* The advertised fact — the whole local arm above, minus only the mirror
 * (a half never echoes the peer's claim back at it). It is what the
 * initiator's standing plan carries as the STORAGE_PENDING push section and
 * what the responder's standing answer carries as STORAGE_NEWS bit7. The
 * dirty phase is included (owner UX ruling, first device sitting
 * 2026-08-14): a VIA save is a multi-second write stream, so a
 * settle-anchored advertisement put the peer seconds behind the edited
 * half's rise and dipped it dark between one domain's convergence and the
 * next one's settle. The responder-edited direction stayed settle-bound for
 * one more sitting — the response mask had no free marker — until the
 * fourth sitting's owner ruling opened the news byte's reserved bit7 as the
 * missing carrier. */
bool era_host_peer_storage_advertised_pending(void);
/* Local presentation handshake around the active pending carrier. The
 * initiator publishes STORAGE_PENDING; the responder publishes STORAGE_NEWS
 * bit7. The role's existing sent-state boundary reports the last successfully
 * sent level, so a peer-confirmed one keeps this panel pending until a zero is
 * likewise confirmed. This is display-only and feeds neither arbitration nor
 * restart/CLEAN safety. */
void era_host_peer_storage_note_local_pending_sent(bool pending);
/* True while a storage episode is in flight that a link raise would tear:
 * a dirty write stream, decided cells, an in-session summary, or a moving
 * span. This is the raw functional view and never consults the indicator's
 * provisional masks. It still excludes the boot changed-shadow: the
 * relation-open audit clears that shadow and waits for the raise
 * (`era_split_link_runtime_settled()`), so waiting on it deadlocks CLEAN. */
bool era_host_peer_storage_restart_should_wait(void);
/* CLEAN-only barrier after storage quarantine. It waits for an admitted
 * runtime episode to reach its coherent teardown boundary, then permanently
 * retires the dedicated Core1 request/snapshot publications and their result
 * reservations. Dirty and queued future work is discarded, not drained. */
bool era_host_peer_storage_restart_quarantine_ready(void);
/* The apply of the peer's advertised pending fact: latches it into the
 * mirror the indicator reads. Each half receives exactly one direction —
 * the responder drain latches the STORAGE_PENDING push section, the
 * initiator drain latches STORAGE_NEWS bit7 — so the two callers never
 * contend. Cleared when the relation leaves service; kept across identity
 * rotations and their fast revalidation window, because the fact it mirrors
 * survives them and the live carrier re-crosses on the fresh relation either
 * way. A recovery that reaches bootstrap backoff is a real service departure
 * for presentation and retires it. */
void era_host_peer_storage_note_peer_pending(bool pending);
/* Successful LOCAL_QMK/MACRO commit notification from ERA's custom EEPROM
 * adapter. Remote Apply bypasses this path so convergence never re-dirties
 * itself as a local edit. */
void era_host_peer_storage_note_eeprom_commit(uint32_t offset, uint32_t length);
/* Diagnostics-only view of the indicator fact's arms: bit0 local arm, bit1
 * peer mirror, bit2 serviceability gate, bit3 derived wire-confirmed hold
 * (`local arm == 0 && last successfully sent local pending == 1`). For the wire
 * console's eeprom shim line; not a behavioral surface. */
uint8_t era_host_peer_storage_indicator_diag(void);
/* True while this half's core0 is inside a sliced durable apply, in either
   role. The scheduler's housekeeping gate reads it to pump the slices
   back-to-back; the question is what core0 is doing, never which role it is
   doing it as, and asking it the other way is what left the responder's apply
   walking at the cold cadence for the project's whole history. */
/* This half's storage news value, the responder's whole advertisement since D2:
   a forward-only 1..127 counter stepped once per settled capture, `0` meaning
   nothing to claim. Its one reader is the responder snapshot's plan build. */
uint8_t era_host_peer_storage_settled_news_value(void);
/* The peer's storage news value as the relation's lane delivered it. Any change
   to a nonzero value arms a summary; nothing else is derived from it, which is
   why the argument stopped being a domain mask at D2. */
void era_host_peer_storage_note_host_news(uint8_t news_value);
void era_host_peer_storage_get_foundation_snapshot(era_host_peer_storage_foundation_snapshot_t *snapshot);
void era_host_peer_storage_get_recency_snapshot(era_host_peer_storage_recency_snapshot_t *snapshot);
void era_host_peer_storage_get_diagnostics_snapshot(era_host_peer_storage_diagnostics_t *snapshot);
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
void era_host_peer_storage_cause_timeline_note(era_host_peer_storage_cause_event_t event, uint8_t detail);
void era_host_peer_storage_cause_timeline_note_stale(uint16_t watch_age_ms, uint16_t stale_limit_ms);
void era_host_peer_storage_get_cause_timeline_snapshot(era_host_peer_storage_cause_timeline_t *snapshot);
void era_host_peer_storage_cause_note_advertised(bool pending);
void era_host_peer_storage_cause_note_indicator(bool pending);
void era_host_peer_storage_cause_note_pending_path(era_host_peer_storage_cause_pending_stage_t stage, bool pending, uint8_t context);
void era_host_peer_storage_get_cause_edge_snapshot(era_host_peer_storage_cause_edge_t *snapshot);
void era_host_peer_storage_reset_cause_edge(void);
#endif
