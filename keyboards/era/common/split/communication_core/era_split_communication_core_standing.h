// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../era_host_peer_transaction.h"
#include "../era_split_transaction_types.h"
#include "../era_split_wire_protocol.h"

/* The standing exchange: one route kind in one relation, running on core1's own
 * period under a bounded core0 grant (era_route_contract.md, era_invariants.md).
 *
 * Why it is not the request/result rings. Those are correct for a one-off --
 * core0 decided "now", once -- and wrong for a subscription. Under a constant
 * cadence the rings make core0 pay per exchange, and worse than per exchange:
 * era_split_transport_scheduler_housekeeping_event_due()
 * (split/era_split_transport_scheduler.c) reports work for as long as
 * a request is in flight, so core0 runs a full housekeeping pass per *scan* of
 * the in-flight window rather than one per poll. At 19 kHz a 240 us exchange
 * spans about five of them, and each rebuilds state that did not change.
 *
 * The responder side already demonstrates the alternative: core0 publishes one
 * immutable generation-stamped snapshot and core1 answers arbitrarily many
 * requests from it, with nothing per request crossing the core boundary. These
 * two records are that shape given to the initiator.
 *
 * The refinement that keeps it legal: what crosses to core1 is transaction
 * *work*, never live QMK state. Core0 publishes the section bodies; core1
 * serializes them and still reads no live QMK state. */

/* core0 -> core1. Published on change, never on a pass and never on a timer:
 * publishing per pass would restore exactly the per-exchange cost this exists
 * to remove, and publishing a time-varying field would make every publish
 * differ -- the failure the responder snapshot already paid for once, measured
 * at 911 publishes per second. */
typedef struct {
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint16_t plan_generation;
    uint16_t poll_period_ms;
    /* How long core1 lets the wire stay quiet before it runs one section-less
     * exchange *regardless of the bit below*. Published rather than compiled in
     * because the constant it derives from is the scheduler's, and because
     * core0 owning the policy while core1 owns the timing is the whole shape of
     * this record. It is never zero on a published plan: the builder either
     * returns the record still memset, or falls through to the scheduler's
     * constant, which a _Static_assert holds above zero. */
    uint16_t liveness_period_ms;
    /* The grant itself. False suspends the standing exchange's *cadence*
     * without tearing anything down: core0 clears it for storage exclusivity,
     * for a pending SESSION_STATUS revalidation, and for any relation that is
     * not a confirmed DUAL-HOST Left. That is the whole of route priority,
     * resolved on core0 and published as one bit.
     *
     * **It does not suspend liveness** (Slice 11.7). It used to, and clearing
     * it handed the wire back to core0 at the one moment core0 was about to
     * disappear into a flash write -- so the peer's silence watch fired 100 ms
     * into a 1499 ms apply and forgot a relation that was perfectly alive. */
    uint8_t  enabled;
    uint8_t  eligible_push_sections;
    uint8_t  eligible_rsp_sections;
    /* The latest-state section bodies. Every field here must be stable while
     * the facts behind it are: the publish early-out compares the whole
     * record, so a field that moves on its own makes every publish differ and
     * "on change" quietly becomes "on pass". */
    uint8_t  input_layer;
    /* The initiator's storage-pending fact (2026-08-14 indicator redesign):
     * bit0 of the STORAGE_PENDING push section, filled where this relation's
     * push eligibility carries it — both serviced relations do. It moves
     * twice per operator action (rise at the first local write of the save,
     * fall at the last close), which is the cadence class the
     * publish-on-change rule was built for. Carried on the INPUT-class
     * discipline: the receiver holds it applied as the lamp's mirror arm, so
     * an invalid sent shadow forces the current value across once per
     * relation, zero included. */
    uint8_t  storage_pending;
    uint8_t  authority_valid;
    era_split_wire_authority_section_t authority;
    /* The DUAL-HOST push RGB body (Slice 12), filled only where this
     * relation's push eligibility carries the section and the sender's own
     * RGB requested bit is set -- a HOST-PEER plan never fills it
     * (plan-field-follows-eligibility, era_route_contract.md). The sleep bit
     * arrives already zeroed by the capture, which is what keeps a suspend
     * flip from making this plan differ. */
    uint8_t  rgb_valid;
    era_host_peer_rgb_state_t rgb;
    /* The DUAL-HOST push ACTIVITY body (FA-2 S2), under the same
     * field-follows-eligibility rule. The value is the tap activity unit's
     * advertised composition -- live counters only while the peer's window is
     * up, frozen otherwise -- which is what keeps ordinary typing from making
     * every publish differ. */
    uint8_t  activity_valid;
    era_split_wire_activity_section_t activity;
    /* The DUAL-HOST push visual baseline (Slice 14), under the same
     * field-follows-eligibility rule: this half's packed pressed baseline,
     * captured on the plan build only where eligibility and the sender's RGB
     * policy bit carry it. The reason byte is core1's to derive from its own
     * shadow, so the plan carries the baseline alone -- which only changes on
     * a local key edge, keeping the publish-on-change discipline. */
    uint8_t  visual_valid;
    uint8_t  visual_baseline[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_BASELINE_BYTES];
    /* The restart arm. It is the initiator's only half of the agreement -- the
     * request and the confirmation ride AUTHORITY, which this record already
     * carries -- and it obeys the same field-follows-eligibility rule as
     * everything above it. Publishing it on change is exactly right for what it
     * is: the fields move twice per owner action, at the arm and at its
     * retirement, and the deadline is an absolute instant rather than a
     * countdown, so a republished plan carries the same body rather than a
     * fresh one. An act of zero is the idle form. */
    uint8_t  restart_act;
    uint8_t  restart_param;
    uint32_t restart_commit_ms;
} era_split_communication_core_standing_plan_t;

