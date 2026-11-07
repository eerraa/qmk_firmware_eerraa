// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "matrix.h"

#include "era_split_eeprom_sync.h"

#define ERA_HOST_PEER_STORAGE_SCHEMA 4U
#define ERA_HOST_PEER_STORAGE_IMAGE_BYTES 16384U

/* Domain sizes of the current schema, defined once because both cores need them. Core0
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
 * The dynamic-keymap size is geometry-parameterised: the schema fixes the
 * formula and the layer count, and the resolved number is the board's. The identical-image rule is what makes per-board resolution
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
#define ERA_HOST_PEER_STORAGE_SCHEMA_DYNAMIC_KEYMAP_LAYERS 6U
#define ERA_HOST_PEER_STORAGE_DOMAIN_ERA_CONFIG_BYTES 200U
#define ERA_HOST_PEER_STORAGE_DOMAIN_DYNAMIC_KEYMAP_BYTES (ERA_HOST_PEER_STORAGE_SCHEMA_DYNAMIC_KEYMAP_LAYERS * MATRIX_ROWS * MATRIX_COLS * 2U)
#define ERA_HOST_PEER_STORAGE_DOMAIN_DYNAMIC_MACRO_BYTES ERA_HOST_PEER_STORAGE_IMAGE_BYTES
#define ERA_HOST_PEER_STORAGE_DOMAIN_QMK_RGB_MATRIX_BYTES 8U
#define ERA_HOST_PEER_STORAGE_DOMAIN_QMK_KEYMAP_CONFIG_BYTES 2U
#define ERA_HOST_PEER_STORAGE_DOMAIN_QMK_DEFAULT_LAYER_BYTES 1U
#define ERA_HOST_PEER_STORAGE_DOMAIN_VIA_LAYOUT_OPTIONS_BYTES 1U
#define ERA_HOST_PEER_STORAGE_DOMAIN_MACRO_OPTIONS_BYTES 72U
#define ERA_HOST_PEER_STORAGE_MANIFEST_ENTRY_BYTES 16U
#define ERA_HOST_PEER_STORAGE_DIRTY_QUIET_MS 1000U
#define ERA_HOST_PEER_STORAGE_UPLOAD_IDLE_MS 5000U
#define ERA_HOST_PEER_STORAGE_UPLOAD_TOTAL_MS 30000U
#define ERA_HOST_PEER_STORAGE_OPERATION_MS 30000U

typedef enum {
    ERA_STORAGE_PRODUCER_MACRO,
    ERA_STORAGE_PRODUCER_SEMANTIC,
    ERA_STORAGE_PRODUCER_DEFERRED_RGB_MATRIX,
    ERA_STORAGE_PRODUCER_DEFERRED_KEYBOARD,
    ERA_STORAGE_PRODUCER_COUNT,
} era_host_peer_storage_producer_t;

uint32_t era_host_peer_storage_contribution_admit(era_host_peer_storage_producer_t producer, uint8_t mask);
bool era_host_peer_storage_contribution_live(uint32_t token);
void era_host_peer_storage_contribution_extend(uint32_t token, uint8_t mask);
bool era_host_peer_storage_contribution_committed(uint32_t token, uint8_t mask, bool final);
void era_host_peer_storage_contribution_progress(uint32_t token);
void era_host_peer_storage_contribution_end(uint32_t token, bool failed);
void era_host_peer_storage_operation_task(uint32_t now_ms);
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
/* V260909R1: +4 bytes for the relation/policy audit receipt, independent of
 * an adopted transaction identity. This remains an exact accounting cap, not
 * a relaxed physical limit; the SRAM free floor and wire budgets do not move. */
/* Eighth domain: one 16-byte manifest and one 4-byte dirty deadline. */
/* +16 bytes for storage-owned attempt, identity and semantic-intent state.
 * This accounting equality does not change the physical SRAM free floor. */
/* Four contribution seats add 76 bytes net; physical SRAM floors are unchanged. */
#define ERA_HOST_PEER_STORAGE_STATIC_BUDGET_BYTES (18352U + ERA_HOST_PEER_STORAGE_DIAGNOSTICS_BYTES)
#define ERA_HOST_PEER_STORAGE_EPISODE_MS 5000U
#define ERA_HOST_PEER_STORAGE_RETRY_MS 25U
/* The consecutive-failure abort bound is deliberately conservative. Its 1 s
 * span was established before the ERA NVM cutover from the widest measured
 * peer-side flash stall; that old sliced wear-leveling mechanism no longer
 * exists and the 500 ms term is not a latency claim for ERA NVM. What remains
 * load-bearing is the allowance itself: a peer's core0 can be unavailable to
 * the storage request/response lane while it completes one synchronous NVM
 * operation, even though core1 keeps relation liveness running from SRAM.
 * Reducing this bound therefore requires fresh device timing of the current
 * journal-room and mandatory-rotation NVM windows, per
 * era_performance_gates.md. At the 25 ms retry cadence the current value spans
 * 1 s of continuous missing storage answers before abort. A stall of THIS
 * half's own core0 contributes only the deadline evaluation that follows it:
 * a local flash window is evidence about this half, never about the peer. The
 * relation's 100 ms silence watch remains independent because it reads core1's
 * accepted frames, which a local NVM commit does not stop. */
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
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
#    define ERA_HOST_PEER_STORAGE_DIAGNOSTICS_BYTES 16U
#else
#    define ERA_HOST_PEER_STORAGE_DIAGNOSTICS_BYTES 0U
#endif
/* Recency layer (Slice 10). The baseline record layout version is XORed into
 * the record guard, so changing the layout invalidates every stored guard
 * and degrades conservatively instead of misreading old baselines. */