/* core1 -> core0. Latest-state and deliberately not a queue: core1 overwrites
 * it, so a core0 that reads it late loses latency and never correctness, and a
 * core0 that reads it rarely loses nothing at all. A queue here would restore
 * per-exchange work on the drain side after removing it on the submit side. */
typedef struct {
    uint16_t owner_epoch;
    uint16_t relation_generation;
    uint8_t  peer_input_layer;
    uint8_t  peer_input_layer_valid;
    /* The peer's authority, reported only when the decoded record differs from
     * the last one delivered. That edge is the whole of Slice 11.6's "the
     * authority section is edge-consumed, exactly as INPUT_LAYER is": a level
     * carried in every reply would wake core0 at the poll rate and undo Slice
     * 11.5's responder fix, and nothing but a device capture would see it. */
    uint8_t  peer_authority_valid;
    era_split_wire_authority_section_t peer_authority;
    /* The responder's storage **news value**, on the same edge rule as the two
     * above and for the same reason: a byte delivered on every reply would wake
     * core0 at the poll rate for a value that has not moved.
     *
     * The field and the section constant say `news` since 2026-08-10. They said
     * `_dirty_mask` and `..._SECTION_STORAGE_SETTLED_DIRTY_MASK` from D2 until
     * then, because renaming a fourteen-file carrier inside a behaviour slice
     * makes the behaviour unreviewable, so D2's rename was scoped out of D3 and
     * landed on its own once D3 was committed. A capture or a patch naming
     * the old spelling predates that.
     *
     * What the byte carries is a forward-only 7-bit counter, one
     * step per settled capture whichever domain it was, `0` meaning nothing to
     * claim (`era_host_peer_storage.c`, `settled_news_value`). It was a
     * seven-bit per-domain mask through Slice 11.7 and D1; D2 replaced the fact
     * because a level has to be able to fall and that one could not -- its
     * clear was derived from convergence, and a domain proven while this half
     * already held newer content left the bit raised for ever. Since the
     * 2026-08-14 entry symmetry the same byte also carries the responder's
     * storage-pending fact in bit7 (`..._STORAGE_NEWS_FLAG_PENDING`), on the
     * identical edge -- a flag flip is a byte change and crosses like any
     * other transition. This record stores the byte whole; the two consumers
     * split it downstream, the news test masking to the value bits and the
     * lamp mirror latching the flag.
     *
     * Two more sentences here used to describe the old mechanism and are worth
     * keeping as what they now detect. The responder does not repeat a level
     * while it is nonzero: since D1 it is latest-state and edge-armed off a
     * sent-state shadow, so it sends a transition, plus a bounded forced
     * refresh whose own `!= 0` term now means "this relation has captured at
     * least once" and therefore repeats once per refresh period for the life of
     * the relation. And `era_host_peer_storage_note_host_news()` no
     * longer keeps an in-hand set -- that set, the re-arm budget and the
     * round-end re-read all retired at D2 with the domain identity that was
     * their only reason to exist. The consumer is one news test now: a value
     * equal to the one already taken arms nothing, and any other value arms a
     * whole-family `SYNC_STATUS` summary that re-derives every domain's
     * direction from both halves' current facts. A relation rotation still
     * clears this record and the consumer's `peer_news_value` together, which
     * is why both ends reopen a relation agreeing on zero.
     *
     * What makes the lost edge safe is therefore the sender's forced refresh
     * rather than a repeating level surviving it. What the edge buys is
     * unchanged: a settled responder costs the initiator's core0 exactly one
     * wake per change of the value, instead of the 50 ms `SESSION_STATUS`
     * round trip Slice 11.7 retired. */
    uint8_t  peer_storage_news;
    uint8_t  peer_storage_news_valid;
    /* The remaining HSRSP response sections, carried here so a relation whose
     * initiator polls from core1 delivers the same six the per-exchange path
     * delivered (R2). Each is eligibility-clipped on the way in, so a relation
     * that does not carry a section never populates its field and pays nothing
     * for it but the bytes.
     *
     * **Three of these are safe to re-apply and one is not, and the difference
     * is the whole reason this record is shaped the way it is.** Core0 applies
     * every valid field on the change-sequence wake without knowing which field
     * caused it, so a section whose apply is not idempotent would fire again on
     * every unrelated edge. Lock and RGB are guarded setters. **The storage
     * mask was in that list wrongly, and this is where the misclassification
     * lived** (D1): its consumer queues probe work, and the in-hand set this
     * comment credited was retired at each domain's own close -- so every
     * replay of a latched byte read as fresh news and armed another episode.
     * That set no longer exists at all, D2 having deleted it with the domain
     * identity it indexed; the sentence stays because it is why the
     * misclassification was possible, not because it describes anything live.
     * The field is idempotent now because the consumer tests the value against
     * the last one it took, which is the property this line asserted before
     * anything provided it. The visual snapshot is not: its reason byte can ask the
     * receiver to re-fire every pressed key, which is an event rather than a
     * level. `peer_visual_seq` is what keeps that exactly-once — core1 bumps it
     * only when it stores a genuinely new snapshot, and core0 replays only a
     * value it has not already applied, which is what the per-exchange path
     * gave for free by delivering one result per frame. */
    uint8_t  peer_lock_state;
    uint8_t  peer_lock_state_valid;
    uint8_t  peer_visual_valid;
    uint8_t  peer_visual_seq;
    era_host_peer_visual_snapshot_t peer_visual_snapshot;
    uint8_t  peer_rgb_state_valid;
    era_host_peer_rgb_state_t peer_rgb_state;
    /* The relation time anchor, and it is the one section that could not be
     * carried as the wire carries it. On the wire it is a timestamp, corrected
     * on apply by one fixed constant; through this record the apply can be an
     * arbitrary time later, and `sync_timer_update()` sets the shared clock
     * absolutely rather than nudging it. A stale timestamp applied verbatim
     * puts this half's clock behind by exactly its staleness, and re-applying a
     * latched one drags it back again on every later wake.
     *
     * So core1 publishes the instant it received the anchor beside the anchor,
     * and core0 adds the elapsed time at apply. That makes the pair a *level* —
     * the constant difference between the two halves' timers — which is what
     * the other five already are, and what this record's delivery discipline
     * requires. Re-application then recomputes the same clock rather than an
     * older one, so the anchor needs no sequence of its own.
     *
     * `rx_us` is `timer_hw->timerawl`, the per-chip free-running counter both
     * cores read. Only differences of it are used, so its origin never has to
     * be reconciled with core0's ChibiOS millisecond timer. */
    uint8_t  peer_time_anchor_valid;
    uint32_t peer_time_anchor_ms;
    uint32_t peer_time_anchor_rx_us;
    /* The responder's tap-hold activity (FA-2 S2), on the same edge rule as
     * every latest-state field here: reported only when the decoded record
     * differs from the last one delivered. */
    uint8_t  peer_activity_valid;
    era_split_wire_activity_section_t peer_activity;
    /* The standing exchange stopped on a failed transaction and will not
     * resume on its own. Core0's ordinary stale/revalidation path is what
     * restarts it, by republishing the plan for a reconfirmed relation. */
    uint8_t  stopped;
    /* Initiator-side confirmation of the last STORAGE_PENDING value that
     * completed a successful standing transaction. This is not another wire
     * fact: it reflects the existing sent-state shadow so core0 can keep the
     * local STATUS lamp up across a pending 1 -> 0 edge until the responder has
     * actually seen that zero. It changes only when that section crosses. */
    uint8_t  local_storage_pending;
    uint8_t  local_storage_pending_valid;
    /* No per-exchange result/failure/request-sent mirrors, and no plan
     * generation. This record is latest-state for the fields core0 acts on;
     * those four were written on every exchange, read by nobody, and had no
     * console field or gate leg — they rode core1's stack on every publish
     * (the standing service chain's measured stack figure includes this
     * struct) to say something only a `wire cstd` line that does not exist
     * could have shown. */
    uint32_t exchange_count;
    /* Runtime *sections*, never polls. That is what keeps the steady-state
     * legs readable under a constant cadence: an idle window reads 0/0 while
     * the frame counter rises at the poll rate. */
    uint32_t tx_section_count;
    uint32_t rx_section_count;
} era_split_communication_core_standing_state_t;

/* core0 side. The publish early-outs when the plan is identical to the
 * published one apart from its generation, which is what makes "on change"
 * true rather than intended; `differs` is the same test without the write, for
 * the staleness bit that decides whether a publish is owed at all. The caller
 * leaves plan_generation alone -- the publish assigns it. */
bool era_split_communication_core_standing_plan_differs(const era_split_communication_core_standing_plan_t *plan);
bool era_split_communication_core_publish_standing_plan(const era_split_communication_core_standing_plan_t *plan);
void era_split_communication_core_clear_standing(void);
/* The cheap per-scan detector: one aligned word, no lock, no retry. Core0
 * compares it against what it last consumed and only then takes the guarded
 * read below. */
uint32_t era_split_communication_core_standing_state_seq(void);
bool     era_split_communication_core_read_standing_state(era_split_communication_core_standing_state_t *state);
/* R7.1: the initiator silence watch's progress signal — the exchange count as
 * one aligned-word read. The guard sequence above protects cross-field
 * consistency, which a single monotonic counter does not need; a torn read is
 * impossible for an aligned word and a stale one costs a pass of latency. */
uint32_t era_split_communication_core_standing_exchange_count(void);

/* core1 side. Returns true when it ran an exchange this pass. It is also the
 * only writer of the received-value cache above, and the only place a response
 * section is decoded on this half, since D3 gave the section set one carrier.
 */
bool era_split_communication_core_standing_service_once(uint16_t owner_epoch);