#define ERA_HOST_PEER_STORAGE_BASELINE_GUARD_XOR 0x45524332UL
#define ERA_HOST_PEER_STORAGE_DIVERGENCE_COUNTER_MAX UINT16_MAX

#define ERA_HOST_PEER_STORAGE_CORE0_STATE_BYTES 300U

typedef struct {
    uint32_t image_crc32;
    uint32_t source_revision;
    uint32_t dirty_generation;
    uint16_t image_size;
    uint8_t  domain;
    uint8_t  schema;
} era_host_peer_storage_manifest_entry_t;

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
    uint8_t  input_recovery_pending;
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
    uint32_t abort_count;
    uint32_t timeout_count;
    uint32_t integrity_reject_count;
    uint8_t  state;
    uint8_t  role;
    uint8_t  status;
} era_host_peer_storage_diagnostics_t;

_Static_assert(sizeof(era_host_peer_storage_manifest_entry_t) == ERA_HOST_PEER_STORAGE_MANIFEST_ENTRY_BYTES,
               "ERA HOST-PEER storage manifest entry must stay 16 bytes.");
_Static_assert(ERA_HOST_PEER_STORAGE_CHUNK_BYTES * ERA_HOST_PEER_STORAGE_MAX_CHUNKS >= ERA_HOST_PEER_STORAGE_IMAGE_BYTES,
               "ERA HOST-PEER storage chunk count does not cover the maximum image.");
_Static_assert(ERA_HOST_PEER_STORAGE_CHUNK_BYTES * (ERA_HOST_PEER_STORAGE_MAX_CHUNKS - 1U) < ERA_HOST_PEER_STORAGE_IMAGE_BYTES,
               "ERA HOST-PEER storage chunk count is not minimal.");
_Static_assert(sizeof(era_host_peer_storage_diagnostics_t) == 16U,
               "ERA HOST-PEER storage diagnostic snapshot size changed.");

void era_host_peer_storage_init(void);
bool era_host_peer_storage_task(uint32_t now_ms);
bool era_host_peer_storage_runtime_task(const era_host_peer_storage_runtime_context_t *context);
/* Explicit semantic work, never relation revalidation or a duplicate receipt. */
void era_host_peer_storage_note_semantic_mask(uint8_t mask);
bool era_host_peer_storage_content_attempt_active(void);
void era_tomak_sync_attempt_started_kb(void);
void era_tomak_sync_failure_report_kb(void);
/* Reevaluation notification only; no dirty or durable content claim. */
void era_host_peer_storage_note_local_news(void);
/* Core0's cached admission fact. True from a successful storage publication
 * until that result is consumed or cancelled; no shared record is read. */
bool era_host_peer_storage_initiator_request_pending(void);
bool era_host_peer_storage_route_exclusive(void);
/* Standing-runtime suppression is slightly wider than transfer exclusivity.
 * After a push's transfer is verified, the initiator keeps Core0 running while
 * the responder may be inside synchronous NVM Apply; only the standing cadence
 * stays suppressed until COMPLETE. Storage control traffic and ordinary route
 * admission keep their existing ownership. */
bool era_host_peer_storage_standing_suppressed(void);
/* O(1) gated observation of live local ownership or the valid peer mirror.
 * Neither retained divergence nor a sent shadow is live ownership. */
bool era_host_peer_storage_indicator_pending(void);
/* Source-local ownership only; never echo the mirror or sent shadow. */
bool era_host_peer_storage_advertised_pending(void);
/* Carrier diagnostics only; delivery never holds an operation or input. */
void era_host_peer_storage_note_local_pending_sent(bool pending);
/* True while a storage episode is in flight that a link raise would tear:
 * a dirty write stream, decided cells, an in-session summary, or a moving
 * span. This is the raw functional view and never consults the indicator's
 * provisional masks. It still excludes the boot changed-shadow: the
 * relation-open audit clears that shadow and waits for the raise
 * (`era_split_link_runtime_settled()`), so waiting on it deadlocks CLEAN. */
bool era_host_peer_storage_restart_should_wait(void);
/* Executable work only: an admitted episode or a published lane result.
 * Queued work, idle relations and presentation obligations are not owners. */
bool era_host_peer_storage_execution_pending(void);
bool era_host_peer_storage_attempt_pending(void);
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
/* A validated explicit COMMIT is a boundary, not an inferred quiet period. */
void era_host_peer_storage_note_macro_commit_ready(void);
uint8_t era_host_peer_storage_eeprom_domain_mask(uint32_t offset, uint32_t length);
void era_host_peer_storage_note_eeprom_mask(uint8_t mask);
/* This half's storage news value, the responder's whole advertisement since D2:
   a forward-only 1..127 counter stepped once per settled capture, `0` meaning
   nothing to claim. Its one reader is the responder snapshot's plan build. */
uint8_t era_host_peer_storage_settled_news_value(void);
/* The peer's storage news value as the relation's lane delivered it. Any change
   to a nonzero value arms a summary; nothing else is derived from it, which is
   why the argument stopped being a domain mask at D2. */
void era_host_peer_storage_note_host_news(uint8_t news_value);
/* Called only at the scheduler's relation-rotation boundary, alongside the
 * standing receive-cache reset and before accepting new-relation news. Audit
 * startup must not reset a value the current relation has already delivered. */
void era_host_peer_storage_note_relation_rotation(void);
void era_host_peer_storage_get_recency_snapshot(era_host_peer_storage_recency_snapshot_t *snapshot);
void era_host_peer_storage_get_diagnostics_snapshot(era_host_peer_storage_diagnostics_t *snapshot);
